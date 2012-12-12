#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

using namespace QLang;
using namespace std;

Expression *Expression::Parse( Lexer &l, Scope *scope, char terminal )
{
	int pos = l.getCurrentPos();
		
	Expression *exp = CallExpression::Parse( l, scope );
	int sym = l.getSymbol();
	if ( exp == NULL or sym != terminal )
	{
		l.setCurrentPos( pos );
		
		exp = VariableExpression::Parse( l, scope );
		int sym = l.getSymbol();
		if ( exp == NULL or sym != terminal )
		{
			cerr << "found " << sym << " looking for " << terminal << endl;
			COMPILE_ERROR( l, "Failed to find terminal" );
		}
	}
	
	return exp;
}

VariableExpression *VariableExpression::Parse( Lexer &l, Scope *scope )
{
	VariableExpression *exp = NULL;
	int sym = l.getSymbol();
	if ( sym == Lexer::SYMBOL )
	{
		SmartPtr<Symbol> s = scope->findSymbol( l.getSymbolText() );
		if ( s == NULL )
		{
			cerr << "Symbol Text " << l.getSymbolText() << endl;
			COMPILE_ERROR( l, "Failed to find Symbol" );
		}
		
		if ( s->getSymbolType() == Symbol::TypeVariable )
		{
			VariableDefinition *def = dynamic_cast<VariableDefinition*>( (Symbol*)s );
			exp = new VariableExpression( def );
		}
	}
		
	return exp;	
}

CallExpression *CallExpression::Parse( Lexer &l, Scope *scope )
{
	CallExpression *exp = NULL;
	int sym  = l.getSymbol();
	if ( sym == Lexer::SYMBOL )
	{
		SmartPtr<Symbol> s = scope->findSymbol( l.getSymbolText() );
		if ( s == NULL )
		{
			COMPILE_ERROR( l, "Failed to find Symbol" );
		}

		if ( s->getSymbolType() == Symbol::TypeFunction )
		{
			FunctionDefinition *def = dynamic_cast<FunctionDefinition*>( (Symbol*)s );
			exp = new CallExpression( def );
			
			sym = l.getSymbol();
			if ( sym != '(' )
				return NULL;
			
			for ( int i = 0; i < def->getNumberParams(); i++ )
			{
				char term = ',';
				if ( i == def->getNumberParams() - 1 )
					term = ')';
				
				Expression *param = Expression::Parse( l, scope, term );
				if ( param != NULL )
					exp->mParams.push_back( param );
				else
					return NULL;
			}			
		}
	}
	
	return exp;	
}

ConstExpression *ConstExpression::Parse( Lexer &l, Scope *scope )
{
	return NULL;
}

Expression *Expression::ParseLValue( Lexer &l, Scope *scope )
{
	// look for proper RValue
	Expression *exp = NULL;
	switch( l.peekSymbol() )
	{
		case Lexer::TYPE_MODIFIER:
		case Lexer::BUILTIN_TYPE:
		{
			SmartPtr<VariableDefinition> def = VariableDefinition::Parse( l, scope );
			exp = new VariableExpression( def );
		} break;
			
		case Lexer::SYMBOL:
		{
			SmartPtr<Type> type = scope->findType( l.getSymbolText() );
			if ( type != NULL )
			{
				SmartPtr<VariableDefinition> def = VariableDefinition::Parse( l, scope );
				exp = new VariableExpression( def );
			}
			else
			{
				SmartPtr<Symbol> symbol = scope->findSymbol( l.getSymbolText() );
				if ( symbol != NULL )
				{
					if ( symbol->getSymbolType() == Symbol::TypeVariable )
						exp = VariableExpression::Parse( l, scope );
					else
						exp = CallExpression::Parse( l, scope );
				}
				else
				{
					COMPILE_ERROR( l, "Failed to find symbol" );
				}
			}
		} break;	
	}
	
	// Parse all remaining LValues
	
	return NULL;
}

Expression *Expression::ParseRValue( Lexer &l, Scope *scope )
{
	// look for proper RValue
	Expression *exp = NULL;
	switch( l.peekSymbol() )
	{
	case Lexer::SYMBOL:
	{
		// encure that this symbol is not a type.
		SmartPtr<Type> type = scope->findType( l.getSymbolText() );
		if ( type == NULL )
		{
			SmartPtr<Symbol> symbol = scope->findSymbol( l.getSymbolText() );
			if ( symbol->getSymbolType() == Symbol::TypeVariable )
				exp = VariableExpression::Parse( l, scope );
			else
				exp = CallExpression::Parse( l, scope );
		}
	} break;
		
	case Lexer::CONSTANT_NUMBER:
		exp = ConstExpression::Parse( l, scope );
		break;
	}

	// Parse all remaining LValues
	
	return NULL;
}

