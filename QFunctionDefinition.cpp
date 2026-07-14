#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

using namespace QLang;
using namespace std;


std::ostream &QLang::operator<<(std::ostream &out, const FunctionDefinition &func)
{
	if ( func.mReturnType != nullptr )
		out << *(func.mReturnType) << " " << func.getName();
	else
		out << "void " << func.getName();

	if ( func.mParameters.size() == 0 )
		out << "()";
	else
	{
		out << "( " << func.mParameters[ 0 ];
		for ( int i = 1; i < func.mParameters.size(); i++ )
		{
			out << ", " << func.mParameters[ i ];
		}
		out << " )";
	}

	return out;
}

FunctionDefinition *FunctionDefinition::Parse( Lexer &l, Scope *s, bool isExtern, bool isPublic )
{
	FunctionDefinition *func;

	// All function declarations use the fn keyword:
	//   fn name( params ) -> returnType { body }
	//   fn name( params ) { body }   (void return)
	//   extern fn name( params ) -> returnType;
	//
	// When isExtern is true, the 'extern' keyword has already been consumed
	// by Module::Parse.
	// When isPublic is true, the 'pub' keyword has already been consumed
	// by Module::Parse.

	// Check for async modifier: async fn name(...) { }
	bool isAsync = false;
	if ( l.peekSymbol() == Lexer::KEYWORD_ASYNC )
	{
		l.getSymbol(); // consume 'async'
		isAsync = true;
	}

	if ( l.peekSymbol() != Lexer::KEYWORD_FN )
		COMPILE_ERROR( l, "Expected 'fn' keyword" );

	l.getSymbol(); // consume 'fn'

	// Parse function name
	int sym = l.getSymbol();
	if ( sym != Lexer::SYMBOL )
		COMPILE_ERROR( l, "Expected function name after 'fn'" );

	func = new FunctionDefinition( l.getSymbolText() );
	func->mIsExtern = isExtern;
	func->mIsPublic = isPublic;
	func->mIsAsync = isAsync;
	func->mFuncScope = new Scope( Scope::kScope_Function, l.getSymbolText() );
	func->mFuncScope->setParent( s );
	if ( !s->addSymbol( func ) )
		COMPILE_ERROR( l, "Duplicate function definition: '" + func->getName() + "'" );

	// Check for generic parameters: fn name<T> or fn name<T: Constraint>
	if ( l.peekSymbol() == '<' )
	{
		l.getSymbol(); // consume '<'

		do {
			sym = l.getSymbol();
			if ( sym != Lexer::SYMBOL )
				COMPILE_ERROR( l, "Expected type parameter name" );

			GenericParam param;
			param.mName = l.getSymbolText();

			// Check for duplicate generic parameter names
			for ( const GenericParam &existing : func->mGenericParams )
			{
				if ( existing.mName == param.mName )
					COMPILE_ERROR( l, "Duplicate generic parameter name: '" + param.mName + "'" );
			}

			// Check for constraint: <T: Comparable>
			if ( l.peekSymbol() == ':' )
			{
				l.getSymbol(); // consume ':'
				sym = l.getSymbol();
				if ( sym != Lexer::SYMBOL )
					COMPILE_ERROR( l, "Expected constraint name after ':'" );
				param.mConstraint = l.getSymbolText();

				// Validate that the constraint names an existing protocol
				Symbol *constraintSym = s->findSymbol( param.mConstraint );
				if ( constraintSym == nullptr )
					COMPILE_ERROR( l, "Unknown protocol constraint: '" + param.mConstraint + "'" );
				if ( dynamic_cast<ProtocolDefinition *>( constraintSym ) == nullptr )
					COMPILE_ERROR( l, "Constraint '" + param.mConstraint + "' is not a protocol" );
			}

			func->mGenericParams.push_back( param );

			// Register type parameter in scope so it can be used as a type
			s->addType( new Type( param.mName ) );

			sym = l.getSymbol();
		} while ( sym == ',' );

		if ( sym != '>' )
			COMPILE_ERROR( l, "Expected '>' after generic parameters" );
	}

	// Parse '(' params ')'
	sym = l.getSymbol();
	if ( sym != '(' )
		COMPILE_ERROR( l, "Expected '(' after function name" );

	sym = l.peekSymbol();
	if ( sym != ')' )
	{
		int paramIndex = 0;
		do {
			// Check for ... (variadic)
			if ( l.peekSymbol() == Lexer::ELLIPSIS )
			{
				l.getSymbol(); // consume ...
				func->mIsVariadic = true;
				sym = l.getSymbol(); // should be ')'
				break;
			}

			VariableDefinition *def = VariableDefinition::ParseFuncParam( l, func->mFuncScope, isExtern, paramIndex );
			func->mParameters.push_back( def );
			paramIndex++;
			sym = l.getSymbol();
		} while ( sym == ',' );

		if ( sym != ')' )
			COMPILE_ERROR( l, "expected ',' or ')'" );
	}
	else
		l.getSymbol(); // consume ')'

	// Parse optional '->' return type; default to void
	sym = l.peekSymbol();
	if ( sym == Lexer::ARROW )
	{
		l.getSymbol(); // consume '->'

		// Parse the return type
		SmartPtr<Type> retType = Type::Parse( l, s, false );

		// cstring and carray types are only allowed in extern fn declarations
		if ( !isExtern && retType != nullptr && ( retType->getName() == "cstring" || retType->getName() == "carray" ) )
			COMPILE_ERROR( l, "'" + retType->getName() + "' type can only be used in extern fn declarations" );

		func->mReturnType = retType;
	}
	else
	{
		// No arrow means void return type
		func->mReturnType = nullptr;
	}

	// Parse optional requires/ensures contract clauses as expression ASTs
	while ( l.peekSymbol() == Lexer::KEYWORD_REQUIRES || l.peekSymbol() == Lexer::KEYWORD_ENSURES )
	{
		bool isRequires = ( l.peekSymbol() == Lexer::KEYWORD_REQUIRES );
		l.getSymbol(); // consume 'requires' or 'ensures'

		// For ensures clauses, add 'result' variable to the function scope
		// so the expression can reference the return value
		if ( !isRequires && func->mReturnType != nullptr )
		{
			if ( func->mFuncScope->findSymbol( "result" ) == nullptr )
			{
				VariableDefinition *resultVar = new VariableDefinition( func->mReturnType, "result" );
				func->mFuncScope->addSymbol( resultVar );
			}
		}

		// Parse the contract expression in the function's scope
		Expression *clauseExpr = Expression::ParseExpr( l, func->mFuncScope );
		if ( clauseExpr == nullptr )
			COMPILE_ERROR( l, "Expected expression after '" + string( isRequires ? "requires" : "ensures" ) + "'" );

		if ( isRequires )
			func->mRequiresClauses.push_back( clauseExpr );
		else
			func->mEnsuresClauses.push_back( clauseExpr );
	}

	// Extern declarations end with ';', regular functions have a body
	if ( isExtern )
	{
		sym = l.getSymbol();
		if ( sym != ';' )
			COMPILE_ERROR( l, "Expected ';' after extern function declaration" );
		func->mFuncBody = nullptr;
		cout << "Completed extern declaration " << endl;
	}
	else if ( l.peekSymbol() == ';' )
	{
		// Bodyless declaration (e.g. protocol methods)
		l.getSymbol(); // consume ';'
		func->mFuncBody = nullptr;
		cout << "Completed function declaration " << endl;
	}
	else
	{
		func->mFuncBody = Block::Parse( l, func->mFuncScope );
		cout << "Completed function " << endl;
	}

	return func;
}


