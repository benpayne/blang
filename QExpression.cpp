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
	if ( sym == Lexer::RANGE ) return 0;  // .. (lowest precedence)
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
	if ( sym == Lexer::RANGE ) return "..";

	return "";
}

// Parse a primary (atomic) expression: constant, variable, function call,
// array literal, or parenthesized sub-expression, followed by optional
// postfix field access, method calls, and indexing.
Expression *Expression::ParsePrimary( Lexer &l, Scope *scope )
{
	Expression *result = nullptr;
	int sym = l.peekSymbol();

	// await expression: await EXPR
	if ( sym == Lexer::KEYWORD_AWAIT )
	{
		l.getSymbol(); // consume 'await'
		Expression *operand = ParsePrimary( l, scope );
		if ( operand == nullptr )
			COMPILE_ERROR( l, "Expected expression after 'await'" );
		return new AwaitExpression( operand );
	}

	// Query expressions: query T, insert T, update T, delete T
	if ( sym == Lexer::KEYWORD_QUERY )
	{
		return QueryExpression::Parse( l, scope );
	}
	if ( sym == Lexer::KEYWORD_INSERT )
	{
		return InsertExpression::Parse( l, scope );
	}
	if ( sym == Lexer::KEYWORD_UPDATE )
	{
		return UpdateExpression::Parse( l, scope );
	}
	if ( sym == Lexer::KEYWORD_DELETE )
	{
		return DeleteExpression::Parse( l, scope );
	}

	// Query field reference: .field (used in query where/order_by/set blocks)
	if ( sym == '.' )
	{
		l.getSymbol(); // consume '.'
		int fieldSym = l.getSymbol();
		if ( fieldSym != Lexer::SYMBOL )
			COMPILE_ERROR( l, "Expected field name after '.'" );
		result = new QueryFieldExpression( l.getSymbolText() );
	}

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
		result = expr;
	}
	// Array literal: [expr, expr, ...]
	else if ( sym == '[' )
	{
		l.getSymbol(); // consume '['
		ArrayLiteralExpression *arr = new ArrayLiteralExpression();

		// Empty array literal
		if ( l.peekSymbol() == ']' )
		{
			l.getSymbol(); // consume ']'
		}
		else
		{
			// Parse elements until ']'
			do {
				Expression *elem = ParseExpr( l, scope, 0 );
				if ( elem == nullptr )
					COMPILE_ERROR( l, "Expected expression in array literal" );
				arr->addElement( elem );
				sym = l.getSymbol();
			} while ( sym == ',' );

			if ( sym != ']' )
				COMPILE_ERROR( l, "Expected ',' or ']' in array literal" );
		}
		result = arr;
	}
	// String constant with possible interpolation
	else if ( sym == Lexer::CONSTANT_STRING )
	{
		int pos = l.getCurrentPos();
		l.getSymbol(); // consume to read text
		string str = l.getSymbolText();
		l.setCurrentPos( pos ); // restore

		if ( str.find( '{' ) != string::npos )
		{
			l.getSymbol(); // consume for real
			result = StringInterpolation::Parse( l, scope, str );
		}
		else
		{
			result = ConstExpression::Parse( l, scope );
		}
	}
	// Other constants
	else if ( sym == Lexer::CONSTANT_NUMBER ||
		 sym == Lexer::CONSTANT_FLOAT ||
		 sym == Lexer::CONSTANT_CHAR ||
		 sym == Lexer::CONSTANT_BOOL )
	{
		result = ConstExpression::Parse( l, scope );
	}
	// self keyword: look up as a variable
	else if ( sym == Lexer::KEYWORD_SELF )
	{
		l.getSymbol(); // consume 'self'
		SmartPtr<Symbol> selfSym = scope->findSymbol( "self" );
		if ( selfSym == nullptr )
			COMPILE_ERROR( l, "'self' can only be used inside methods" );
		VariableDefinition *def = dynamic_cast<VariableDefinition*>( (Symbol*)selfSym );
		if ( def != nullptr )
			result = new VariableExpression( def );
	}
	// Symbol: could be a struct literal, function call, or variable reference
	else if ( sym == Lexer::SYMBOL )
	{
		int pos = l.getCurrentPos();

		// Check for struct literal: StructName { field: value, ... }
		// Peek at the identifier name and check if it's a known struct
		l.getSymbol(); // consume SYMBOL to read its text
		string identName = l.getSymbolText();
		l.setCurrentPos( pos ); // restore position

		SmartPtr<Symbol> structSym = scope->findSymbol( identName );
		if ( structSym != nullptr &&
			 dynamic_cast<StructDefinition*>( (Symbol*)structSym ) != nullptr )
		{
			// Save position again to peek past the identifier
			l.getSymbol(); // consume SYMBOL
			int nextSym = l.peekSymbol();
			if ( nextSym == '{' )
			{
				l.setCurrentPos( pos ); // restore for Parse method
				result = StructLiteralExpression::Parse( l, scope, identName );
			}
			else if ( nextSym == '<' )
			{
				// Generic struct literal: Box<int> { ... }
				// Save position in case this isn't actually a generic struct literal
				int genericPos = l.getCurrentPos();
				l.getSymbol(); // consume '<'

				// Parse type arguments
				std::vector<SmartPtr<Type>> typeArgs;
				do {
					Type *param = Type::Parse( l, scope, false );
					if ( param == nullptr )
					{
						// Not a valid type — restore and fall through
						l.setCurrentPos( pos );
						typeArgs.clear();
						break;
					}
					typeArgs.push_back( param );
					int next = l.getSymbol();
					if ( next == '>' )
						break;
					if ( next != ',' )
					{
						l.setCurrentPos( pos );
						typeArgs.clear();
						break;
					}
				} while ( true );

				if ( !typeArgs.empty() && l.peekSymbol() == '{' )
				{
					// We have Box<int> { ... } — parse as struct literal with type args
					l.setCurrentPos( pos ); // restore for Parse method
					result = StructLiteralExpression::Parse( l, scope, identName );
				}
				else if ( !typeArgs.empty() )
				{
					// Had type args but no '{' — restore
					l.setCurrentPos( pos );
				}
				// else already restored above
			}
			else
			{
				l.setCurrentPos( pos ); // restore, not a struct literal
			}
		}

		// Check for enum variant construction: EnumName.variant or EnumName.variant(args)
		if ( result == nullptr && structSym != nullptr )
		{
			EnumDefinition *enumDef = dynamic_cast<EnumDefinition*>( (Symbol*)structSym );
			if ( enumDef != nullptr )
			{
				// We have an enum name. Peek ahead for '.variant'
				l.getSymbol(); // consume SYMBOL (the enum name)
				if ( l.peekSymbol() == '.' )
				{
					l.getSymbol(); // consume '.'
					int fieldSym = l.getSymbol();
					if ( fieldSym == Lexer::SYMBOL )
					{
						string variantName = l.getSymbolText();
						// Find the variant index
						int variantIdx = -1;
						const auto &variants = enumDef->getVariants();
						for ( size_t v = 0; v < variants.size(); v++ )
						{
							if ( variants[v].mName == variantName )
							{
								variantIdx = static_cast<int>( v );
								break;
							}
						}
						if ( variantIdx >= 0 )
						{
							EnumConstructExpression *enumExpr = new EnumConstructExpression( enumDef, variantIdx );
							// Check for arguments: variant(args)
							if ( l.peekSymbol() == '(' )
							{
								l.getSymbol(); // consume '('
								if ( l.peekSymbol() != ')' )
								{
									do {
										Expression *arg = ParseExpr( l, scope, 0 );
										if ( arg == nullptr )
											COMPILE_ERROR( l, "Expected expression in enum variant argument" );
										enumExpr->addArg( arg );
										sym = l.getSymbol();
									} while ( sym == ',' );
									if ( sym != ')' )
										COMPILE_ERROR( l, "Expected ',' or ')' in enum variant arguments" );
								}
								else
								{
									l.getSymbol(); // consume ')'
								}
							}
							result = enumExpr;
						}
						else
						{
							l.setCurrentPos( pos ); // restore, unknown variant
						}
					}
					else
					{
						l.setCurrentPos( pos ); // restore
					}
				}
				else
				{
					l.setCurrentPos( pos ); // restore, no dot after enum name
				}
			}
		}

		// Try function call first (save/restore position on failure)
		if ( result == nullptr )
		{
			try {
				CallExpression *callExpr = CallExpression::Parse( l, scope );
				if ( callExpr != nullptr )
					result = callExpr;
			} catch ( CompileError & ) {
				// Not a valid call — fall through to variable
			}
		}

		if ( result == nullptr )
		{
			l.setCurrentPos( pos );
			result = VariableExpression::Parse( l, scope );
		}
	}

	// Postfix operators: field access (.field), method calls (.method()), indexing ([expr])
	while ( result != nullptr )
	{
		if ( l.peekSymbol() == '.' )
		{
			l.getSymbol(); // consume '.'

			int fieldSym = l.getSymbol();
			if ( fieldSym != Lexer::SYMBOL )
				COMPILE_ERROR( l, "Expected field name after '.'" );

			string fieldName = l.getSymbolText();

			// Check if this is a method call: expr.method(args)
			if ( l.peekSymbol() == '(' )
			{
				l.getSymbol(); // consume '('
				MethodCallExpression *methodCall = new MethodCallExpression( result, fieldName );

				// Empty argument list
				if ( l.peekSymbol() == ')' )
				{
					l.getSymbol(); // consume ')'
				}
				else
				{
					// Parse arguments until ')'
					do {
						Expression *arg = ParseExpr( l, scope, 0 );
						if ( arg == nullptr )
							COMPILE_ERROR( l, "Expected expression in method call" );
						methodCall->addArg( arg );
						sym = l.getSymbol();
					} while ( sym == ',' );

					if ( sym != ')' )
						COMPILE_ERROR( l, "Expected ',' or ')' in method call" );
				}
				result = methodCall;
			}
			else
			{
				result = new FieldAccessExpression( result, fieldName );
			}
		}
		else if ( l.peekSymbol() == '[' )
		{
			l.getSymbol(); // consume '['
			Expression *index = ParseExpr( l, scope, 0 );
			if ( index == nullptr )
				COMPILE_ERROR( l, "Expected expression in index" );
			int closeBracket = l.getSymbol();
			if ( closeBracket != ']' )
				COMPILE_ERROR( l, "Expected ']'" );
			result = new IndexExpression( result, index );
		}
		else if ( l.peekSymbol() == Lexer::QUESTION_MARK )
		{
			l.getSymbol(); // consume '?'
			result = new TryExpression( result );
		}
		else
		{
			break;
		}
	}

	return result;
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

		// Handle pipeline operator |> at lowest precedence (below range)
		// Desugars: expr |> fn       -> fn(expr)
		//           expr |> fn(args) -> fn(expr, args)
		if ( nextSym == Lexer::PIPE_ARROW )
		{
			if ( minPrec > 0 )
				break;  // |> has precedence 0, below everything except itself

			l.getSymbol(); // consume |>

			// The RHS of a pipeline can be:
			// 1. A function call: expr |> fn(args) -> fn(expr, args)
			// 2. A bare function name: expr |> fn -> fn(expr)
			int rhsSym = l.peekSymbol();
			if ( rhsSym == Lexer::SYMBOL )
			{
				int pos = l.getCurrentPos();
				l.getSymbol(); // consume symbol
				string name = l.getSymbolText();

				SmartPtr<Symbol> funcSym = scope->findSymbol( name );
				if ( funcSym != nullptr && funcSym->getSymbolType() == Symbol::TypeFunction )
				{
					FunctionDefinition *funcDef = dynamic_cast<FunctionDefinition*>( (Symbol*)funcSym );
					CallExpression *call = new CallExpression( funcDef );
					call->addParam( left );

					// Check if there are additional arguments: fn(args)
					if ( l.peekSymbol() == '(' )
					{
						l.getSymbol(); // consume '('
						if ( l.peekSymbol() != ')' )
						{
							do {
								Expression *arg = ParseExpr( l, scope, 0 );
								if ( arg == nullptr )
									COMPILE_ERROR( l, "Expected expression in pipeline call arguments" );
								call->addParam( arg );
								int s = l.getSymbol();
								if ( s == ')' ) break;
								if ( s != ',' )
									COMPILE_ERROR( l, "Expected ',' or ')' in pipeline call" );
							} while ( true );
						}
						else
						{
							l.getSymbol(); // consume ')'
						}
					}

					left = call;
				}
				else
				{
					// Not a function — try parsing as a general expression
					l.setCurrentPos( pos );
					Expression *right = ParsePrimary( l, scope );
					if ( right == nullptr )
						COMPILE_ERROR( l, "Expected expression after '|>'" );
					left = new PipelineExpression( left, right );
				}
			}
			else
			{
				// RHS starts with something other than a symbol
				Expression *right = ParsePrimary( l, scope );
				if ( right == nullptr )
					COMPILE_ERROR( l, "Expected expression after '|>'" );
				left = new PipelineExpression( left, right );
			}
			continue;
		}

		int prec = getOperatorPrec( nextSym, nextText );

		if ( prec < 0 || prec < minPrec )
			break;

		// Consume the operator
		l.getSymbol();
		string opStr = getOperatorString( nextSym, nextText );

		// Handle range operator specially: create RangeExpression instead of OperationsExpression
		if ( nextSym == Lexer::RANGE )
		{
			Expression *right = ParseExpr( l, scope, prec + 1 );
			if ( right == nullptr )
				COMPILE_ERROR( l, "Expected expression after '..'" );
			left = new RangeExpression( left, right );
			continue;
		}

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

			// Check for generic type arguments: fn<int>(args)
			if ( l.peekSymbol() == '<' )
			{
				l.getSymbol(); // consume '<'
				do {
					Type *param = Type::Parse( l, scope, false );
					if ( param == nullptr )
						COMPILE_ERROR( l, "Expected type argument in generic function call" );
					exp->addTypeArg( param );
					int next = l.getSymbol();
					if ( next == '>' )
						break;
					if ( next != ',' )
						COMPILE_ERROR( l, "Expected ',' or '>' in generic type arguments" );
				} while ( true );
			}

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
	case Lexer::CONSTANT_FLOAT:
		exp = new ConstFloat( atof( l.getSymbolText().c_str() ) );
		break;
	case Lexer::CONSTANT_BOOL:
		exp = new ConstInteger( l.getSymbolText() == "true" ? 1 : 0 );
		break;
	default:
		return nullptr;
	}

	return exp;
}

