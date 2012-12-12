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
	if ( func.mReturnType != NULL )
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
	SmartPtr<Type> retType = Type::Parse( l, s, true );
	int sym = l.getSymbol();
	if ( sym == Lexer::SYMBOL )
	{
		func = new FunctionDefinition( l.getSymbolText() );
		func->mReturnType = retType;
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
		do {
			VariableDefinition *def = VariableDefinition::ParseFuncParam( l, func->mFuncScope );
			func->mParameters.push_back( def );
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
	
	func->mFuncBody = Block::Parse( l, func->mFuncScope );

	cout << "Completed function " << endl;
	
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

