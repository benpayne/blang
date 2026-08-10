#include <assert.h>

#include <cctype>
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
	// Location of this primary's first token. Every node ParsePrimary
	// constructs directly (unary/postfix/inline calls/literals) begins at
	// this token; recursive sub-parses capture their own. A tiny helper
	// stamps any node this function returns or wraps, so none escapes with
	// an unset (0:0) location — spec FR-004.
	SourceLocation ploc = l.getTokenLocation();
	auto S = [&]( Expression *e ) -> Expression * {
		if ( e != nullptr && !e->getLocation().isSet() )
			e->setLocation( ploc );
		return e;
	};
	int sym = l.peekSymbol();

	// await expression: await EXPR
	if ( sym == Lexer::KEYWORD_AWAIT )
	{
		l.getSymbol(); // consume 'await'
		Expression *operand = ParsePrimary( l, scope );
		if ( operand == nullptr )
			COMPILE_ERROR( l, "Expected expression after 'await'" );
		return S( new AwaitExpression( operand ) );
	}

	// Value-producing match: match SUBJECT { pattern { expr } ... }
	// (statement-position match is routed by Statement::Parse and keeps
	// block-bodied arms; here each arm holds a single expression)
	if ( sym == Lexer::KEYWORD_MATCH )
	{
		return S( MatchExpression::Parse( l, scope, /*exprMode=*/true ) );
	}

	// Lambda expression: fn(params) -> RetType { body }
	if ( sym == Lexer::KEYWORD_FN )
	{
		l.getSymbol(); // consume 'fn'
		return LambdaExpression::Parse( l, scope );
	}

	// spawn expression: spawn { ... } returns a Task handle
	if ( sym == Lexer::KEYWORD_SPAWN )
	{
		return SpawnStatement::Parse( l, scope );
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
		return S( new UnaryExpression( opStr, operand ) );
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

		// Check for module-qualified access: sys.args, sys.exit(), net.Socket { }
		Scope *nsScope = scope->findNamespace( identName );
		if ( nsScope != nullptr && scope->isModuleImported( identName ) )
		{
			// U6b-2 (DC8): a qualified access marks the module used (unused-import lint).
			scope->markModuleUsed( identName );
			// Save position past the module name to check for '.'
			l.getSymbol(); // consume module name
			if ( l.peekSymbol() == '.' )
			{
				l.getSymbol(); // consume '.'
				int memberSym = l.getSymbol();
				if ( memberSym != Lexer::SYMBOL )
					COMPILE_ERROR( l, "Expected member name after '" + identName + ".'" );
				string memberName = l.getSymbolText();

				Symbol *resolved = nsScope->findSymbol( memberName );
				if ( resolved == nullptr )
					COMPILE_ERROR( l, "Module '" + identName + "' has no member '" + memberName + "'" );

				FunctionDefinition *funcDef = dynamic_cast<FunctionDefinition*>( resolved );
				StructDefinition *structDef = dynamic_cast<StructDefinition*>( resolved );

				if ( funcDef != nullptr )
				{
					if ( l.peekSymbol() == '(' )
					{
						// sys.exit(1) → CallExpression with mangled name sys__exit
						// Create a call expression referencing the original FunctionDefinition
						// modules-v2-graph U6a — the codegen-naming fork: a
						// qualified call's emitted symbol depends on the callee's
						// PROVENANCE, because a namespaced (combined) module and a
						// .bmod dependency reach codegen differently.
						//   - GENERIC (either): leave the mangled name empty so
						//     codegen monomorphizes via mangleGenericName (already
						//     prefix-free + U1-digest-keyed).
						//   - EXTERN (a .bmod dep function, marked extern by
						//     injectBmodSymbols; also a real `extern fn`): the
						//     UNPREFIXED name — the library `.a` exports plain `add`,
						//     and codegen never prefixes an extern (CodeGen.cpp:157).
						//   - otherwise a COMBINED-STDLIB function compiled in-process
						//     WITH the module prefix (sys.args -> sys__args): prefixed.
						CallExpression *call = new CallExpression( funcDef );
						if ( funcDef->isGeneric() )
							; // monomorphized name from mangleGenericName
						else if ( funcDef->isExtern() )
							call->setMangledName( memberName );
						else
							call->setMangledName( identName + "__" + memberName );

						l.getSymbol(); // consume '('
						if ( l.peekSymbol() != ')' )
						{
							do {
								Expression *arg = ParseExpr( l, scope, 0 );
								if ( arg == nullptr )
									COMPILE_ERROR( l, "Expected expression in function call" );
								call->addParam( arg );
								int s = l.getSymbol();
								if ( s == ')' ) break;
								if ( s != ',' )
									COMPILE_ERROR( l, "Expected ',' or ')' in function call" );
							} while ( true );
						}
						else
						{
							l.getSymbol(); // consume ')'
						}
						result = call;
					}
					else
					{
						// sys.args → zero-arg getter call (property-style).
						// U6a provenance fork (see the '(' branch above).
						CallExpression *call = new CallExpression( funcDef );
						if ( funcDef->isGeneric() )
							; // monomorphized name from mangleGenericName
						else if ( funcDef->isExtern() )
							call->setMangledName( memberName );
						else
							call->setMangledName( identName + "__" + memberName );
						result = call;
					}
				}
				else if ( structDef != nullptr && l.peekSymbol() == '{' )
				{
					// net.Socket { fd: 5 } → struct literal
					// Restore position to just before the '{' and parse normally
					// We need to handle this specially since StructLiteralExpression::Parse
					// expects SYMBOL '{' ...
					int beforeBrace = l.getCurrentPos();

					l.getSymbol(); // consume '{'
					StructLiteralExpression *expr = new StructLiteralExpression( memberName );
					if ( l.peekSymbol() != '}' )
					{
						do {
							int fSym = l.getSymbol();
							if ( fSym != Lexer::SYMBOL )
								COMPILE_ERROR( l, "Expected field name in struct literal" );
							string fieldName = l.getSymbolText();
							int colon = l.getSymbol();
							if ( colon != ':' )
								COMPILE_ERROR( l, "Expected ':' after field name in struct literal" );
							Expression *value = ParseExpr( l, scope, 0 );
							if ( value == nullptr )
								COMPILE_ERROR( l, "Expected expression for field value" );
							expr->addField( fieldName, value );
							if ( l.peekSymbol() == ',' )
								l.getSymbol();
						} while ( l.peekSymbol() != '}' );
					}
					int closeBrace = l.getSymbol();
					if ( closeBrace != '}' )
						COMPILE_ERROR( l, "Expected '}' in struct literal" );
					result = expr;
				}
				else if ( structDef != nullptr )
				{
					// net.Socket used as a type reference — not valid in expression position
					COMPILE_ERROR( l, "'" + identName + "." + memberName + "' is a type, not an expression" );
				}
				else
				{
					COMPILE_ERROR( l, "Module '" + identName + "' member '" + memberName + "' is not a function or struct" );
				}
			}
			else
			{
				// Module name without '.', restore and fall through
				l.setCurrentPos( pos );
			}
		}

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
				else if ( !typeArgs.empty() && l.peekSymbol() == '(' )
				{
					// Generic constructor call: Map<string,int>(args) — construct a
					// generic struct via its `pub init`, monomorphized for the
					// written type args. Symmetric with the non-generic Counter(5)
					// form; the one external construction spelling for a generic
					// struct now that imported struct literals are private (U5b).
					StructDefinition *gsd =
						dynamic_cast<StructDefinition*>( (Symbol*)structSym );
					if ( gsd != nullptr && gsd->hasInit() )
					{
						l.getSymbol(); // consume '('
						ConstructExpression *ctor = new ConstructExpression( gsd );
						for ( auto &ta : typeArgs )
							ctor->addTypeArg( (Type*)ta );
						if ( l.peekSymbol() != ')' )
						{
							do {
								Expression *arg = ParseExpr( l, scope, 0 );
								if ( arg == nullptr )
									COMPILE_ERROR( l,
										"Expected expression in constructor call" );
								ctor->addArg( arg );
								sym = l.getSymbol();
							} while ( sym == ',' );
							if ( sym != ')' )
								COMPILE_ERROR( l, "Expected ')' in constructor call" );
						}
						else
						{
							l.getSymbol(); // consume ')'
						}
						result = ctor;
					}
					else
					{
						// A generic type with no `init` cannot be constructed this
						// way — restore and let later parsing report it.
						l.setCurrentPos( pos );
					}
				}
				else if ( !typeArgs.empty() )
				{
					// Had type args but no '{' or '(' — restore
					l.setCurrentPos( pos );
				}
				// else already restored above
			}
			else
			{
				l.setCurrentPos( pos ); // restore, not a struct literal
			}
		}

		// Check for constructor call: StructName(args)
		if ( result == nullptr && structSym != nullptr )
		{
			StructDefinition *sd = dynamic_cast<StructDefinition*>( (Symbol*)structSym );
			if ( sd != nullptr && sd->hasInit() )
			{
				l.getSymbol(); // consume SYMBOL (struct name)
				int nextSym = l.peekSymbol();
				if ( nextSym == '(' )
				{
					l.getSymbol(); // consume '('
					ConstructExpression *ctor = new ConstructExpression( sd );
					if ( l.peekSymbol() != ')' )
					{
						do {
							Expression *arg = ParseExpr( l, scope, 0 );
							if ( arg == nullptr )
								COMPILE_ERROR( l, "Expected expression in constructor call" );
							ctor->addArg( arg );
							sym = l.getSymbol();
						} while ( sym == ',' );
						if ( sym != ')' )
							COMPILE_ERROR( l, "Expected ')' in constructor call" );
					}
					else
					{
						l.getSymbol(); // consume ')'
					}
					result = ctor;
				}
				else
				{
					l.setCurrentPos( pos ); // restore, not a constructor call
				}
			}
		}

		// Check for static method call: StructName.staticMethod(args)
		if ( result == nullptr && structSym != nullptr )
		{
			StructDefinition *sd = dynamic_cast<StructDefinition*>( (Symbol*)structSym );
			if ( sd != nullptr )
			{
				l.getSymbol(); // consume SYMBOL (struct name)
				if ( l.peekSymbol() == '.' )
				{
					l.getSymbol(); // consume '.'
					int fieldSym = l.getSymbol();
					if ( fieldSym == Lexer::SYMBOL )
					{
						string methodName = l.getSymbolText();
						// Find static method in struct
						FunctionDefinition *staticMethod = nullptr;
						const auto &methods = sd->getMethods();
						for ( size_t mi = 0; mi < methods.size(); mi++ )
						{
							FunctionDefinition *m = const_cast<FunctionDefinition*>( (const FunctionDefinition*)methods[mi] );
							if ( m->getName() == methodName && m->isStatic() )
							{
								staticMethod = m;
								break;
							}
						}
						// Type-directed builtin deserializer: `Todo.from_json(str)`
						// (modules-v2-exports OQ#1). Symmetric with `to_json(value)`
						// — value-directed out, type-directed in — so neither
						// spelling is a hand-written mangled symbol (D15: one
						// canonical spelling). A @json struct registers a
						// `<Struct>_from_json(string) -> Struct` extern at parse
						// (QModule.cpp); resolve to it so all existing Sema
						// arg-checking and codegen dispatch apply unchanged. The old
						// bare `Todo_from_json(str)` still resolves the same symbol.
						if ( staticMethod == nullptr && methodName == "from_json" )
						{
							SmartPtr<Symbol> fjSym =
								scope->findSymbol( sd->getName() + "_from_json" );
							staticMethod = dynamic_cast<FunctionDefinition*>(
								(Symbol*)fjSym );
						}
						if ( staticMethod != nullptr && l.peekSymbol() == '(' )
						{
							l.getSymbol(); // consume '('
							CallExpression *callExpr = new CallExpression( staticMethod );
							callExpr->setMangledName( sd->getName() + "_" + methodName );
							if ( l.peekSymbol() != ')' )
							{
								do {
									Expression *arg = ParseExpr( l, scope, 0 );
									if ( arg == nullptr )
										COMPILE_ERROR( l, "Expected expression in static method call" );
									callExpr->addParam( arg );
									sym = l.getSymbol();
								} while ( sym == ',' );
								if ( sym != ')' )
									COMPILE_ERROR( l, "Expected ')' in static method call" );
							}
							else
							{
								l.getSymbol(); // consume ')'
							}
							result = callExpr;
						}
						else
						{
							l.setCurrentPos( pos ); // restore
						}
					}
					else
					{
						l.setCurrentPos( pos ); // restore
					}
				}
				else
				{
					l.setCurrentPos( pos ); // restore
				}
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
				// Not a valid call — fall through to variable or function ref
			}
		}

		// Check for bare function name used as a value (function reference)
		if ( result == nullptr )
		{
			l.setCurrentPos( pos );
			l.getSymbol(); // consume SYMBOL to read name
			string symName = l.getSymbolText();
			SmartPtr<Symbol> symLookup = scope->findSymbol( symName );
			if ( symLookup != nullptr && symLookup->getSymbolType() == Symbol::TypeFunction &&
				 l.peekSymbol() != '(' )
			{
				FunctionDefinition *funcDef = dynamic_cast<FunctionDefinition*>( (Symbol*)symLookup );
				if ( funcDef != nullptr )
					result = new FunctionRefExpression( funcDef );
			}
			else
			{
				l.setCurrentPos( pos ); // restore for variable parse
			}
		}

		if ( result == nullptr )
		{
			l.setCurrentPos( pos );
			result = VariableExpression::Parse( l, scope );

			// Check for indirect call through function-typed variable
			if ( result != nullptr )
			{
				VariableExpression *varExpr = dynamic_cast<VariableExpression*>( result );
				if ( varExpr != nullptr &&
					 varExpr->getVariable()->getVariableType()->isFunctionType() &&
					 l.peekSymbol() == '(' )
				{
					l.getSymbol(); // consume '('
					IndirectCallExpression *indCall = new IndirectCallExpression( varExpr->getVariable() );
					if ( l.peekSymbol() != ')' )
					{
						do {
							Expression *arg = ParseExpr( l, scope, 0 );
							if ( arg == nullptr )
								COMPILE_ERROR( l, "Expected expression in indirect call" );
							indCall->addParam( arg );
							sym = l.getSymbol();
							if ( sym == ')' ) break;
							if ( sym != ',' )
								COMPILE_ERROR( l, "Expected ',' or ')' in indirect call" );
						} while ( true );
					}
					else
					{
						l.getSymbol(); // consume ')'
					}
					result = indCall;
				}
			}
		}
	}

	// Stamp the base primary before any postfix wrapping so wrapped children
	// keep their own location and never escape unset.
	result = S( result );

	// Postfix operators: field access (.field), method calls (.method()), indexing ([expr])
	while ( result != nullptr )
	{
		if ( l.peekSymbol() == '.' )
		{
			l.getSymbol(); // consume '.'

			// Member accesses are located at the MEMBER NAME token, not the
			// leftmost operand: an unknown-field/method diagnostic (and an LSP
			// hover/definition) should point at `.field`, not at the object
			// expression that may start many tokens earlier.
			SourceLocation memberLoc = l.getTokenLocation();

			int fieldSym = l.getSymbol();
			if ( fieldSym != Lexer::SYMBOL )
				COMPILE_ERROR( l, "Expected field name after '.'" );

			string fieldName = l.getSymbolText();

			// Check if this is a method call: expr.method(args)
			if ( l.peekSymbol() == '(' )
			{
				l.getSymbol(); // consume '('
				MethodCallExpression *methodCall = new MethodCallExpression( result, fieldName );
				methodCall->setLocation( memberLoc );

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
				FieldAccessExpression *fieldAccess = new FieldAccessExpression( result, fieldName );
				fieldAccess->setLocation( memberLoc );
				result = fieldAccess;
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
			result = S( new IndexExpression( result, index ) );
		}
		else if ( l.peekSymbol() == Lexer::QUESTION_MARK )
		{
			l.getSymbol(); // consume '?'
			result = S( new TryExpression( result ) );
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

	// A binary/range/pipeline node spans from the first operand's first
	// token; give each such node the leftmost location (spec assumption).
	SourceLocation startLoc = left->getLocation();
	auto S = [&]( Expression *e ) -> Expression * {
		if ( e != nullptr && !e->getLocation().isSet() )
			e->setLocation( startLoc );
		return e;
	};

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

					left = S( call );
				}
				else
				{
					// Not a function — try parsing as a general expression
					l.setCurrentPos( pos );
					Expression *right = ParsePrimary( l, scope );
					if ( right == nullptr )
						COMPILE_ERROR( l, "Expected expression after '|>'" );
					left = S( new PipelineExpression( left, right ) );
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
			left = S( new RangeExpression( left, right ) );
			continue;
		}

		// Parse right-hand side with higher precedence (left-associative)
		Expression *right = ParseExpr( l, scope, prec + 1 );
		if ( right == nullptr )
			COMPILE_ERROR( l, "Expected expression after operator" );

		left = S( new OperationsExpression( opStr, left, right ) );
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
			// The assignment node spans from the LHS's first token.
			SourceLocation assignLoc = exp->getLocation();
			VariableExpression *varExpr = dynamic_cast<VariableExpression*>( exp );
			FieldAccessExpression *fieldExpr = dynamic_cast<FieldAccessExpression*>( exp );
			IndexExpression *indexExpr = dynamic_cast<IndexExpression*>( exp );

			if ( varExpr != nullptr )
			{
				l.getSymbol(); // consume assignment operator
				Expression *value = ParseExpr( l, scope, 0 );
				if ( value == nullptr )
					COMPILE_ERROR( l, "Expected expression after assignment operator" );
				exp = new AssignmentExpression( assignOp, varExpr->getVariable(), value );
				exp->setLocation( assignLoc );
			}
			else if ( fieldExpr != nullptr )
			{
				l.getSymbol(); // consume assignment operator
				Expression *value = ParseExpr( l, scope, 0 );
				if ( value == nullptr )
					COMPILE_ERROR( l, "Expected expression after assignment operator" );
				exp = new FieldAssignmentExpression( assignOp, fieldExpr->getObject(), fieldExpr->getFieldName(), value );
				exp->setLocation( assignLoc );
			}
			else if ( indexExpr != nullptr )
			{
				l.getSymbol(); // consume assignment operator
				Expression *value = ParseExpr( l, scope, 0 );
				if ( value == nullptr )
					COMPILE_ERROR( l, "Expected expression after assignment operator" );
				exp = new IndexAssignmentExpression( assignOp, indexExpr->getObject(), indexExpr->getIndex(), value );
				exp->setLocation( assignLoc );
			}
			else
			{
				COMPILE_ERROR( l, "Left side of assignment must be a variable, field, or index expression" );
			}
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
	SourceLocation loc = l.getTokenLocation();
	VariableExpression *exp = nullptr;
	int sym = l.getSymbol();
	if ( sym == Lexer::SYMBOL )
	{
		string name = l.getSymbolText();
		SmartPtr<Symbol> s = scope->findSymbol( name );
		if ( s == nullptr )
		{
			// modules-v2-graph U6b-2 (DC8, D3): a bare name that is actually
			// exported by an imported/available module — a dependency's function
			// used unqualified (`greet(...)` for `lib.greet`) — gets a D3-rendered
			// "did you mean module.name?" hint instead of the bare "not found".
			std::string owner = scope->moduleExporting( name );
			if ( !owner.empty() )
				COMPILE_ERROR( l, "undefined symbol '" + name +
					"' — did you mean '" + owner + "." + name +
					"'? (a dependency's names are reached qualified after `import " +
					owner + ";`)" );
			COMPILE_ERROR( l, "undefined symbol '" + name + "'" );
		}

		LOG( "Found symbol type %d", s->getSymbolType() );
		if ( s->getSymbolType() == Symbol::TypeVariable )
		{
			VariableDefinition *def = dynamic_cast<VariableDefinition*>( (Symbol*)s );
			if ( def != nullptr )
			{
				exp = new VariableExpression( def );
				exp->setLocation( loc );
			}
		}
	}


	return exp;
}

CallExpression *CallExpression::Parse( Lexer &l, Scope *scope )
{
	SourceLocation loc = l.getTokenLocation();
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
			exp->setLocation( loc );

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
	SourceLocation loc = l.getTokenLocation();
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

	if ( exp != nullptr )
		exp->setLocation( loc );

	return exp;
}

// Parse string interpolation from a raw string containing {varname} patterns.
// Creates alternating ConstString and VariableExpression parts.
StringInterpolation *StringInterpolation::Parse( Lexer &l, Scope *scope, const string &rawString )
{
	// Interpolation parts are synthesized from the raw string; give them a
	// real (non-zero) location near the string token (FR-004 inheritance).
	SourceLocation loc = l.getTokenLocation();
	StringInterpolation *interp = new StringInterpolation();
	interp->setLocation( loc );
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

		// Extract the placeholder text: a variable name, or a dotted field path
		// such as `self.count` or `req.path`.
		string varName = rawString.substr( braceStart + 1, braceEnd - braceStart - 1 );

		// Split on '.' so a field path becomes a FieldAccessExpression chain.
		// Before this, ANY placeholder that was not a bare variable name was
		// silently copied to the output as literal text — so a program printed
		// its own source ("{self.x}") with no diagnostic. The rule now is:
		// resolve it, or reject it; never emit it as text (Principle III).
		std::vector<string> segs;
		{
			size_t segStart = 0;
			while ( true )
			{
				size_t dot = varName.find( '.', segStart );
				if ( dot == string::npos )
				{
					segs.push_back( varName.substr( segStart ) );
					break;
				}
				segs.push_back( varName.substr( segStart, dot - segStart ) );
				segStart = dot + 1;
			}
		}

		// A placeholder that is NOT an identifier path is left alone: `{}` and
		// `{:.2f}` are print FORMAT placeholders handled by FormatString, and a
		// literal brace run is just text. Only something shaped like a name (or a
		// dotted name) is resolved — and only that shape is rejected when it does
		// not resolve. Each segment must obey the identifier rule (leading letter
		// or underscore), so a positional/format placeholder like `{0}` and an
		// embedded JSON/template brace stay literal rather than becoming an
		// error about a variable nobody wrote.
		bool looksLikePath = !varName.empty();
		for ( const string &seg : segs )
		{
			if ( seg.empty() ||
				 !( isalpha( (unsigned char)seg[0] ) || seg[0] == '_' ) )
			{
				looksLikePath = false;
				break;
			}
			for ( char c : seg )
			{
				if ( !( isalnum( (unsigned char)c ) || c == '_' ) )
				{
					looksLikePath = false;
					break;
				}
			}
			if ( !looksLikePath )
				break;
		}
		if ( !looksLikePath )
		{
			interp->addPart( new ConstString(
				rawString.substr( braceStart, braceEnd - braceStart + 1 ) ) );
			pos = braceEnd + 1;
			continue;
		}

		// Every segment is a well-formed identifier by the check above, so the
		// only way this fails is that the BASE names nothing (or names a
		// non-variable such as a function). Field names are resolved later, by
		// Sema, which reports its own located "no field" diagnostic.
		SmartPtr<Symbol> sym = scope->findSymbol( segs[0] );
		VariableDefinition *varDef = ( sym != nullptr )
			? dynamic_cast<VariableDefinition*>( (Symbol*)sym ) : nullptr;
		if ( varDef != nullptr )
		{
			Expression *base = new VariableExpression( varDef );
			base->setLocation( loc );
			for ( size_t si = 1; si < segs.size(); si++ )
			{
				Expression *fa = new FieldAccessExpression( base, segs[si] );
				fa->setLocation( loc );
				base = fa;
			}
			interp->addPart( base );
		}
		else
		{
			// Not resolvable here. Emitting it as literal text is what made this
			// a silent wrong answer; say so instead. (Sema resolves the FIELD
			// names above — this only rejects a placeholder whose base is not a
			// variable in scope at all.)
			COMPILE_ERROR( l, "cannot interpolate '{" + varName +
				"}': '" + ( segs.empty() ? varName : segs[0] ) +
				"' is not a variable in scope" );
		}

		pos = braceEnd + 1;
	}

	// Ensure every synthesized part carries a real location.
	for ( auto &part : interp->mParts )
	{
		if ( part != nullptr && !part->getLocation().isSet() )
			part->setLocation( loc );
	}

	return interp;
}

StructLiteralExpression *StructLiteralExpression::Parse( Lexer &l, Scope *scope, const string &typeName )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	SourceLocation loc = l.getTokenLocation();
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
	expr->setLocation( loc );

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