// Parse string interpolation from a raw string containing {varname} patterns.
// Creates alternating ConstString and VariableExpression parts.
StringInterpolation *StringInterpolation::Parse( Lexer &l, Scope *scope, const string &rawString )
{
	StringInterpolation *interp = new StringInterpolation();
	size_t pos = 0;

	while ( pos < rawString.size() )
	{
		size_t braceStart = rawString.find( '{', pos );
		if ( braceStart == string::npos )
		{
			// Rest is literal
			if ( pos < rawString.size() )
				interp->addPart( new ConstString( rawString.substr( pos ) ) );
			break;
		}

		// Literal before brace
		if ( braceStart > pos )
			interp->addPart( new ConstString( rawString.substr( pos, braceStart - pos ) ) );

		// Find closing brace
		size_t braceEnd = rawString.find( '}', braceStart );
		if ( braceEnd == string::npos )
		{
			// No closing brace, treat rest as literal
			interp->addPart( new ConstString( rawString.substr( braceStart ) ) );
			break;
		}

		// Extract variable name
		string varName = rawString.substr( braceStart + 1, braceEnd - braceStart - 1 );

		// Look up variable in scope
		SmartPtr<Symbol> sym = scope->findSymbol( varName );
		if ( sym != nullptr )
		{
			VariableDefinition *varDef = dynamic_cast<VariableDefinition*>( (Symbol*)sym );
			if ( varDef != nullptr )
			{
				interp->addPart( new VariableExpression( varDef ) );
			}
			else
			{
				// Not a variable, treat as literal
				interp->addPart( new ConstString( rawString.substr( braceStart, braceEnd - braceStart + 1 ) ) );
			}
		}
		else
		{
			// Unknown symbol, treat as literal (might be used before declaration)
			interp->addPart( new ConstString( rawString.substr( braceStart, braceEnd - braceStart + 1 ) ) );
		}

		pos = braceEnd + 1;
	}

	return interp;
}