Type *FunctionDefinition::getParamType( int p )
{
	return mParameters[ p ]->getVariableType();
}

VariableDefinition *FunctionDefinition::getParam( int p )
{
	return mParameters[ p ];
}

FunctionDefinition *FunctionDefinition::ParseInit( Lexer &l, Scope *s )
{
	// 'init' keyword has already been consumed by the caller.
	// Parse: init(params) { body }
	// Creates a method named "init" with implicit self parameter.

	FunctionDefinition *func = new FunctionDefinition( "init" );
	func->mIsInit = true;
	func->mFuncScope = new Scope( Scope::kScope_Function, "init" );
	func->mFuncScope->setParent( s );

	// Add implicit self parameter
	Type *selfType = new Type( "self" );
	VariableDefinition *selfParam = new VariableDefinition( selfType, "self" );
	func->mParameters.push_back( selfParam );
	func->mFuncScope->addSymbol( selfParam );

	// Parse parameter list: init(int x, int y)
	int sym = l.getSymbol();
	if ( sym != '(' )
		COMPILE_ERROR( l, "Expected '(' after 'init'" );

	sym = l.peekSymbol();
	if ( sym != ')' )
	{
		int paramIndex = 0;
		do {
			VariableDefinition *param = VariableDefinition::ParseFuncParam( l, func->mFuncScope );
			func->mParameters.push_back( param );
			func->mFuncScope->addSymbol( param );
			paramIndex++;
			sym = l.getSymbol();
		} while ( sym == ',' );
		if ( sym != ')' )
			COMPILE_ERROR( l, "Expected ')' after init parameters" );
	}
	else
	{
		l.getSymbol(); // consume ')'
	}

	// No return type — init is void (caller allocates and passes self)
	func->mReturnType = nullptr;

	// Parse body
	func->mFuncBody = Block::Parse( l, func->mFuncScope );

	cout << "Completed init constructor" << endl;
	return func;
}
