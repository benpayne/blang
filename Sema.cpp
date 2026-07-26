#include "Sema.h"

#include <cctype>
#include <set>
#include <map>

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
	// Container/enum kinds are mutually incompatible even though their generic
	// type arguments keep them out of the "checkable" set below: e.g. assigning
	// a `query T |> first` result (Option<T>) to an Array<T> is always wrong.
	{
		auto isContainerKind = []( const string &n ) {
			return n == "Array" || n == "Option" || n == "Result" ||
				n == "Buffer" || n == "Map" || n == "Set";
		};
		if ( isContainerKind( f ) && isContainerKind( t ) )
			return false;   // both containers, different kinds (f != t here)
	}
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

	// Table structs map fields 1:1 to SQL columns, so every field must have a
	// column representation (primitive or string). The row mapper would leave a
	// nested struct/array field silently null — a guaranteed null-deref on
	// first access — so reject it here, located, in all build modes.
	if ( structDef->isTable() )
	{
		for ( const auto &f : structDef->getFields() )
		{
			const Type *ft = f->getVariableType();
			string fn = ( ft != nullptr ) ? ft->getName() : string();
			if ( !isScalarTypeName( fn ) && fn != "string" )
			{
				mDiag.error( structDef->getLocation(),
					"table struct '" + structDef->getName() + "' field '" +
					f->getName() + "' has type '" + fn +
					"', which has no SQL column mapping (columns support "
					"int/long/short/char/bool/float/double/string)" );
				mReported = true;
			}
		}
	}

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
	mMoved.clear();
	mDeclLoopDepth.clear();
	mDeclSpawnDepth.clear();
	mLocalDecls.clear();
	mReferencedNames.clear();
	mLoopDepth = 0;
	mSpawnDepth = 0;
	if ( func->mFuncBody != nullptr )
		visitStmt( func->mFuncBody );

	// U1: warn on local variables declared but never referenced anywhere in the
	// function body. A pure lint (severity Warning); it does NOT set mReported, so
	// the compile still succeeds unless -Werror promotes it.
	for ( VariableDefinition *v : mLocalDecls )
	{
		if ( v != nullptr && mReferencedNames.count( v->getName() ) == 0 )
			mDiag.warning( v->getLocation(),
				"unused variable '" + v->getName() + "'", "unused-variable" );
	}

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
		mLoopDepth++;
		visitStmt( s->mLoopStatement );
		mLoopDepth--;
	}
	else if ( auto *s = dynamic_cast<ForInStatement *>( stmt ) )
	{
		visitExpr( s->mIterableExpression );
		mLoopDepth++;
		visitStmt( s->mBody );
		mLoopDepth--;
	}
	else if ( auto *s = dynamic_cast<VariableDeclaration *>( stmt ) )
	{
		for ( auto &decl : s->mVariables )
		{
			// U1: track this local as an unused-variable-lint candidate.
			if ( decl.mVaribale != nullptr )
				mLocalDecls.push_back( decl.mVaribale );
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
			// Channel element types are restricted to value types. A channel
			// transfers elements by raw byte copy (__blang_chan_send/recv), so a
			// refcounted heap element (string/Array/Buffer/struct) would be
			// copied without a reference count — its owner releases it at scope
			// exit, leaving the channel (and any recv) with a dangling pointer,
			// and undrained elements would leak at channel teardown. Origin's
			// channel feature only supports value elements; reject the rest with a
			// located diagnostic (reject, don't coerce) rather than crashing/leaking.
			if ( decl.mVaribale != nullptr )
			{
				Type *vt = decl.mVaribale->getVariableType();
				if ( vt != nullptr && vt->getName() == "chan" &&
					 vt->getNumTypeParams() > 0 &&
					 isHeapType( vt->getTypeParam( 0 ) ) )
				{
					mDiag.error( s->getLocation(),
						"channel element type '" +
						typeName( vt->getTypeParam( 0 ) ) +
						"' is not supported: channels carry value types only "
						"(a refcounted element would be copied without ownership)" );
					mReported = true;
				}
			}

			// U6: record the declaration's loop/spawn nesting for this variable.
			if ( decl.mVaribale != nullptr )
			{
				mDeclLoopDepth[ decl.mVaribale ] = mLoopDepth;
				mDeclSpawnDepth[ decl.mVaribale ] = mSpawnDepth;
			}
			// U6: an `own` value initialised from another `own` variable moves the
			// source. Moving in a loop (source declared outside the loop) is a
			// located error; otherwise the source is marked moved.
			if ( auto *ve = dynamic_cast<VariableExpression *>( (Expression *)decl.mInitialValue ) )
			{
				VariableDefinition *src = ve->getVariable();
				if ( src != nullptr && src->getOwnership() == OwnershipQualifier::kOwnership_Own )
				{
					int declDepth = mDeclLoopDepth.count( src ) ? mDeclLoopDepth[ src ] : 0;
					if ( mLoopDepth > declDepth )
					{
						mDiag.error( decl.mInitialValue->getLocation(),
							"move of own variable '" + src->getName() + "' inside a loop" );
						mReported = true;
					}
					else
						mMoved.insert( src );
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
		// U1: a variable read counts as a reference (suppresses the unused lint).
		if ( var != nullptr )
			mReferencedNames.insert( var->getName() );
		if ( var != nullptr && var->getOwnership() == OwnershipQualifier::kOwnership_Own )
		{
			// U6: use of a moved own value.
			if ( mMoved.count( var ) )
			{
				mDiag.error( ve->getLocation(),
					"use of moved variable '" + var->getName() + "'" );
				mReported = true;
			}
			// U6: an own value cannot be captured across a spawn boundary.
			int declSpawn = mDeclSpawnDepth.count( var ) ? mDeclSpawnDepth[ var ] : 0;
			if ( mSpawnDepth > declSpawn )
			{
				mDiag.error( ve->getLocation(),
					"cannot capture own variable '" + var->getName() +
					"' across a spawn boundary" );
				mReported = true;
			}
		}
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

		// Builtin to_json(value) requires a @json-annotated struct argument.
		// Reject a plain (non-@json) struct here with a located error in ALL
		// build modes (the codegen dispatches to the generated StructName_to_json,
		// which only exists for @json structs).
		if ( callee != nullptr && callee->isBuiltin() && callee->getName() == "to_json" )
		{
			validateToJsonArg( ce );
			return t;
		}

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

		// U6: passing an `own` variable to an `own` parameter moves it (mirrors
		// codegen's move-on-own-argument). Params were visited above, so a
		// use-after-move on the argument itself is already reported.
		if ( callee != nullptr )
		{
			int np2 = callee->getNumberParams();
			for ( int i = 0; i < (int)ce->mParams.size() && i < np2; i++ )
			{
				VariableDefinition *pd = callee->getParam( i );
				if ( pd == nullptr || pd->getOwnership() != OwnershipQualifier::kOwnership_Own )
					continue;
				if ( auto *ve = dynamic_cast<VariableExpression *>( (Expression *)ce->mParams[i] ) )
				{
					VariableDefinition *av = ve->getVariable();
					if ( av != nullptr && av->getOwnership() == OwnershipQualifier::kOwnership_Own )
						mMoved.insert( av );
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
		// U1: calling through a fn-typed variable is a use of that variable.
		if ( ic->mFnVariable != nullptr )
			mReferencedNames.insert( ic->mFnVariable->getName() );
		Type *t = nullptr;
		if ( ic->mFnVariable != nullptr )
		{
			if ( auto *ft = dynamic_cast<FunctionType *>( (Type *)ic->mFnVariable->getVariableType() ) )
				t = ft->getReturnType();
		}
		expr->setResolvedType( t );
		return t;
	}

	// Database query/insert/update/delete: validate that every referenced column
	// exists on the table struct, in ALL build modes, with a located error
	// (reject, don't coerce). The codegen retains a non-located backstop, but
	// Sema is the source of truth so query_bad_field is a fail/sema fixture.
	if ( auto *q = dynamic_cast<QueryExpression *>( expr ) )
	{
		validateTableSteps( q->mTableName, q->mSteps, q );

		// Annotate the query's value type: Array<T> for a row set, Option<T>
		// when the pipeline ends in |> first (single row or none). Codegen and
		// match-exhaustiveness read this — e.g. a match over a query-first
		// result must handle `none`, and the temp-subject payload release needs
		// the concrete T.
		bool hasFirst = false;
		for ( const auto &step : q->mSteps )
		{
			if ( step.mType == QueryPipelineStep::FIRST )
			{
				hasFirst = true;
				break;
			}
		}
		Type *resultType = new Type( hasFirst ? "Option" : "Array" );
		resultType->addTypeParam( new Type( q->mTableName ) );
		q->setResolvedType( resultType );
		return resultType;
	}
	if ( auto *u = dynamic_cast<UpdateExpression *>( expr ) )
	{
		validateTableSteps( u->mTableName, u->mSteps, u );
		return nullptr;
	}
	if ( auto *d = dynamic_cast<DeleteExpression *>( expr ) )
	{
		validateTableSteps( d->mTableName, d->mSteps, d );
		return nullptr;
	}
	if ( auto *ins = dynamic_cast<InsertExpression *>( expr ) )
	{
		StructDefinition *table = tableStructFor( ins->mTableName, ins );
		if ( table != nullptr )
		{
			for ( const auto &name : ins->mFieldNames )
				checkTableField( table, name, ins->getLocation() );
		}
		return nullptr;
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
		// String '+' is concatenation and requires BOTH operands to be string.
		// `"k" + i` (string + int) is a type error — not an implicit int→string
		// coercion (BLang is explicit-over-implicit; use interpolation `"k{i}"`).
		// Without this it reaches codegen as `add ptr, i32` → IR-verify ICE.
		if ( oper == "+" && lt != nullptr && rt != nullptr &&
		     !lt->getName().empty() && !rt->getName().empty() &&
		     !looksGenericParam( lt->getName() ) &&
		     !looksGenericParam( rt->getName() ) )
		{
			bool lStr = ( lt->getName() == "string" );
			bool rStr = ( rt->getName() == "string" );
			if ( lStr != rStr )   // exactly one operand is a string
			{
				mDiag.error( op->getLocation(),
					"operator '+' cannot be applied to 'string' and '" +
					typeName( lStr ? rt : lt ) + "' (use string interpolation)" );
				mReported = true;
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
		// U1: a write also counts as a reference (conservative — avoids flagging
		// a variable that is assigned to but, e.g., read only in codegen paths).
		if ( as->mVariable != nullptr )
			mReferencedNames.insert( as->mVariable->getName() );
		// U6: reassigning a variable clears its moved state (it holds a value again).
		if ( as->mVariable != nullptr )
			mMoved.erase( as->mVariable );
		Type *t = ( as->mVariable != nullptr ) ? as->mVariable->getVariableType() : nullptr;
		expr->setResolvedType( t );
		return t;
	}
	if ( auto *fas = dynamic_cast<FieldAssignmentExpression *>( expr ) )
	{
		visitExpr( fas->mObject );
		visitExpr( fas->mValue );
		// Concurrency safety (REQ-010): a `shared` value is immutable through its
		// fields — assigning to a field of a shared variable is a located error.
		// (`sync` values are mutated under a lock in codegen and are allowed.)
		if ( auto *ve = dynamic_cast<VariableExpression *>( (Expression *)fas->mObject ) )
		{
			VariableDefinition *v = ve->getVariable();
			if ( v != nullptr && v->getOwnership() == OwnershipQualifier::kOwnership_Shared )
			{
				mDiag.error( fas->getLocation(),
					"cannot assign to field '" + fas->mFieldName + "' through shared value '" +
					v->getName() + "'" );
				mReported = true;
			}
		}
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
		EnumDefinition *ed = enumForType( subjType );

		// Resolve each destructuring arm's binding to its variant's associated
		// type BEFORE walking the arm body. The parser can only stamp a
		// placeholder `var` type on the binding (the subject's enum is not known
		// at parse time); leaving it `var` means string/Array payloads are not
		// recognized as such downstream — e.g. `err(msg)` where msg is a string
		// would be passed to an extern `cstring` parameter without the .data
		// extraction, printing the BlangString header as garbage. Setting the
		// resolved type here (all build modes) fixes the typed AST codegen reads.
		if ( ed != nullptr )
		{
			for ( auto &arm : mt->mArms )
			{
				if ( arm.mBindingName.empty() || arm.mBody == nullptr ||
					 arm.mBody->mScope == nullptr )
					continue;
				for ( auto &v : ed->getVariants() )
				{
					if ( v.mName == arm.mPattern && !v.mAssociatedTypes.empty() )
					{
						Symbol *s = arm.mBody->mScope->findSymbol( arm.mBindingName );
						if ( auto *vd = dynamic_cast<VariableDefinition *>( s ) )
						{
							SmartPtr<Type> atsp = v.mAssociatedTypes[0];
							Type *at = (Type *)atsp;
							// For a generic enum (e.g. built-in Option<T>/Result<T,E>),
							// the variant's associated type is a generic param (T/E).
							// Recover the concrete argument from the subject's static
							// type (e.g. Result<int, string> -> err payload is string),
							// so string/Array payload methods resolve downstream.
							if ( subjType != nullptr )
							{
								const auto &gps = ed->getGenericParams();
								for ( size_t gi = 0; gi < gps.size(); gi++ )
								{
									if ( gps[gi].mName == at->getName() &&
										 (int)gi < subjType->getNumTypeParams() )
									{
										at = subjType->getTypeParam( (int)gi );
										break;
									}
								}
							}
							vd->setType( at );
						}
						break;
					}
				}
			}
		}

		for ( auto &arm : mt->mArms )
			visitStmt( arm.mBody );

		// Exhaustiveness (REQ-007): when the subject is a determinable enum and no
		// wildcard `_` arm is present, every variant must be covered by an arm.
		// Non-enum subjects (literals, `var`-inferred bindings) are left unchecked.
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
		mSpawnDepth++;
		visitStmt( sp->mBody );
		mSpawnDepth--;
		// Concurrency safety (REQ-010): a non-`own` heap value (string/Array/
		// Buffer/struct) captured by a spawn must be `shared` or `sync`; a plain
		// (value-ownership) capture would cross the spawn boundary by raw pointer
		// copy — a located error naming the variable and the fix.
		std::set<VariableDefinition *> refs, locals;
		collectSpawnRefs( sp->mBody, refs, locals );
		for ( auto *v : refs )
		{
			if ( v == nullptr || locals.count( v ) )
				continue;
			if ( v->getOwnership() != OwnershipQualifier::kOwnership_Value )
				continue;  // own handled at use-site; shared/sync are safe
			if ( isHeapType( v->getVariableType() ) )
			{
				mDiag.error( sp->getLocation(),
					"captured variable '" + v->getName() +
					"' must be declared shared or sync to cross a spawn boundary" );
				mReported = true;
			}
		}
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

void Sema::validateToJsonArg( CallExpression *call )
{
	if ( call->mParams.size() != 1 )
	{
		mDiag.error( call->getLocation(), "to_json expects exactly one argument" );
		mReported = true;
		return;
	}
	Expression *arg = call->mParams[0];

	// Resolve the argument's struct type name from the AST / Sema annotations.
	std::string typeName;
	if ( auto *slit = dynamic_cast<StructLiteralExpression *>( arg ) )
		typeName = slit->mTypeName;
	else if ( auto *ve = dynamic_cast<VariableExpression *>( arg ) )
	{
		if ( ve->getVariable() != nullptr &&
			 ve->getVariable()->getVariableType() != nullptr )
			typeName = ve->getVariable()->getVariableType()->getName();
	}
	else if ( Type *rt = arg->getResolvedType() )
		typeName = rt->getName();

	// Only flag when we can POSITIVELY resolve the argument to a concrete struct
	// in scope. If the type is not determinable here (e.g. a for-in loop variable
	// whose element type is only fixed at codegen), leave it unchecked — codegen
	// dispatches on the concrete type and errors if no serializer exists. This
	// mirrors Sema's member-resolution rule (never fabricate an error on an
	// indeterminate base).
	StructDefinition *sd =
		dynamic_cast<StructDefinition *>( mScope->findSymbol( typeName ) );
	if ( sd == nullptr )
		return;
	bool hasJson = false;
	for ( const auto &ann : sd->getAnnotations() )
		if ( ann.mName == "json" )
		{
			hasJson = true;
			break;
		}
	if ( !hasJson )
	{
		mDiag.error( arg->getLocation(),
			"to_json requires a @json-annotated struct, but '" + typeName +
			"' is not annotated @json" );
		mReported = true;
	}
}

StructDefinition *Sema::tableStructFor( const std::string &tableName, Expression *node )
{
	StructDefinition *sd =
		dynamic_cast<StructDefinition *>( mScope->findSymbol( tableName ) );
	if ( sd == nullptr )
	{
		mDiag.error( node->getLocation(), "unknown table '" + tableName + "'" );
		mReported = true;
		return nullptr;
	}
	if ( !sd->isTable() )
	{
		mDiag.error( node->getLocation(),
			"'" + tableName + "' is not a table struct (use `table struct`)" );
		mReported = true;
		return nullptr;
	}
	return sd;
}

void Sema::checkTableField( StructDefinition *table, const std::string &field,
	const SourceLocation &loc )
{
	for ( const auto &f : table->getFields() )
		if ( f->getName() == field )
			return;
	mDiag.error( loc,
		"table '" + table->getName() + "' has no field '" + field + "'" );
	mReported = true;
}

void Sema::collectQueryFieldExprs( const Expression *e,
	std::vector<const QueryFieldExpression *> &out )
{
	if ( e == nullptr )
		return;
	if ( auto *qf = dynamic_cast<const QueryFieldExpression *>( e ) )
	{
		out.push_back( qf );
		return;
	}
	if ( auto *ops = dynamic_cast<const OperationsExpression *>( e ) )
	{
		collectQueryFieldExprs( (const Expression *)ops->mOp1, out );
		collectQueryFieldExprs( (const Expression *)ops->mOp2, out );
	}
}

void Sema::validateTableSteps( const std::string &tableName,
	const std::vector<QueryPipelineStep> &steps, Expression *node )
{
	StructDefinition *table = tableStructFor( tableName, node );
	if ( table == nullptr )
		return;

	// A JOIN references a second table; skip field validation once one is
	// present to avoid false positives on the joined table's columns (matches
	// the codegen backstop).
	for ( const auto &step : steps )
		if ( step.mType == QueryPipelineStep::JOIN )
			return;

	for ( const auto &step : steps )
	{
		if ( step.mType == QueryPipelineStep::SET )
		{
			for ( const auto &sf : step.mSetFields )
				checkTableField( table, sf.first, node->getLocation() );
		}
		else
		{
			std::vector<const QueryFieldExpression *> refs;
			collectQueryFieldExprs( (const Expression *)step.mExpression, refs );
			for ( auto *r : refs )
				checkTableField( table, r->getFieldName(), r->getLocation() );
		}
	}
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
	// Channel methods: chan<T> is a built-in generic, not a StructDefinition,
	// so structForType returns null and the struct path below would leave the
	// call unannotated. recv() yields the built-in Option<T>; annotating it here
	// is load-bearing in ALL build modes: (a) match-exhaustiveness over
	// `match ch.recv()` fires (a match that ignores the closed/empty `none` case
	// is rejected), and (b) codegen recovers the concrete payload type for the
	// erased-payload scope release (so a refcounted recv payload is not leaked).
	// send()/close() are void and need no annotation.
	if ( baseType != nullptr && baseType->getName() == "chan" )
	{
		if ( mc->mMethodName == "recv" && mc->mArgs.empty() )
		{
			Type *opt = new Type( "Option" );
			if ( baseType->getNumTypeParams() > 0 )
				opt->addTypeParam( baseType->getTypeParam( 0 ) );
			mc->setResolvedType( opt );
		}
		return;
	}

	StructDefinition *structDef = structForType( baseType );
	if ( structDef == nullptr )
		return;

	const string &name = mc->mMethodName;

	for ( auto &method : structDef->mMethods )
	{
		if ( method->getName() == name )
		{
			Type *rt = method->getReturnType();
			if ( rt != nullptr )
			{
				if ( isGenericParamName( structDef, rt->getName() ) )
				{
					// B1: a generic return type (e.g. `V`) is substituted with the
					// concrete type argument from the base type (e.g.
					// Map<string,Point>.get -> Point), so a method-chain field
					// access `m.get(k).field` can resolve the concrete struct type.
					// Without this the call is left unannotated and codegen's
					// field-access fallback finds no struct type (reads empty).
					const auto &gps = structDef->getGenericParams();
					for ( size_t gi = 0; gi < gps.size(); gi++ )
					{
						if ( gps[gi].mName == rt->getName() && baseType != nullptr &&
						     (int)gi < baseType->getNumTypeParams() )
						{
							mc->setResolvedType( baseType->getTypeParam( gi ) );
							break;
						}
					}
				}
				else
				{
					mc->setResolvedType( rt );
				}
			}
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

// ---------------------------------------------------------------------------
// U7: spawn-capture helpers
// ---------------------------------------------------------------------------

bool Sema::isHeapType( Type *t )
{
	if ( t == nullptr )
		return false;
	const string &n = t->getName();
	if ( n == "string" || n == "Array" || n == "Buffer" )
		return true;
	return dynamic_cast<StructDefinition *>( mScope->findSymbol( n ) ) != nullptr;
}

// Collect variables referenced in a spawn body (refs) and variables declared
// inside it (locals). A captured variable is one referenced but not local. The
// walk is conservative: unrecognized nodes are simply not descended (a missed
// reference yields a false negative, never a false positive).
void Sema::collectSpawnRefs( Statement *stmt,
	std::set<VariableDefinition *> &refs, std::set<VariableDefinition *> &locals )
{
	if ( stmt == nullptr )
		return;

	if ( auto *ve = dynamic_cast<VariableExpression *>( stmt ) )
	{
		if ( ve->getVariable() != nullptr )
			refs.insert( ve->getVariable() );
		return;
	}
	if ( auto *vd = dynamic_cast<VariableDeclaration *>( stmt ) )
	{
		for ( auto &decl : vd->mVariables )
		{
			if ( decl.mVaribale != nullptr )
				locals.insert( decl.mVaribale );
			collectSpawnRefs( decl.mInitialValue, refs, locals );
		}
		return;
	}
	if ( auto *ae = dynamic_cast<AssignmentExpression *>( stmt ) )
	{
		if ( ae->mVariable != nullptr )
			refs.insert( ae->mVariable );
		collectSpawnRefs( ae->mValue, refs, locals );
		return;
	}
	if ( auto *ic = dynamic_cast<IndirectCallExpression *>( stmt ) )
	{
		if ( ic->mFnVariable != nullptr )
			refs.insert( ic->mFnVariable );
		for ( auto &p : ic->mParams )
			collectSpawnRefs( p, refs, locals );
		return;
	}
	if ( auto *b = dynamic_cast<Block *>( stmt ) )
	{
		for ( auto &c : b->mStatementList )
			collectSpawnRefs( c, refs, locals );
		return;
	}
	if ( auto *i = dynamic_cast<IfStatement *>( stmt ) )
	{
		collectSpawnRefs( i->mIfExpression, refs, locals );
		collectSpawnRefs( i->mStatement, refs, locals );
		collectSpawnRefs( i->mElseStatement, refs, locals );
		return;
	}
	if ( auto *w = dynamic_cast<WhileStatement *>( stmt ) )
	{
		collectSpawnRefs( w->mLoopExpression, refs, locals );
		collectSpawnRefs( w->mLoopStatement, refs, locals );
		return;
	}
	if ( auto *f = dynamic_cast<ForInStatement *>( stmt ) )
	{
		collectSpawnRefs( f->mIterableExpression, refs, locals );
		collectSpawnRefs( f->mBody, refs, locals );
		return;
	}
	if ( auto *r = dynamic_cast<ReturnStatement *>( stmt ) )
	{
		collectSpawnRefs( r->mExpression, refs, locals );
		return;
	}
	if ( auto *call = dynamic_cast<CallExpression *>( stmt ) )
	{
		for ( auto &p : call->mParams )
			collectSpawnRefs( p, refs, locals );
		return;
	}
	if ( auto *mc = dynamic_cast<MethodCallExpression *>( stmt ) )
	{
		collectSpawnRefs( mc->mObject, refs, locals );
		for ( auto &a : mc->mArgs )
			collectSpawnRefs( a, refs, locals );
		return;
	}
	if ( auto *fa = dynamic_cast<FieldAccessExpression *>( stmt ) )
	{
		collectSpawnRefs( fa->mObject, refs, locals );
		return;
	}
	if ( auto *fas = dynamic_cast<FieldAssignmentExpression *>( stmt ) )
	{
		collectSpawnRefs( fas->mObject, refs, locals );
		collectSpawnRefs( fas->mValue, refs, locals );
		return;
	}
	if ( auto *ie = dynamic_cast<IndexExpression *>( stmt ) )
	{
		collectSpawnRefs( ie->getObject(), refs, locals );
		collectSpawnRefs( ie->getIndex(), refs, locals );
		return;
	}
	if ( auto *ias = dynamic_cast<IndexAssignmentExpression *>( stmt ) )
	{
		collectSpawnRefs( ias->mObject, refs, locals );
		collectSpawnRefs( ias->mIndex, refs, locals );
		collectSpawnRefs( ias->mValue, refs, locals );
		return;
	}
	if ( auto *op = dynamic_cast<OperationsExpression *>( stmt ) )
	{
		collectSpawnRefs( op->mOp1, refs, locals );
		collectSpawnRefs( op->mOp2, refs, locals );
		return;
	}
	if ( auto *un = dynamic_cast<UnaryExpression *>( stmt ) )
	{
		collectSpawnRefs( un->mOperand, refs, locals );
		return;
	}
	if ( auto *al = dynamic_cast<ArrayLiteralExpression *>( stmt ) )
	{
		for ( auto &el : al->mElements )
			collectSpawnRefs( el, refs, locals );
		return;
	}
	if ( auto *si = dynamic_cast<StringInterpolation *>( stmt ) )
	{
		for ( auto &p : si->mParts )
			collectSpawnRefs( p, refs, locals );
		return;
	}
	if ( auto *cons = dynamic_cast<ConstructExpression *>( stmt ) )
	{
		for ( auto &a : cons->mArgs )
			collectSpawnRefs( a, refs, locals );
		return;
	}
	if ( auto *asx = dynamic_cast<AssertStatement *>( stmt ) )
	{
		collectSpawnRefs( asx->mExpression, refs, locals );
		return;
	}
}