StructLiteralExpression *StructLiteralExpression::Parse( Lexer &l, Scope *scope, const string &typeName )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	// Consume the struct type name
	int sym = l.getSymbol();
	if ( sym != Lexer::SYMBOL )
		COMPILE_ERROR( l, "Expected struct type name" );

	// Check for generic type arguments: StructName<T1, T2>
	std::vector<SmartPtr<Type>> typeArgs;
	if ( l.peekSymbol() == '<' )
	{
		l.getSymbol(); // consume '<'
		do {
			Type *param = Type::Parse( l, scope, false );
			if ( param == nullptr )
				COMPILE_ERROR( l, "Expected type argument in generic struct literal" );
			typeArgs.push_back( param );
			int next = l.getSymbol();
			if ( next == '>' )
				break;
			if ( next != ',' )
				COMPILE_ERROR( l, "Expected ',' or '>' in generic type arguments" );
		} while ( true );
	}

	// Consume '{'
	sym = l.getSymbol();
	if ( sym != '{' )
		COMPILE_ERROR( l, "Expected '{' in struct literal" );

	StructLiteralExpression *expr = new StructLiteralExpression( typeName );

	// Store generic type arguments if present
	for ( auto &arg : typeArgs )
		expr->addTypeArg( arg );

	// Parse field initializers: fieldName: expression, ...
	if ( l.peekSymbol() != '}' )
	{
		do {
			// Parse field name
			sym = l.getSymbol();
			if ( sym != Lexer::SYMBOL )
				COMPILE_ERROR( l, "Expected field name in struct literal" );
			string fieldName = l.getSymbolText();

			// Expect ':'
			sym = l.getSymbol();
			if ( sym != ':' )
				COMPILE_ERROR( l, "Expected ':' after field name in struct literal" );

			// Parse field value expression
			Expression *value = ParseExpr( l, scope, 0 );
			if ( value == nullptr )
				COMPILE_ERROR( l, "Expected expression for field value in struct literal" );

			expr->addField( fieldName, value );

			// Check for ',' or '}'
			sym = l.peekSymbol();
			if ( sym == ',' )
				l.getSymbol(); // consume ','
		} while ( l.peekSymbol() != '}' );
	}

	// Consume '}'
	sym = l.getSymbol();
	if ( sym != '}' )
		COMPILE_ERROR( l, "Expected '}' in struct literal" );

	return expr;
}
