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

// Return operator precedence for binary operators (higher = tighter binding).
// Returns -1 if the token is not a binary operator.
static int getOperatorPrec( int sym, const string &symText )
{
	if ( sym == Lexer::LOR ) return 1;    // ||
	if ( sym == Lexer::LAND ) return 2;   // &&
	if ( sym == '|' ) return 3;           // bitwise or
	if ( sym == '^' ) return 4;           // bitwise xor
	if ( sym == '&' ) return 5;           // bitwise and
	if ( sym == Lexer::EQ ) return 6;     // == !=

	// < and > as single chars are comparisons
	if ( sym == '<' || sym == '>' ) return 7;

	// <= and >= come through the lexer as ASSIGN tokens
	if ( sym == Lexer::ASSIGN && ( symText == "<=" || symText == ">=" ) ) return 7;

	if ( sym == Lexer::SHIFT ) return 8;  // << >>
	if ( sym == '+' || sym == '-' ) return 9;
	if ( sym == '*' || sym == '/' || sym == '%' ) return 10;

	return -1;
}

// Convert a token into its string representation for the AST node.
static string getOperatorString( int sym, const string &symText )
{
	switch ( sym )
	{
	case '+': return "+";
	case '-': return "-";
	case '*': return "*";
	case '/': return "/";
	case '%': return "%";
	case '^': return "^";
	case '|': return "|";
	case '&': return "&";
	case '<': return "<";
	case '>': return ">";
	default: break;
	}

	// Multi-character operators: use the lexer text
	if ( sym == Lexer::LOR )   return "||";
	if ( sym == Lexer::LAND )  return "&&";
	if ( sym == Lexer::EQ )    return symText;  // "==" or "!="
	if ( sym == Lexer::SHIFT ) return symText;  // "<<" or ">>"
	if ( sym == Lexer::ASSIGN ) return symText; // "<=" or ">="

	return "";
}

// Parse a primary (atomic) expression: constant, variable, function call,
// or parenthesized sub-expression.
Expression *Expression::ParsePrimary( Lexer &l, Scope *scope )
{
	int sym = l.peekSymbol();

	// Unary prefix operators: -, !, ~
	if ( sym == '-' || sym == '!' || sym == '~' )
	{
		l.getSymbol(); // consume operator
		string opStr( 1, (char)sym );
		Expression *operand = ParsePrimary( l, scope );
		if ( operand == nullptr )
			COMPILE_ERROR( l, "Expected expression after unary operator" );
		return new UnaryExpression( opStr, operand );
	}

	// Parenthesized expression
	if ( sym == '(' )
	{
		l.getSymbol(); // consume '('
		Expression *expr = ParseExpr( l, scope, 0 );
		if ( expr == nullptr )
			COMPILE_ERROR( l, "Expected expression after '('" );
		sym = l.getSymbol();
		if ( sym != ')' )
			COMPILE_ERROR( l, "Expected ')'" );
		return expr;
	}

	// Constants
	if ( sym == Lexer::CONSTANT_NUMBER ||
		 sym == Lexer::CONSTANT_CHAR ||
		 sym == Lexer::CONSTANT_STRING )
	{
		return ConstExpression::Parse( l, scope );
	}

	// Symbol: could be a function call or variable reference
	if ( sym == Lexer::SYMBOL )
	{
		int pos = l.getCurrentPos();

		// Try function call first (save/restore position on failure)
		try {
			CallExpression *callExpr = CallExpression::Parse( l, scope );
			if ( callExpr != nullptr )
				return callExpr;
		} catch ( CompileError & ) {
			// Not a valid call — fall through to variable
		}

		l.setCurrentPos( pos );
		return VariableExpression::Parse( l, scope );
	}

	return nullptr;
}

// Precedence-climbing binary expression parser.
// Parses a binary expression with operators at or above minPrec.
Expression *Expression::ParseExpr( Lexer &l, Scope *scope, int minPrec )
{
	Expression *left = ParsePrimary( l, scope );
	if ( left == nullptr )
		return nullptr;

	while ( true )
	{
		int nextSym = l.peekSymbol();
		string nextText = l.getSymbolText();
		int prec = getOperatorPrec( nextSym, nextText );

		if ( prec < 0 || prec < minPrec )
			break;

		// Consume the operator
		l.getSymbol();
		string opStr = getOperatorString( nextSym, nextText );

		// Parse right-hand side with higher precedence (left-associative)
		Expression *right = ParseExpr( l, scope, prec + 1 );
		if ( right == nullptr )
			COMPILE_ERROR( l, "Expected expression after operator" );

		left = new OperationsExpression( opStr, left, right );
	}

	return left;
}

