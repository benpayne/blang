#include "AstLocator.h"

namespace QLang
{

const Statement *AstLocator::locate( const Module *mod, uint32_t line, uint32_t col )
{
	AstLocator locator( line, col );
	locator.visitModule( mod );
	return locator.mBest;
}

void AstLocator::consider( const Statement *stmt )
{
	const SourceLocation &loc = stmt->getLocation();
	if ( loc.line != mLine || loc.col > mCol )
		return;
	// Later-visited wins ties: children are visited after parents, so at the
	// same column the innermost node ends up selected.
	if ( mBest == nullptr || loc.col >= mBest->getLocation().col )
		mBest = stmt;
}

void AstLocator::visitModule( const Module *mod )
{
	for ( const auto &s : mod->mStructList )
		for ( const auto &method : s->mMethods )
			visitFunction( method );
	for ( const auto &f : mod->mFunctionList )
		visitFunction( f );
	for ( const auto &t : mod->mTestBlocks )
		visitStatement( t->mBody );
}

void AstLocator::visitFunction( const FunctionDefinition *func )
{
	for ( const auto &req : func->mRequiresClauses )
		visitStatement( req );
	for ( const auto &ens : func->mEnsuresClauses )
		visitStatement( ens );
	if ( func->mFuncBody != nullptr )
		visitStatement( func->mFuncBody );
}

// Same dispatch shape as LocationDumper::visitStatement: every node is
// considered, then its children are visited (children after parents, so
// same-column ties resolve to the innermost node).
void AstLocator::visitStatement( const Statement *stmt )
{
	if ( stmt == nullptr )
		return;

	consider( stmt );

	if ( const auto *e = dynamic_cast<const OperationsExpression *>( stmt ) )
	{
		visitStatement( e->mOp1 );
		visitStatement( e->mOp2 );
	}
	else if ( const auto *e = dynamic_cast<const UnaryExpression *>( stmt ) )
	{
		visitStatement( e->mOperand );
	}
	else if ( const auto *e = dynamic_cast<const AssignmentExpression *>( stmt ) )
	{
		visitStatement( e->mValue );
	}
	else if ( const auto *e = dynamic_cast<const FieldAssignmentExpression *>( stmt ) )
	{
		visitStatement( e->mObject );
		visitStatement( e->mValue );
	}
	else if ( const auto *e = dynamic_cast<const IndexAssignmentExpression *>( stmt ) )
	{
		visitStatement( e->mObject );
		visitStatement( e->mIndex );
		visitStatement( e->mValue );
	}
	else if ( const auto *e = dynamic_cast<const CallExpression *>( stmt ) )
	{
		for ( const auto &p : e->mParams )
			visitStatement( p );
	}
	else if ( const auto *e = dynamic_cast<const MethodCallExpression *>( stmt ) )
	{
		visitStatement( e->mObject );
		for ( const auto &a : e->mArgs )
			visitStatement( a );
	}
	else if ( const auto *e = dynamic_cast<const FieldAccessExpression *>( stmt ) )
	{
		visitStatement( e->mObject );
	}
	else if ( const auto *e = dynamic_cast<const IndexExpression *>( stmt ) )
	{
		visitStatement( e->mObject );
		visitStatement( e->mIndex );
	}
	else if ( const auto *e = dynamic_cast<const ArrayLiteralExpression *>( stmt ) )
	{
		for ( const auto &el : e->mElements )
			visitStatement( el );
	}
	else if ( const auto *e = dynamic_cast<const RangeExpression *>( stmt ) )
	{
		visitStatement( e->mStart );
		visitStatement( e->mEnd );
	}
	else if ( const auto *e = dynamic_cast<const StringInterpolation *>( stmt ) )
	{
		for ( const auto &part : e->mParts )
			visitStatement( part );
	}
	else if ( const auto *e = dynamic_cast<const StructLiteralExpression *>( stmt ) )
	{
		for ( const auto &v : e->mFieldValues )
			visitStatement( v );
	}
	else if ( const auto *e = dynamic_cast<const ConstructExpression *>( stmt ) )
	{
		for ( const auto &a : e->mArgs )
			visitStatement( a );
	}
	else if ( const auto *e = dynamic_cast<const EnumConstructExpression *>( stmt ) )
	{
		for ( const auto &a : e->mArgs )
			visitStatement( a );
	}
	else if ( const auto *e = dynamic_cast<const TryExpression *>( stmt ) )
	{
		visitStatement( e->mOperand );
	}
	else if ( const auto *e = dynamic_cast<const AwaitExpression *>( stmt ) )
	{
		visitStatement( e->mOperand );
	}
	else if ( const auto *e = dynamic_cast<const IndirectCallExpression *>( stmt ) )
	{
		for ( const auto &p : e->mParams )
			visitStatement( p );
	}
	else if ( const auto *e = dynamic_cast<const PipelineExpression *>( stmt ) )
	{
		visitStatement( e->mInput );
		visitStatement( e->mTransform );
	}
	else if ( const auto *e = dynamic_cast<const LambdaExpression *>( stmt ) )
	{
		if ( e->mBody != nullptr )
			visitStatement( e->mBody );
	}
	else if ( const auto *e = dynamic_cast<const SpawnStatement *>( stmt ) )
	{
		if ( e->mBody != nullptr )
			visitStatement( e->mBody );
	}
	else if ( const auto *e = dynamic_cast<const MatchExpression *>( stmt ) )
	{
		visitStatement( e->mSubject );
		for ( const auto &arm : e->mArms )
		{
			if ( arm.mBody != nullptr )
				visitStatement( arm.mBody );
		}
	}
	else if ( const auto *s = dynamic_cast<const Block *>( stmt ) )
	{
		for ( const auto &child : s->mStatementList )
			visitStatement( child );
	}
	else if ( const auto *s = dynamic_cast<const ReturnStatement *>( stmt ) )
	{
		if ( s->mExpression != nullptr )
			visitStatement( s->mExpression );
	}
	else if ( const auto *s = dynamic_cast<const IfStatement *>( stmt ) )
	{
		visitStatement( s->mIfExpression );
		visitStatement( s->mStatement );
		if ( s->mElseStatement != nullptr )
			visitStatement( s->mElseStatement );
	}
	else if ( const auto *s = dynamic_cast<const WhileStatement *>( stmt ) )
	{
		visitStatement( s->mLoopExpression );
		visitStatement( s->mLoopStatement );
	}
	else if ( const auto *s = dynamic_cast<const ForInStatement *>( stmt ) )
	{
		if ( s->mIterableExpression != nullptr )
			visitStatement( s->mIterableExpression );
		if ( s->mBody != nullptr )
			visitStatement( s->mBody );
	}
	else if ( const auto *s = dynamic_cast<const VariableDeclaration *>( stmt ) )
	{
		for ( const auto &decl : s->mVariables )
		{
			if ( decl.mInitialValue != nullptr )
				visitStatement( decl.mInitialValue );
		}
	}
	else if ( const auto *s = dynamic_cast<const AssertStatement *>( stmt ) )
	{
		if ( s->mExpression != nullptr )
			visitStatement( s->mExpression );
	}
	else if ( const auto *s = dynamic_cast<const WaitStatement *>( stmt ) )
	{
		if ( s->mExpr != nullptr )
			visitStatement( s->mExpr );
	}
	else if ( const auto *s = dynamic_cast<const EventHandler *>( stmt ) )
	{
		if ( s->mEventExpression != nullptr )
			visitStatement( s->mEventExpression );
		if ( s->mBody != nullptr )
			visitStatement( s->mBody );
	}
}

// A usable definition site: resolved pointer AND a real source location
// (builtins like Option/Result/print are compiler-built at 0:0).
static bool located( const SourceLocation &loc, SourceLocation &out )
{
	if ( !loc.isSet() )
		return false;
	out = loc;
	return true;
}

bool AstLocator::definitionLocation( const Statement *node, SourceLocation &out )
{
	if ( node == nullptr )
		return false;

	if ( const auto *e = dynamic_cast<const VariableExpression *>( node ) )
	{
		const VariableDefinition *def = e->mVariable;
		return def != nullptr && located( def->getLocation(), out );
	}
	if ( const auto *e = dynamic_cast<const CallExpression *>( node ) )
	{
		const FunctionDefinition *def = e->mFunction;
		return def != nullptr && located( def->getLocation(), out );
	}
	if ( const auto *e = dynamic_cast<const ConstructExpression *>( node ) )
	{
		const StructDefinition *def = e->mStructDef;
		return def != nullptr && located( def->getLocation(), out );
	}
	if ( const auto *e = dynamic_cast<const EnumConstructExpression *>( node ) )
	{
		const EnumDefinition *def = e->mEnumDef;
		if ( def == nullptr )
			return false;
		// Prefer the variant's own location (stamped since U0c).
		const auto &variants = def->getVariants();
		if ( e->mVariantIndex >= 0 && e->mVariantIndex < (int)variants.size() &&
		     located( variants[ e->mVariantIndex ].mLocation, out ) )
			return true;
		return located( def->getLocation(), out );
	}
	if ( const auto *e = dynamic_cast<const FunctionRefExpression *>( node ) )
	{
		const FunctionDefinition *def = e->mFunction;
		return def != nullptr && located( def->getLocation(), out );
	}
	if ( const auto *e = dynamic_cast<const IndirectCallExpression *>( node ) )
	{
		const VariableDefinition *def = e->mFnVariable;
		return def != nullptr && located( def->getLocation(), out );
	}
	if ( const auto *e = dynamic_cast<const MethodCallExpression *>( node ) )
	{
		const FunctionDefinition *def = e->mResolvedMethod;
		return def != nullptr && located( def->getLocation(), out );
	}
	if ( const auto *e = dynamic_cast<const FieldAccessExpression *>( node ) )
	{
		const VariableDefinition *def = e->mResolvedField;
		return def != nullptr && located( def->getLocation(), out );
	}
	return false;
}

} // namespace QLang
