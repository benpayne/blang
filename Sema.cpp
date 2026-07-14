#include "Sema.h"

#include <cctype>
#include <set>

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
// U4: type compatibility (closed conversion set — design decision 6)
// ---------------------------------------------------------------------------

// Scalar primitives that interconvert implicitly. Integer-width promotion is the
// documented implicit conversion; float<->double and the fact that bool/char are
// lowered to integers (true/false parse as ConstInteger "int") make the scalar
// family mutually compatible — matching codegen's existing coercions. Rejecting
// within this set would false-positive on pervasive bool/int/char/float mixing.
static bool isScalarTypeName( const string &n )
{
	return n == "int" || n == "long" || n == "short" || n == "byte" ||
	       n == "char" || n == "bool" || n == "float" || n == "double";
}

// A single upper-case letter is, by house convention, a generic type parameter
// (T, U, K, V). Treat it as compatible so U4 never rejects generic code (U5 owns
// constraint checking).
static bool looksGenericParam( const string &n )
{
	return n.size() == 1 && isupper( (unsigned char)n[0] );
}

// isCheckableType: a type whose values U4 can confidently compare. Scalars,
// string, and concrete user structs qualify. Enums (Result/Option/user), generic
// parameters, inferred `var`, `fn` function types, Array<T>, and unknown names do
// NOT — their values are not fully typed by U3, so U4 must not judge them.
bool Sema::isCheckableType( Type *t )
{
	if ( t == nullptr )
		return false;
	const string &n = t->getName();
	if ( n.empty() )
		return false;
	if ( isScalarTypeName( n ) || n == "string" )
		return true;
	return dynamic_cast<StructDefinition *>( mScope->findSymbol( n ) ) != nullptr;
}

// typesCompatible returns true (do NOT reject) unless BOTH types are concretely
// checkable and provably incompatible. nullptr / empty / non-checkable / generic
// types are never rejected — U3 leaves many nodes untyped and later units type
// them; U4 only fires on clearly determinable, clearly incompatible pairs to
// avoid false positives (FR-001/004/006). The only implicit conversions are the
// scalar family (integer width promotion + bool/char/float interchange).
bool Sema::typesCompatible( Type *from, Type *to )
{
	if ( from == nullptr || to == nullptr )
		return true;
	const string &f = from->getName();
	const string &t = to->getName();
	if ( f.empty() || t.empty() )
		return true;
	if ( f == t )
		return true;
	if ( looksGenericParam( f ) || looksGenericParam( t ) )
		return true;
	if ( !isCheckableType( from ) || !isCheckableType( to ) )
		return true;
	if ( isScalarTypeName( f ) && isScalarTypeName( t ) )
		return true;
	return false;
}

