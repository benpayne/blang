#include "LocationDumper.h"

#include <cxxabi.h>
#include <cstdlib>
#include <string>
#include <typeinfo>

using namespace QLang;

// Demangle a typeid name and strip a leading "QLang::" namespace qualifier
// so the printed kind is the bare AST class name (e.g. FunctionDefinition,
// ReturnStatement, ConstInteger). Stable on the Itanium C++ ABI (the
// project's Linux/macOS toolchains).
static std::string nodeKind( const std::type_info &ti )
{
	int status = 0;
	char *demangled = abi::__cxa_demangle( ti.name(), nullptr, nullptr, &status );
	std::string name = ( status == 0 && demangled != nullptr ) ? demangled : ti.name();
	if ( demangled != nullptr )
		free( demangled );

	const std::string prefix = "QLang::";
	if ( name.compare( 0, prefix.size(), prefix ) == 0 )
		name = name.substr( prefix.size() );
	return name;
}

void LocationDumper::dump( const Module *mod, std::ostream &out )
{
	LocationDumper dumper( out );
	dumper.visitModule( mod );
}

void LocationDumper::emit( const SourceLocation &loc, const char *kind )
{
	mOut << loc.file << ":" << loc.line << ":" << loc.col << " " << kind << "\n";
}

void LocationDumper::visitModule( const Module *mod )
{
	// Deterministic pre-order across the module's top-level declarations.
	// The parser stores each declaration kind in its own list, so a fixed
	// category order (imports, structs, enums, protocols, functions, tests)
	// gives a stable dump; within each list, order is source order.
	for ( const auto &imp : mod->mImports )
		visitImport( imp );
	for ( const auto &s : mod->mStructList )
		visitStruct( s );
	for ( const auto &e : mod->mEnumList )
		visitEnum( e );
	for ( const auto &p : mod->mProtocolList )
		visitProtocol( p );
	for ( const auto &f : mod->mFunctionList )
		visitFunction( f );
	for ( const auto &t : mod->mTestBlocks )
		visitTestBlock( t );
}

void LocationDumper::visitImport( const ImportStatement *imp )
{
	emit( imp->getLocation(), "ImportStatement" );
}

void LocationDumper::visitFunction( const FunctionDefinition *func )
{
	emit( func->getLocation(), nodeKind( typeid( *func ) ).c_str() );

	for ( const auto &param : func->mParameters )
		visitVariableDef( param );
	for ( const auto &req : func->mRequiresClauses )
		visitStatement( req );
	for ( const auto &ens : func->mEnsuresClauses )
		visitStatement( ens );
	if ( func->mFuncBody != nullptr )
		visitStatement( func->mFuncBody );
}

void LocationDumper::visitVariableDef( const VariableDefinition *var )
{
	emit( var->getLocation(), nodeKind( typeid( *var ) ).c_str() );
}

void LocationDumper::visitStruct( const StructDefinition *structDef )
{
	emit( structDef->getLocation(), nodeKind( typeid( *structDef ) ).c_str() );
	for ( const auto &field : structDef->mFields )
		visitVariableDef( field );
	for ( const auto &method : structDef->mMethods )
		visitFunction( method );
}

void LocationDumper::visitEnum( const EnumDefinition *enumDef )
{
	emit( enumDef->getLocation(), nodeKind( typeid( *enumDef ) ).c_str() );
}

void LocationDumper::visitProtocol( const ProtocolDefinition *protoDef )
{
	emit( protoDef->getLocation(), nodeKind( typeid( *protoDef ) ).c_str() );
	for ( const auto &method : protoDef->mRequiredMethods )
		visitFunction( method );
}

void LocationDumper::visitTestBlock( const TestBlock *test )
{
	emit( test->getLocation(), "TestBlock" );
	if ( test->mBody != nullptr )
		visitStatement( test->mBody );
}

// Dispatch a Statement (or Expression — Expression derives from Statement)
// to the correct child traversal. Every node is printed with its dynamic
// class name; a node kind not explicitly handled still prints one line and
// simply stops recursing (spec R6). Order of dynamic_casts: most-derived
// expression kinds, then statements.
void LocationDumper::visitStatement( const Statement *stmt )
{
	if ( stmt == nullptr )
		return;

	emit( stmt->getLocation(), nodeKind( typeid( *stmt ) ).c_str() );

	// --- Expressions ---
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
		for ( const auto &param : e->mParameters )
			visitVariableDef( param );
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
	// --- Statements ---
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
			if ( decl.mVaribale != nullptr )
				visitVariableDef( decl.mVaribale );
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
	// Leaf nodes (ConstInteger, VariableExpression, BreakStatement, etc.)
	// print their line above and need no further recursion.
}
