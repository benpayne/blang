#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

SET_LOG_CAT( LOG_CAT_ALL );
SET_LOG_LEVEL( LOG_LVL_NOISE );

using namespace QLang;
using namespace std;

Expression *Expression::Parse( Lexer &l, Scope *scope, char terminal )
{
	TRACE_BEGIN( LOG_LVL_INFO );
	int pos = l.getCurrentPos();
	Expression *exp = NULL;
	
	if ( l.peekSymbol() == Lexer::SYMBOL )
	{
		exp = CallExpression::Parse( l, scope );
		int sym = l.getSymbol();
		if ( exp == NULL or sym != terminal )
		{
			LOG( "Not a call, Moving position to %d", pos );
			l.setCurrentPos( pos );
			
			exp = VariableExpression::Parse( l, scope );
			int sym = l.getSymbol();
			if ( exp == NULL or sym != terminal )
			{
				cerr << "found " << (char)sym << " looking for " << terminal << endl;
				COMPILE_ERROR( l, "Failed to find terminal" );
			}
		}
	}
	else if ( l.peekSymbol() == Lexer::CONSTANT_NUMBER || 
		l.peekSymbol() == Lexer::CONSTANT_CHAR || 
		l.peekSymbol() == Lexer::CONSTANT_STRING )
	{
		exp = ConstExpression::Parse( l, scope );
	}
	
	return exp;
}

VariableExpression *VariableExpression::Parse( Lexer &l, Scope *scope )
{
	TRACE_BEGIN( LOG_LVL_INFO );
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
		
		LOG( "Found symbol type %d", s->getSymbolType() );
		if ( s->getSymbolType() == Symbol::TypeVariable )
		{
			VariableDefinition *def = dynamic_cast<VariableDefinition*>( (Symbol*)s );
			if ( def != NULL )
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
	ConstExpression *exp = NULL;
	int sym = l.getSymbol();
	switch( sym )
	{
	case Lexer::CONSTANT_STRING:
		exp = new ConstString( l.getSymbolText() );
		break;
	case Lexer::CONSTANT_CHAR:
		//exp = jh_new ConstString( l.getSymbolText() );
		break;
	case Lexer::CONSTANT_NUMBER:
		exp = new ConstInteger( atoi( l.getSymbolText().c_str() ) );
		break;
		
	return NULL;
	}
	
	return exp;
}

#if 0
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
#endif

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

