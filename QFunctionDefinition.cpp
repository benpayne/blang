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
		func->mReturnType = retType;
	}
	else
	{
		// No arrow means void return type
		func->mReturnType = nullptr;
	}

	// Parse optional requires/ensures contract clauses
	while ( l.peekSymbol() == Lexer::KEYWORD_REQUIRES || l.peekSymbol() == Lexer::KEYWORD_ENSURES )
	{
		bool isRequires = ( l.peekSymbol() == Lexer::KEYWORD_REQUIRES );
		l.getSymbol(); // consume 'requires' or 'ensures'

		// Parse the contract expression as text until we hit another keyword or '{'
		// For now, collect tokens until we see requires, ensures, '{', or ';'
		string clauseText;
		int depth = 0;
		while ( true )
		{
			int nextSym = l.peekSymbol();
			if ( depth == 0 &&
				 ( nextSym == Lexer::KEYWORD_REQUIRES ||
				   nextSym == Lexer::KEYWORD_ENSURES ||
				   nextSym == '{' || nextSym == ';' ) )
				break;

			l.getSymbol();
			if ( nextSym == '(' ) depth++;
			else if ( nextSym == ')' ) depth--;

			if ( !clauseText.empty() ) clauseText += " ";
			clauseText += l.getSymbolText();
		}

		if ( clauseText.empty() )
			COMPILE_ERROR( l, "Expected expression after '" + string( isRequires ? "requires" : "ensures" ) + "'" );

		if ( isRequires )
			func->mRequiresClauses.push_back( clauseText );
		else
			func->mEnsuresClauses.push_back( clauseText );
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
