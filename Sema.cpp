#include "Sema.h"

using namespace QLang;
using namespace std;

// A member's declared type is only recorded on the typed AST when it is a
// CONCRETE type — never when it names one of the enclosing struct's generic
// parameters (e.g. Box<T>'s `T value`). Codegen substitutes those at
// monomorphization time; recording the bare parameter would make codegen read
// "T" instead of the concrete argument. Sema leaves such nodes nullptr so
// codegen keeps its substitution path (FR-010/FR-011).
static bool isGenericParamName( StructDefinition *structDef, const string &typeName )
{
	if ( structDef == nullptr )
		return false;
	for ( const auto &gp : structDef->getGenericParams() )
		if ( gp.mName == typeName )
			return true;
	return false;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

bool Sema::analyze( Module *module, Scope *scope, DiagnosticEngine &diag )
{
	// Extern-only modules (.bmod) provide types for resolution but are not
	// analyzed as user code (contracts/sema-pass.md).
	if ( module == nullptr || module->isExtern() )
		return true;

	Sema sema( scope, diag );

	// Struct/impl method bodies, then module-level function bodies, then test
	// blocks. Order is not observable (sema emits nothing on success); it just
	// needs to reach every reachable expression.
	for ( auto &s : module->mStructList )
		sema.visitStruct( s );
	for ( auto &f : module->mFunctionList )
		sema.visitFunction( f );

	return !sema.mReported;
}

// ---------------------------------------------------------------------------
// Declaration walk
// ---------------------------------------------------------------------------

void Sema::visitStruct( StructDefinition *structDef )
{
	if ( structDef == nullptr )
		return;
	for ( auto &method : structDef->mMethods )
		visitFunction( method );
	if ( structDef->mInitMethod != nullptr )
		visitFunction( structDef->mInitMethod );
}

void Sema::visitFunction( FunctionDefinition *func )
{
	if ( func == nullptr )
		return;
	// requires/ensures clauses are expressions; the body is a Block.
	for ( auto &req : func->mRequiresClauses )
		visitExpr( req );
	for ( auto &ens : func->mEnsuresClauses )
		visitExpr( ens );
	if ( func->mFuncBody != nullptr )
		visitStmt( func->mFuncBody );
}

// ---------------------------------------------------------------------------
// Statement walk
// ---------------------------------------------------------------------------

void Sema::visitStmt( Statement *stmt )
{
	if ( stmt == nullptr )
		return;

	// Expressions ARE statements (Expression : Statement). Route them through
	// the expression walk, which recurses into every sub-expression and any
	// nested block (lambda/match/spawn bodies).
	if ( auto *e = dynamic_cast<Expression *>( stmt ) )
	{
		visitExpr( e );
		return;
	}

	if ( auto *s = dynamic_cast<Block *>( stmt ) )
	{
		for ( auto &child : s->mStatementList )
			visitStmt( child );
	}
	else if ( auto *s = dynamic_cast<ReturnStatement *>( stmt ) )
	{
		visitExpr( s->mExpression );
	}
	else if ( auto *s = dynamic_cast<IfStatement *>( stmt ) )
	{
		visitExpr( s->mIfExpression );
		visitStmt( s->mStatement );
		visitStmt( s->mElseStatement );
	}
	else if ( auto *s = dynamic_cast<WhileStatement *>( stmt ) )
	{
		visitExpr( s->mLoopExpression );
		visitStmt( s->mLoopStatement );
	}
	else if ( auto *s = dynamic_cast<ForInStatement *>( stmt ) )
	{
		visitExpr( s->mIterableExpression );
		visitStmt( s->mBody );
	}
	else if ( auto *s = dynamic_cast<VariableDeclaration *>( stmt ) )
	{
		for ( auto &decl : s->mVariables )
			visitExpr( decl.mInitialValue );
	}
	else if ( auto *s = dynamic_cast<AssertStatement *>( stmt ) )
	{
		visitExpr( s->mExpression );
	}
	else if ( auto *s = dynamic_cast<WaitStatement *>( stmt ) )
	{
		visitExpr( s->mExpr );
	}
	else if ( auto *s = dynamic_cast<EventHandler *>( stmt ) )
	{
		visitExpr( s->mEventExpression );
		visitStmt( s->mBody );
	}
	// Leaf statements (Break/Continue/WaitAll) need no recursion.
}

// ---------------------------------------------------------------------------
// Expression walk + resolution/annotation
// ---------------------------------------------------------------------------

Type *Sema::visitExpr( Expression *expr )
{
	if ( expr == nullptr )
		return nullptr;

	// --- Literals: annotate with the primitive type (FR-010). ---
	if ( dynamic_cast<ConstInteger *>( expr ) )
	{
		// true/false are parsed as ConstInteger; int is a safe annotation here.
		Type *t = mScope->findType( "int" );
		expr->setResolvedType( t );
		return t;
	}
	if ( dynamic_cast<ConstFloat *>( expr ) )
	{
		Type *t = mScope->findType( "double" );
		expr->setResolvedType( t );
		return t;
	}
	if ( dynamic_cast<ConstString *>( expr ) || dynamic_cast<StringInterpolation *>( expr ) )
	{
		// Recurse into interpolation parts.
		if ( auto *si = dynamic_cast<StringInterpolation *>( expr ) )
			for ( auto &p : si->mParts )
				visitExpr( p );
		Type *t = mScope->findType( "string" );
		expr->setResolvedType( t );
		return t;
	}
	if ( dynamic_cast<ConstChar *>( expr ) )
	{
		Type *t = mScope->findType( "char" );
		expr->setResolvedType( t );
		return t;
	}

	// --- Variable reference: type is the variable's declared type. ---
	if ( auto *ve = dynamic_cast<VariableExpression *>( expr ) )
	{
		VariableDefinition *var = ve->getVariable();
		Type *t = ( var != nullptr ) ? var->getVariableType() : nullptr;
		expr->setResolvedType( t );
		return t;
	}

	// --- Function call: type is the callee's return type. ---
	if ( auto *ce = dynamic_cast<CallExpression *>( expr ) )
	{
		for ( auto &p : ce->mParams )
			visitExpr( p );
		Type *t = ( ce->mFunction != nullptr ) ? ce->mFunction->getReturnType() : nullptr;
		expr->setResolvedType( t );
		return t;
	}
	if ( auto *ic = dynamic_cast<IndirectCallExpression *>( expr ) )
	{
		for ( auto &p : ic->mParams )
			visitExpr( p );
		Type *t = nullptr;
		if ( ic->mFnVariable != nullptr )
		{
			if ( auto *ft = dynamic_cast<FunctionType *>( (Type *)ic->mFnVariable->getVariableType() ) )
				t = ft->getReturnType();
		}
		expr->setResolvedType( t );
		return t;
	}

	// --- Field access: resolve the member against the base's struct type. ---
	if ( auto *fa = dynamic_cast<FieldAccessExpression *>( expr ) )
	{
		Type *baseType = visitExpr( fa->getObject() );
		resolveFieldAccess( fa, baseType );
		return fa->getResolvedType();
	}

	// --- Method call: resolve the method against the base's struct type. ---
	if ( auto *mc = dynamic_cast<MethodCallExpression *>( expr ) )
	{
		Type *baseType = visitExpr( mc->mObject );
		for ( auto &a : mc->mArgs )
			visitExpr( a );
		resolveMethodCall( mc, baseType );
		return mc->getResolvedType();
	}

	// --- Index: element type is best-effort (element of Array<T>). ---
	if ( auto *ie = dynamic_cast<IndexExpression *>( expr ) )
	{
		Type *baseType = visitExpr( ie->getObject() );
		visitExpr( ie->getIndex() );
		Type *t = nullptr;
		if ( baseType != nullptr && baseType->getName() == "Array" &&
			 baseType->getNumTypeParams() > 0 )
			t = baseType->getTypeParam( 0 );
		expr->setResolvedType( t );
		return t;
	}

	// --- Operators: type follows the left operand (best-effort). ---
	if ( auto *op = dynamic_cast<OperationsExpression *>( expr ) )
	{
		Type *lt = visitExpr( op->mOp1 );
		visitExpr( op->mOp2 );
		expr->setResolvedType( lt );
		return lt;
	}
	if ( auto *un = dynamic_cast<UnaryExpression *>( expr ) )
	{
		Type *t = visitExpr( un->mOperand );
		expr->setResolvedType( t );
		return t;
	}

	// --- Assignments: type follows the assigned target (best-effort). ---
	if ( auto *as = dynamic_cast<AssignmentExpression *>( expr ) )
	{
		visitExpr( as->mValue );
		Type *t = ( as->mVariable != nullptr ) ? as->mVariable->getVariableType() : nullptr;
		expr->setResolvedType( t );
		return t;
	}
	if ( auto *fas = dynamic_cast<FieldAssignmentExpression *>( expr ) )
	{
		visitExpr( fas->mObject );
		visitExpr( fas->mValue );
		return nullptr;
	}
	if ( auto *ias = dynamic_cast<IndexAssignmentExpression *>( expr ) )
	{
		visitExpr( ias->mObject );
		visitExpr( ias->mIndex );
		visitExpr( ias->mValue );
		return nullptr;
	}

	// --- Composite expressions: recurse; type left for later units. ---
	if ( auto *al = dynamic_cast<ArrayLiteralExpression *>( expr ) )
	{
		for ( auto &el : al->mElements )
			visitExpr( el );
		Type *t = mScope->findType( "Array" );
		expr->setResolvedType( t );
		return t;
	}
	if ( auto *rg = dynamic_cast<RangeExpression *>( expr ) )
	{
		visitExpr( rg->mStart );
		visitExpr( rg->mEnd );
		return nullptr;
	}
	if ( auto *sl = dynamic_cast<StructLiteralExpression *>( expr ) )
	{
		for ( auto &v : sl->mFieldValues )
			visitExpr( v );
		Type *t = mScope->findType( sl->mTypeName );
		expr->setResolvedType( t );
		return t;
	}
	if ( auto *cons = dynamic_cast<ConstructExpression *>( expr ) )
	{
		for ( auto &a : cons->mArgs )
			visitExpr( a );
		Type *t = ( cons->mStructDef != nullptr ) ? mScope->findType( cons->mStructDef->getName() ) : nullptr;
		expr->setResolvedType( t );
		return t;
	}
	if ( auto *ec = dynamic_cast<EnumConstructExpression *>( expr ) )
	{
		for ( auto &a : ec->mArgs )
			visitExpr( a );
		return nullptr;
	}
	if ( auto *tr = dynamic_cast<TryExpression *>( expr ) )
	{
		visitExpr( tr->mOperand );
		return nullptr;
	}
	if ( auto *aw = dynamic_cast<AwaitExpression *>( expr ) )
	{
		visitExpr( aw->mOperand );
		return nullptr;
	}
	if ( auto *pp = dynamic_cast<PipelineExpression *>( expr ) )
	{
		visitExpr( pp->mInput );
		visitExpr( pp->mTransform );
		return nullptr;
	}
	if ( auto *mt = dynamic_cast<MatchExpression *>( expr ) )
	{
		visitExpr( mt->mSubject );
		for ( auto &arm : mt->mArms )
			visitStmt( arm.mBody );
		return nullptr;
	}
	if ( auto *lm = dynamic_cast<LambdaExpression *>( expr ) )
	{
		visitStmt( lm->mBody );
		return lm->mReturnType;
	}
	if ( auto *sp = dynamic_cast<SpawnStatement *>( expr ) )
	{
		visitStmt( sp->mBody );
		return nullptr;
	}

	// Query/insert/update/delete expressions carry `.field` query pseudo-
	// references (QueryFieldExpression), NOT struct member access — they are
	// deliberately not walked here so sema never mistakes a query field for an
	// unknown struct field. FunctionRefExpression and other leaves: no type.
	return nullptr;
}

// ---------------------------------------------------------------------------
// Member resolution
// ---------------------------------------------------------------------------

StructDefinition *Sema::structForType( Type *baseType )
{
	if ( baseType == nullptr )
		return nullptr;

	// findSymbol only returns real Symbols (structs/enums/functions/variables);
	// builtin types (string/Array/Buffer/int/…) are registered via addType and
	// are therefore invisible here. A generic struct instance (Box<int>) names
	// "Box", whose template struct carries the field/method NAMES unchanged, so
	// name-based lookup is correct across monomorphization. A generic PARAMETER
	// base (e.g. "T") is not a Symbol → nullptr → left unchecked (R5).
	Symbol *sym = mScope->findSymbol( baseType->getName() );
	return dynamic_cast<StructDefinition *>( sym );
}

void Sema::resolveFieldAccess( FieldAccessExpression *fa, Type *baseType )
{
	StructDefinition *structDef = structForType( baseType );
	if ( structDef == nullptr )
		return;  // builtin/generic/unknown base — not our error to raise (R5)

	const string &name = fa->getFieldName();

	// A hit annotates the access with the field's declared type — the same
	// Type identity codegen uses (FR-012), so the annotation and codegen agree.
	for ( auto &field : structDef->mFields )
	{
		if ( field->getName() == name )
		{
			Type *ft = field->getVariableType();
			if ( !isGenericParamName( structDef, ft->getName() ) )
				fa->setResolvedType( ft );  // concrete only (see helper)
			return;
		}
	}

	// A name that is a METHOD (referenced without a call) is a valid member;
	// do not reject it as an unknown field (leave its typing to a later unit).
	for ( auto &method : structDef->mMethods )
		if ( method->getName() == name )
			return;

	// Genuinely absent on a concrete struct → the located rejection this unit
	// adds (FR-006). This is the site that returned a silent nullptr in codegen.
	mDiag.error( fa->getLocation(),
		"type '" + baseType->getName() + "' has no field '" + name + "'" );
	mReported = true;
}

void Sema::resolveMethodCall( MethodCallExpression *mc, Type *baseType )
{
	StructDefinition *structDef = structForType( baseType );
	if ( structDef == nullptr )
		return;  // builtin (string/Array/Buffer) or unknown base — leave it (R5)

	const string &name = mc->mMethodName;

	for ( auto &method : structDef->mMethods )
	{
		if ( method->getName() == name )
		{
			Type *rt = method->getReturnType();
			if ( rt != nullptr && !isGenericParamName( structDef, rt->getName() ) )
				mc->setResolvedType( rt );  // concrete only (see helper)
			return;
		}
	}

	// A fn-typed field invoked as a method (obj.callback(...)) is valid; codegen
	// lowers it as an indirect call. Any field with this name is enough to defer
	// (its call-validity is not U3's concern).
	for ( auto &field : structDef->mFields )
		if ( field->getName() == name )
			return;

	mDiag.error( mc->getLocation(),
		"type '" + baseType->getName() + "' has no method '" + name + "'" );
	mReported = true;
}
