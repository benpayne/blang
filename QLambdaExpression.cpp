#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

using namespace QLang;
using namespace std;

LambdaExpression *LambdaExpression::Parse( Lexer &l, Scope *scope )
{
	// 'fn' already consumed by caller (ParsePrimary)

	// Parse '('
	int sym = l.getSymbol();
	if ( sym != '(' )
		COMPILE_ERROR( l, "Expected '(' in lambda expression" );

	LambdaExpression *lambda = new LambdaExpression();

	// Create a child scope for the lambda body
	SmartPtr<Scope> lambdaScope = new Scope( Scope::kScope_Function );
	lambdaScope->setParent( scope );

	// Parse parameters
	if ( l.peekSymbol() != ')' )
	{
		int paramIndex = 0;
		do {
			VariableDefinition *param = VariableDefinition::ParseFuncParam( l, lambdaScope, false, paramIndex++ );
			if ( param == nullptr )
				COMPILE_ERROR( l, "Expected parameter in lambda expression" );
			lambda->mParameters.push_back( param );
			sym = l.getSymbol();
			if ( sym == ')' ) break;
			if ( sym != ',' )
				COMPILE_ERROR( l, "Expected ',' or ')' in lambda parameters" );
		} while ( true );
	}
	else
	{
		l.getSymbol(); // consume ')'
	}

	// Optional return type: -> Type
	if ( l.peekSymbol() == Lexer::ARROW )
	{
		l.getSymbol(); // consume '->'
		lambda->mReturnType = Type::Parse( l, lambdaScope, false );
		if ( lambda->mReturnType == nullptr )
			COMPILE_ERROR( l, "Expected return type after '->' in lambda" );
	}

	// Parse body block
	lambda->mBody = Block::Parse( l, lambdaScope );
	if ( lambda->mBody == nullptr )
		COMPILE_ERROR( l, "Expected '{' for lambda body" );

	return lambda;
}