static string typeName( Type *t )
{
	return ( t != nullptr && !t->getName().empty() ) ? t->getName() : string( "<unknown>" );
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

bool Sema::analyze( Module *module, Scope *scope, DiagnosticEngine &diag )
{
	if ( module == nullptr || module->isExtern() )
		return true;

	Sema sema( scope, diag );

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
	if ( func == nullptr || func->isExtern() )
		return;

	for ( auto &req : func->mRequiresClauses )
		visitExpr( req );
	for ( auto &ens : func->mEnsuresClauses )
		visitExpr( ens );

	FunctionDefinition *saved = mCurrentFunc;
	mCurrentFunc = func;
	if ( func->mFuncBody != nullptr )
		visitStmt( func->mFuncBody );

	mCurrentFunc = saved;
}

// ---------------------------------------------------------------------------
// Statement walk
// ---------------------------------------------------------------------------

void Sema::visitStmt( Statement *stmt )
{
	if ( stmt == nullptr )
		return;

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
		Type *vt = visitExpr( s->mExpression );
		// Return-type checking (FR-001, FR-002). mCurrentFunc is null inside a
		// lambda body (see visitExpr LambdaExpression) so lambda returns are not
		// checked against the enclosing function.
		if ( mCurrentFunc != nullptr )
		{
			Type *rt = mCurrentFunc->getReturnType();  // nullptr = void
			bool hasValue = ( s->mExpression != nullptr );
			if ( rt != nullptr && !hasValue )
			{
				mDiag.error( s->getLocation(),
					"return with no value in function '" + mCurrentFunc->getName() +
					"' returning '" + typeName( rt ) + "'" );
				mReported = true;
			}
			else if ( rt != nullptr && hasValue && !typesCompatible( vt, rt ) )
			{
				mDiag.error( s->getLocation(),
					"cannot return '" + typeName( vt ) + "' from function '" +
					mCurrentFunc->getName() + "' returning '" + typeName( rt ) + "'" );
				mReported = true;
			}
		}
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
		{
			Type *initType = visitExpr( decl.mInitialValue );
			// Initializer compatibility (FR-004). Only when both types are
			// determinable and provably incompatible.
			if ( decl.mInitialValue != nullptr && decl.mVaribale != nullptr )
			{
				Type *declType = decl.mVaribale->getVariableType();
				if ( declType != nullptr && !typesCompatible( initType, declType ) )
				{
					mDiag.error( decl.mInitialValue->getLocation(),
						"cannot initialize '" + typeName( declType ) +
						"' from a value of type '" + typeName( initType ) + "'" );
					mReported = true;
				}
			}
		}
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
}

// ---------------------------------------------------------------------------
// Expression walk + resolution/annotation
// ---------------------------------------------------------------------------

Type *Sema::visitExpr( Expression *expr )
{
	if ( expr == nullptr )
		return nullptr;

	if ( dynamic_cast<ConstInteger *>( expr ) )
	{
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

	if ( auto *ve = dynamic_cast<VariableExpression *>( expr ) )
	{
		VariableDefinition *var = ve->getVariable();
		Type *t = ( var != nullptr ) ? var->getVariableType() : nullptr;
		expr->setResolvedType( t );
		return t;
	}

	if ( auto *ce = dynamic_cast<CallExpression *>( expr ) )
	{
		for ( auto &p : ce->mParams )
			visitExpr( p );
		FunctionDefinition *callee = ce->mFunction;
		Type *t = ( callee != nullptr ) ? callee->getReturnType() : nullptr;
		expr->setResolvedType( t );

		// Call arity + argument-type checking (FR-005, FR-006). Skip variadic,
		// generic, and builtin callees (their own paths validate). Argument-TYPE
		// checking additionally skips extern callees, whose string<->cstring /
		// carray FFI conversions are legitimate at the boundary.
		if ( callee != nullptr && !callee->isVariadic() && !callee->isGeneric() &&
		     !callee->isBuiltin() )
		{
			int np = callee->getNumberParams();
			int na = (int)ce->mParams.size();
			if ( na != np )
			{
				mDiag.error( ce->getLocation(),
					"wrong number of arguments to '" + callee->getName() +
					"': expected " + to_string( np ) + ", got " + to_string( na ) );
				mReported = true;
			}
			else if ( !callee->isExtern() )
			{
				for ( int i = 0; i < np; i++ )
				{
					Type *at = ce->mParams[i]->getResolvedType();
					Type *pt = callee->getParamType( i );
					if ( pt != nullptr && !typesCompatible( at, pt ) )
					{
						mDiag.error( ce->mParams[i]->getLocation(),
							"argument " + to_string( i + 1 ) + " to '" + callee->getName() +
							"': cannot pass '" + typeName( at ) + "' as '" + typeName( pt ) + "'" );
						mReported = true;
					}
				}
			}
		}

		// Generic constraint checking (REQ-008): for an explicit-type-argument call
		// to a generic function, each type argument bound to a constrained generic
		// parameter must satisfy that protocol constraint. Inferred (no explicit
		// type args) instantiations are left unchecked in U5.
		if ( callee != nullptr && callee->isGeneric() && !ce->mTypeArgs.empty() )
		{
			const auto &gps = callee->getGenericParams();
			for ( size_t i = 0; i < gps.size() && i < ce->mTypeArgs.size(); i++ )
			{
				if ( gps[i].mConstraint.empty() )
					continue;
				checkConstraint( ce->mTypeArgs[i], gps[i].mConstraint, gps[i].mName,
					ce->getLocation() );
			}
		}
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

	if ( auto *fa = dynamic_cast<FieldAccessExpression *>( expr ) )
	{
		Type *baseType = visitExpr( fa->getObject() );
		resolveFieldAccess( fa, baseType );
		return fa->getResolvedType();
	}

	if ( auto *mc = dynamic_cast<MethodCallExpression *>( expr ) )
	{
		Type *baseType = visitExpr( mc->mObject );
		for ( auto &a : mc->mArgs )
			visitExpr( a );
		resolveMethodCall( mc, baseType );
		return mc->getResolvedType();
	}

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

	if ( auto *op = dynamic_cast<OperationsExpression *>( expr ) )
	{
		Type *lt = visitExpr( op->mOp1 );
		Type *rt = visitExpr( op->mOp2 );
		// Operand validity (FR-007), conservative: reject arithmetic operators
		// applied to a determinable NON-scalar, NON-string operand (a struct/enum
		// value) — BLang has no operator overloading, so this is always invalid.
		// String '+' (concat) and any operand of unknown type are left alone.
		const string &oper = op->mOperation;
		bool isArith = ( oper == "+" || oper == "-" || oper == "*" ||
		                 oper == "/" || oper == "%" );
		// Comparison and logical operators yield bool, not the operand type.
		bool isBoolResult = ( oper == "==" || oper == "!=" || oper == "<" ||
		                      oper == ">" || oper == "<=" || oper == ">=" ||
		                      oper == "&&" || oper == "||" );
		if ( isArith )
		{
			for ( Type *o : { lt, rt } )
			{
				if ( o != nullptr && !o->getName().empty() &&
				     !isScalarTypeName( o->getName() ) &&
				     o->getName() != "string" && o->getName() != "Array" &&
				     !looksGenericParam( o->getName() ) &&
				     structForType( o ) != nullptr )
				{
					mDiag.error( op->getLocation(),
						"operator '" + oper +
						"' cannot be applied to a value of type '" + typeName( o ) + "'" );
					mReported = true;
					break;
				}
			}
		}
		Type *resultType = isBoolResult ? mScope->findType( "bool" ) : lt;
		expr->setResolvedType( resultType );
		return resultType;
	}
	if ( auto *un = dynamic_cast<UnaryExpression *>( expr ) )
	{
		Type *t = visitExpr( un->mOperand );
		expr->setResolvedType( t );
		return t;
	}

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
		Type *subjType = visitExpr( mt->mSubject );
		for ( auto &arm : mt->mArms )
			visitStmt( arm.mBody );

		// Exhaustiveness (REQ-007): when the subject is a determinable enum and no
		// wildcard `_` arm is present, every variant must be covered by an arm.
		// Non-enum subjects (literals, `var`-inferred bindings) are left unchecked.
		EnumDefinition *ed = enumForType( subjType );
		if ( ed != nullptr )
		{
			bool hasWildcard = false;
			std::set<string> covered;
			for ( auto &arm : mt->mArms )
			{
				if ( arm.mIsWildcard )
					hasWildcard = true;
				else
					covered.insert( arm.mPattern );
			}
			if ( !hasWildcard )
			{
				for ( auto &v : ed->getVariants() )
				{
					if ( covered.find( v.mName ) == covered.end() )
					{
						mDiag.error( mt->getLocation(),
							"non-exhaustive match: variant '" + v.mName + "' of enum '" +
							ed->getName() + "' is not handled" );
						mReported = true;
						break;
					}
				}
			}
		}
		return nullptr;
	}
	if ( auto *lm = dynamic_cast<LambdaExpression *>( expr ) )
	{
		// Returns inside a lambda are checked against the lambda's own return
		// type, not the enclosing function; U4 defers lambda return checking, so
		// suppress the enclosing-function check while walking the lambda body.
		FunctionDefinition *saved = mCurrentFunc;
		mCurrentFunc = nullptr;
		visitStmt( lm->mBody );
		mCurrentFunc = saved;
		return nullptr;
	}
	if ( auto *sp = dynamic_cast<SpawnStatement *>( expr ) )
	{
		visitStmt( sp->mBody );
		return nullptr;
	}

	return nullptr;
}

// ---------------------------------------------------------------------------
// Member resolution (U3)
// ---------------------------------------------------------------------------

StructDefinition *Sema::structForType( Type *baseType )
{
	if ( baseType == nullptr )
		return nullptr;
	Symbol *sym = mScope->findSymbol( baseType->getName() );
	return dynamic_cast<StructDefinition *>( sym );
}

void Sema::resolveFieldAccess( FieldAccessExpression *fa, Type *baseType )
{
	StructDefinition *structDef = structForType( baseType );
	if ( structDef == nullptr )
		return;

	const string &name = fa->getFieldName();

	for ( auto &field : structDef->mFields )
	{
		if ( field->getName() == name )
		{
			Type *ft = field->getVariableType();
			if ( !isGenericParamName( structDef, ft->getName() ) )
				fa->setResolvedType( ft );
			return;
		}
	}

	for ( auto &method : structDef->mMethods )
		if ( method->getName() == name )
			return;

	mDiag.error( fa->getLocation(),
		"type '" + baseType->getName() + "' has no field '" + name + "'" );
	mReported = true;
}

void Sema::resolveMethodCall( MethodCallExpression *mc, Type *baseType )
{
	StructDefinition *structDef = structForType( baseType );
	if ( structDef == nullptr )
		return;

	const string &name = mc->mMethodName;

	for ( auto &method : structDef->mMethods )
	{
		if ( method->getName() == name )
		{
			Type *rt = method->getReturnType();
			if ( rt != nullptr && !isGenericParamName( structDef, rt->getName() ) )
				mc->setResolvedType( rt );
			return;
		}
	}

	for ( auto &field : structDef->mFields )
		if ( field->getName() == name )
			return;

	mDiag.error( mc->getLocation(),
		"type '" + baseType->getName() + "' has no method '" + name + "'" );
	mReported = true;
}

// ---------------------------------------------------------------------------
// U5: match/generics helpers
// ---------------------------------------------------------------------------

EnumDefinition *Sema::enumForType( Type *t )
{
	if ( t == nullptr )
		return nullptr;
	return dynamic_cast<EnumDefinition *>( mScope->findSymbol( t->getName() ) );
}

void Sema::checkConstraint( Type *arg, const string &constraint,
	const string &paramName, const SourceLocation &loc )
{
	if ( arg == nullptr || constraint.empty() )
		return;
	// Only a concrete user struct is judged; builtins / generic parameters /
	// unknown type arguments are left unchecked to avoid false positives.
	StructDefinition *sd = dynamic_cast<StructDefinition *>( mScope->findSymbol( arg->getName() ) );
	if ( sd == nullptr )
		return;
	ProtocolDefinition *pd = dynamic_cast<ProtocolDefinition *>( mScope->findSymbol( constraint ) );
	if ( pd == nullptr )
		return;  // unknown protocol — not judged
	// Structural conformance: the struct must implement every required method by
	// name (conformance is not otherwise recorded on StructDefinition).
	for ( auto &req : pd->getRequiredMethods() )
	{
		if ( req == nullptr )
			continue;
		bool found = false;
		for ( auto &m : sd->getMethods() )
			if ( m != nullptr && m->getName() == req->getName() )
			{
				found = true;
				break;
			}
		if ( !found )
		{
			mDiag.error( loc,
				"type '" + arg->getName() + "' does not satisfy constraint '" + constraint +
				"' on generic parameter '" + paramName + "': missing method '" +
				req->getName() + "'" );
			mReported = true;
			return;
		}
	}
}