// Top-level expression parser.
// Parses a full expression (including assignment) and consumes the terminal.
Expression *Expression::Parse( Lexer &l, Scope *scope, char terminal )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	Expression *exp = ParseExpr( l, scope, 0 );

	if ( exp != nullptr )
	{
		int nextSym = l.peekSymbol();
		string nextText = l.getSymbolText();

		// Check for assignment: = or compound assignment (+=, -=, etc.)
		bool isAssign = false;
		string assignOp;

		if ( nextSym == '=' )
		{
			isAssign = true;
			assignOp = "=";
		}
		else if ( nextSym == Lexer::ASSIGN && nextText != "<=" && nextText != ">=" )
		{
			isAssign = true;
			assignOp = nextText;
		}

		if ( isAssign )
		{
			VariableExpression *varExpr = dynamic_cast<VariableExpression*>( exp );
			if ( varExpr == nullptr )
				COMPILE_ERROR( l, "Left side of assignment must be a variable" );

			l.getSymbol(); // consume assignment operator
			Expression *value = ParseExpr( l, scope, 0 );
			if ( value == nullptr )
				COMPILE_ERROR( l, "Expected expression after assignment operator" );

			exp = new AssignmentExpression( assignOp, varExpr->getVariable(), value );
		}
	}

	// Consume terminal if present
	if ( exp != nullptr && l.peekSymbol() == terminal )
		l.getSymbol();

	return exp;
}

VariableExpression *VariableExpression::Parse( Lexer &l, Scope *scope )
{
	TRACE_BEGIN( LOG_LVL_INFO );
	VariableExpression *exp = nullptr;
	int sym = l.getSymbol();
	if ( sym == Lexer::SYMBOL )
	{
		SmartPtr<Symbol> s = scope->findSymbol( l.getSymbolText() );
		if ( s == nullptr )
		{
			cerr << "Symbol Text " << l.getSymbolText() << endl;
			COMPILE_ERROR( l, "Failed to find Symbol" );
		}

		LOG( "Found symbol type %d", s->getSymbolType() );
		if ( s->getSymbolType() == Symbol::TypeVariable )
		{
			VariableDefinition *def = dynamic_cast<VariableDefinition*>( (Symbol*)s );
			if ( def != nullptr )
				exp = new VariableExpression( def );
		}
	}


	return exp;
}

CallExpression *CallExpression::Parse( Lexer &l, Scope *scope )
{
	CallExpression *exp = nullptr;
	int sym  = l.getSymbol();
	if ( sym == Lexer::SYMBOL )
	{
		SmartPtr<Symbol> s = scope->findSymbol( l.getSymbolText() );
		if ( s == nullptr )
		{
			COMPILE_ERROR( l, "Failed to find Symbol" );
		}

		if ( s->getSymbolType() == Symbol::TypeFunction )
		{
			FunctionDefinition *def = dynamic_cast<FunctionDefinition*>( (Symbol*)s );
			exp = new CallExpression( def );

			sym = l.getSymbol();
			if ( sym != '(' )
				return nullptr;

			// Empty argument list
			if ( l.peekSymbol() == ')' )
			{
				l.getSymbol(); // consume ')'
			}
			else
			{
				// Parse arguments until ')'
				do {
					Expression *param = ParseExpr( l, scope, 0 );
					if ( param != nullptr )
						exp->mParams.push_back( param );
					else
						return nullptr;

					sym = l.getSymbol();
				} while ( sym == ',' );

				if ( sym != ')' )
					COMPILE_ERROR( l, "Expected ',' or ')' in function call" );
			}
		}
	}

	return exp;
}

ConstExpression *ConstExpression::Parse( Lexer &l, Scope *scope )
{
	ConstExpression *exp = nullptr;
	int sym = l.getSymbol();
	switch( sym )
	{
	case Lexer::CONSTANT_STRING:
		exp = new ConstString( l.getSymbolText() );
		break;
	case Lexer::CONSTANT_CHAR:
		exp = new ConstChar( l.getSymbolText() );
		break;
	case Lexer::CONSTANT_NUMBER:
		exp = new ConstInteger( atoi( l.getSymbolText().c_str() ) );
		break;

	return nullptr;
	}

	return exp;
}
