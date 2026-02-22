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

FunctionDefinition *FunctionDefinition::Parse( Lexer &l, Scope *s )
{
	FunctionDefinition *func;

	// Check for extern modifier
	bool isExtern = false;
	if ( l.peekSymbol() == Lexer::TYPE_MODIFIER )
	{
		string modText = l.getSymbolText();
		if ( modText == "extern" )
			isExtern = true;
		// Type::Parse expects to consume the type token itself,
		// but we already consumed the modifier. For extern, the modifier
		// is consumed here and Type::Parse will get the return type directly.
		// However, Type::Parse calls getSymbol() which will get the next token.
		// Since we consumed "extern", the next token is the return type. Good.
	}

	SmartPtr<Type> retType = Type::Parse( l, s, true );
	int sym = l.getSymbol();
	if ( sym == Lexer::SYMBOL )
	{
		func = new FunctionDefinition( l.getSymbolText() );
		func->mReturnType = retType;
		func->mIsExtern = isExtern;
		func->mFuncScope = new Scope( Scope::kScope_Function, l.getSymbolText() );
		func->mFuncScope->setParent( s );
		s->addSymbol( func );
	}
	else
	{
		// report error
		COMPILE_ERROR( l, "Failed to parse function name" );
	}

	sym = l.getSymbol();
	if ( sym != '(' )
	{
		// report error
		COMPILE_ERROR( l, "Failed to find function name" );
	}

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
		{
			// report error
			COMPILE_ERROR( l, "expected \',\' or \')\'" );
		}
	}
	else
		l.getSymbol();

	// Extern declarations end with ';', regular functions have a body
	if ( isExtern )
	{
		sym = l.getSymbol();
		if ( sym != ';' )
			COMPILE_ERROR( l, "Expected \';\' after extern function declaration" );
		cout << "Completed extern declaration " << endl;
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

