// QModule.cpp — the compiler frontend's top-level entry points, extracted
// from qcc.cpp so every frontend consumer (qcc, blangd, the fuzzer) shares one
// implementation without conditional-main tricks:
//   - the gScope/gDiag globals
//   - CompileError::getMessage
//   - Module::Parse (two-phase: signatures, then deferred bodies) and the
//     top-level statement parsers that historically lived beside it
//   - createGlobalScope(): the builtin types/functions/protocols/enums

#include <assert.h>

#include <iostream>
#include <fstream>
#include <sstream>

#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

#include "DiagnosticEngine.h"
#include "Frontend.h"
#include "sha256.h"

#include <iomanip>

using namespace QLang;
using namespace std;

// --- Parse-progress trace (see Frontend.h) ----------------------------------

static bool gParseTraceEnabled = false;

void QLang::setParseTraceEnabled( bool enabled )
{
	gParseTraceEnabled = enabled;
}

bool QLang::parseTraceEnabled()
{
	return gParseTraceEnabled;
}

std::ostream &QLang::parseTrace()
{
	if ( gParseTraceEnabled )
		return std::cerr;
	static std::ostream nullStream( nullptr );  // badbit sink: writes are no-ops
	return nullStream;
}

Scope *gScope;

// The single diagnostic reporting path, owned by main() and referenced here so
// the top-level parse-catch (inside Module::Parse) renders through it. Null
// until main() installs it; the catch falls back to a local engine if unset.
DiagnosticEngine *gDiag = nullptr;

// True if `sym` can start a top-level declaration — a panic-mode resync target.
static bool isTopLevelStarter( int sym )
{
	switch ( sym )
	{
		case Lexer::KEYWORD_IMPORT:
		case Lexer::KEYWORD_PUB:
		case Lexer::KEYWORD_TABLE:
		case Lexer::KEYWORD_STRUCT:
		case Lexer::KEYWORD_PROTOCOL:
		case Lexer::KEYWORD_IMPL:
		case Lexer::KEYWORD_ENUM:
		case Lexer::KEYWORD_TEST:
		case Lexer::KEYWORD_ON:
		case Lexer::KEYWORD_FN:
		case Lexer::KEYWORD_ASYNC:
		case Lexer::TYPE_MODIFIER:   // extern
		case Lexer::AT_SIGN:         // @annotation
			return true;
		default:
			return false;
	}
}

// Panic-mode recovery after a top-level parse error: consume the current token
// then skip to the next top-level declaration starter (at brace depth 0, so a
// `fn` inside a lambda/body is not a false resync point) or EOF. Guarantees
// forward progress so Module::Parse's loop cannot spin.
static void resyncTopLevel( Lexer &l )
{
	int depth = 0;
	bool first = true;
	while ( !l.isEOF() )
	{
		int p = l.peekSymbol();
		if ( p == -1 )
			break;
		if ( !first && depth == 0 && isTopLevelStarter( p ) )
			break;
		int sym = l.getSymbol();
		if ( sym == '{' )
			depth++;
		else if ( sym == '}' && depth > 0 )
			depth--;
		first = false;
	}
}

string CompileError::getMessage() const
{
	// Raw message body only. The located "<file>:<line>:<col>: error: " prefix
	// and any compiler-internal C++ throw-site detail are the DiagnosticEngine's
	// job (U2, FR-001/FR-003); this accessor no longer formats them.
	return mMessage;
}

Module *Module::Parse( Lexer &l, Scope *s )
{
	// Module namespacing: `import sys;` enables `sys.args`, `sys.exit()`.
	// In combine mode, stdlib files are parsed into per-module namespace
	// scopes. The expression parser resolves `sys.args` → `sys__args()` etc.

	Module *mod = new Module();
	mod->mScope = s;
	SmartPtr<FunctionDefinition> def;
	// Top-level functions are parsed signature-first with their bodies deferred,
	// so every function is registered before any body is parsed (forward
	// references / mutual recursion). Bodies are parsed after the declaration
	// loop below.
	std::vector<FunctionDefinition*> deferredFuncs;
	while( !l.isEOF() )
	{
		// Peek past any trailing whitespace/comments to check for real EOF
		int nextSym = l.peekSymbol();
		if ( nextSym == -1 )
			break;

		// Per-declaration try/catch enables panic-mode recovery: one bad
		// declaration is reported and skipped, and parsing continues so a single
		// compile reports all top-level syntax errors.
		try {
			// Handle import statements
			if ( nextSym == Lexer::KEYWORD_IMPORT )
			{
				SourceLocation importLoc = l.getTokenLocation();
				l.getSymbol(); // consume 'import'
				int importSym = l.getSymbol();
				if ( importSym != Lexer::SYMBOL )
					COMPILE_ERROR( l, "Expected module name after 'import'" );

				string moduleName = l.getSymbolText();

				// Support dotted paths: import std.io
				while ( l.peekSymbol() == '.' )
				{
					l.getSymbol(); // consume '.'
					importSym = l.getSymbol();
					if ( importSym != Lexer::SYMBOL )
						COMPILE_ERROR( l, "Expected module name after '.'" );
					moduleName += "." + l.getSymbolText();
				}

				// Expect semicolon
				int semi = l.getSymbol();
				if ( semi != ';' )
					COMPILE_ERROR( l, "Expected ';' after import statement" );

				{
					ImportStatement *imp = new ImportStatement( moduleName );
					imp->setLocation( importLoc );
					mod->mImports.push_back( imp );
				}

				// Validate and register the import for qualified access
				Scope *ns = s->findNamespace( moduleName );
				if ( ns != nullptr )
					s->addImportedModule( moduleName );
				// If namespace not found, it may be an external module — allow for now

				PARSE_TRACE( "import " << moduleName );
				continue;
			}

			// Task 63 — Symbol visibility checking:
			// Once multi-module linking is implemented, the compiler must enforce
			// that symbols imported from another module are only accessible when
			// they carry the `pub` modifier in their defining module. Concretely:
			//   - After all modules are parsed, build a per-module export table
			//     containing only symbols whose mIsPublic flag is true.
			//   - During name resolution, when a lookup crosses a module boundary,
			//     reject any symbol that is not in the exporting module's export
			//     table with a "symbol is not public" compile error.
			// At present, each module is parsed independently with no cross-module
			// symbol resolution, so this check is deferred to that future phase.

			// Parse annotations before declarations: @name or @name("arg")
			std::vector<AnnotationNode> annotations;
			while ( nextSym == Lexer::AT_SIGN )
			{
				l.getSymbol(); // consume '@'
				int annSym = l.getSymbol();
				if ( annSym != Lexer::SYMBOL )
					COMPILE_ERROR( l, "Expected annotation name after '@'" );

				AnnotationNode ann;
				ann.mName = l.getSymbolText();

				// Check for optional arguments: @name("arg")
				if ( l.peekSymbol() == '(' )
				{
					l.getSymbol(); // consume '('
					// Parse string arguments
					while ( l.peekSymbol() != ')' )
					{
						int argSym = l.getSymbol();
						if ( argSym == Lexer::CONSTANT_STRING )
							ann.mArgs.push_back( l.getSymbolText() );
						else if ( argSym == Lexer::SYMBOL )
							ann.mArgs.push_back( l.getSymbolText() );
						else
							COMPILE_ERROR( l, "Expected string or identifier in annotation argument" );

						if ( l.peekSymbol() == ',' )
							l.getSymbol(); // consume ','
					}
					l.getSymbol(); // consume ')'
				}

				annotations.push_back( ann );
				PARSE_TRACE( "annotation @" << ann.mName );
				nextSym = l.peekSymbol();
			}

			// Handle pub visibility modifier
			bool isPublic = false;
			if ( nextSym == Lexer::KEYWORD_PUB )
			{
				l.getSymbol(); // consume 'pub'
				isPublic = true;
				nextSym = l.peekSymbol();
			}

			// Handle table struct
			if ( nextSym == Lexer::KEYWORD_TABLE )
			{
				l.getSymbol(); // consume 'table'
				nextSym = l.peekSymbol();
				if ( nextSym != Lexer::KEYWORD_STRUCT )
					COMPILE_ERROR( l, "Expected 'struct' after 'table'" );

				SmartPtr<StructDefinition> structDef = StructDefinition::Parse( l, s, isPublic );
				structDef->setIsTable( true );
				structDef->setAnnotations( annotations );
				mod->mStructList.push_back( structDef );

				// A table struct may also be @json (serialized over the wire);
				// register the generated to_json/from_json forward declarations
				// just like a plain @json struct so they resolve at parse time.
				for ( const auto &ann : annotations )
				{
					if ( ann.mName == "json" )
					{
						if ( structDef->isGeneric() )
							COMPILE_ERROR( l, "@json is not yet supported on generic struct '" + structDef->getName() + "' (requires monomorphization)" );

						FunctionDefinition *toJson = new FunctionDefinition( structDef->getName() + "_to_json" );
						toJson->mReturnType = new Type( "string" );
						toJson->mParameters.push_back( new VariableDefinition( new Type( structDef->getName() ), "self" ) );
						toJson->mIsExtern = true;
						s->addSymbol( toJson );

						FunctionDefinition *fromJson = new FunctionDefinition( structDef->getName() + "_from_json" );
						fromJson->mReturnType = new Type( structDef->getName() );
						fromJson->mParameters.push_back( new VariableDefinition( new Type( "string" ), "input" ) );
						fromJson->mIsExtern = true;
						s->addSymbol( fromJson );
						break;
					}
				}

				PARSE_TRACE( "Completed table struct " << structDef->getName() );
				continue;
			}

			if ( nextSym == Lexer::KEYWORD_STRUCT )
			{
				SmartPtr<StructDefinition> structDef = StructDefinition::Parse( l, s, isPublic );
				structDef->setAnnotations( annotations );
				mod->mStructList.push_back( structDef );

				// Register forward declarations for @json generated functions
				for ( const auto &ann : annotations )
				{
					if ( ann.mName == "json" )
					{
						if ( structDef->isGeneric() )
							COMPILE_ERROR( l, "@json is not yet supported on generic struct '" + structDef->getName() + "' (requires monomorphization)" );

						// StructName_to_json(StructType self) -> string
						FunctionDefinition *toJson = new FunctionDefinition( structDef->getName() + "_to_json" );
						toJson->mReturnType = new Type( "string" );
						toJson->mParameters.push_back( new VariableDefinition( new Type( structDef->getName() ), "self" ) );
						toJson->mIsExtern = true;
						s->addSymbol( toJson );

						// StructName_from_json(string input) -> StructType
						FunctionDefinition *fromJson = new FunctionDefinition( structDef->getName() + "_from_json" );
						fromJson->mReturnType = new Type( structDef->getName() );
						fromJson->mParameters.push_back( new VariableDefinition( new Type( "string" ), "input" ) );
						fromJson->mIsExtern = true;
						s->addSymbol( fromJson );
						break;
					}
				}

				continue;
			}

			if ( nextSym == Lexer::KEYWORD_PROTOCOL )
			{
				SmartPtr<ProtocolDefinition> protoDef = ProtocolDefinition::Parse( l, s, isPublic );
				mod->mProtocolList.push_back( protoDef );
				continue;
			}

			if ( nextSym == Lexer::KEYWORD_IMPL )
			{
				StructDefinition::ParseImplBlock( l, s );
				continue;
			}

			if ( nextSym == Lexer::KEYWORD_ENUM )
			{
				SmartPtr<EnumDefinition> enumDef = EnumDefinition::Parse( l, s, isPublic );
				enumDef->setAnnotations( annotations );
				mod->mEnumList.push_back( enumDef );
				continue;
			}

			if ( nextSym == Lexer::KEYWORD_TEST )
			{
				SmartPtr<TestBlock> testBlock = TestBlock::Parse( l, s );
				mod->mTestBlocks.push_back( testBlock );
				continue;
			}

			// Handle 'on' event handlers at module level
			if ( nextSym == Lexer::KEYWORD_ON )
			{
				SmartPtr<EventHandler> handler = EventHandler::Parse( l, s );
				// Event handlers stored in function list as statements for now
				PARSE_TRACE( "Completed event handler" );
				continue;
			}

			// Handle extern fn declarations
			bool isExtern = false;
			if ( nextSym == Lexer::TYPE_MODIFIER )
			{
				l.getSymbol(); // consume the modifier
				string modText = l.getSymbolText();
				if ( modText == "extern" )
				{
					isExtern = true;
					// Next token should be 'fn'
				}
				else
				{
					COMPILE_ERROR( l, "Unexpected modifier '" + modText + "' at top level" );
				}
			}

			if ( l.peekSymbol() != Lexer::KEYWORD_FN && l.peekSymbol() != Lexer::KEYWORD_ASYNC )
				COMPILE_ERROR( l, "Expected 'fn' or 'async fn' for function declaration" );

			// Task 66 — Mandatory pub type signatures on public functions:
			// Public functions must have fully explicit type signatures with no
			// type inference. This is already enforced by the `fn` grammar: every
			// parameter must carry an explicit type annotation and the return type
			// must be declared after `->` (or omitted to mean void). There is no
			// syntax for inferred parameter or return types in BLang, so public
			// functions automatically satisfy this requirement. No additional
			// validation is required here.

			def = FunctionDefinition::Parse( l, s, isExtern, isPublic, /*deferBody=*/true );
			def->setAnnotations( annotations );
			if ( def->hasDeferredBody() )
				deferredFuncs.push_back( def );

			// Validate @format annotation
			for ( const auto &ann : annotations )
			{
				if ( ann.mName == "format" )
				{
					if ( def->getNumberParams() < 1 ||
						 def->getParamType( 0 )->getName() != "string" )
						COMPILE_ERROR( l, "@format requires first parameter to be type 'string'" );
					if ( !def->isVariadic() )
						COMPILE_ERROR( l, "@format requires function to be variadic (...)" );
					break;
				}
			}

			mod->mFunctionList.push_back( def );
			PARSE_TRACE( *def );
		} catch( CompileError &err ) {
			// Buffer the located diagnostic through the single reporting path,
			// then resync to the next top-level declaration so parsing continues.
			// gDiag is installed by main() before parsing; fall back defensively.
			// The fallback is a collector like gDiag, so on the (degenerate)
			// gDiag==null path finish() it immediately or the error is dropped.
			DiagnosticEngine fallback;
			DiagnosticEngine &eng = ( gDiag != nullptr ) ? *gDiag : fallback;
			eng.reportCompileError( err );
			if ( gDiag == nullptr )
				fallback.finish();
			resyncTopLevel( l );
		}
	}

	// Second pass: parse the deferred function bodies now that every top-level
	// function signature is registered in scope (forward references / mutual
	// recursion). Each body gets its own try/catch so one bad body is reported
	// through the diagnostic engine and the rest still parse.
	//
	// Bodies are parsed in REVERSE source order: parsing a body can rewrite the
	// lexer's symbol-replay list (splitShiftIntoCloseAngles inserts a '>' when a
	// nested generic closes with ">>"), which shifts every position AFTER the
	// insert. Going last-to-first, any insertion lands in a region whose body is
	// already parsed, so the remaining (earlier, smaller) recorded body
	// positions stay valid.
	for ( auto it = deferredFuncs.rbegin(); it != deferredFuncs.rend(); ++it )
	{
		try {
			(*it)->ParseDeferredBody( l );
		} catch( CompileError &err ) {
			DiagnosticEngine fallback;
			DiagnosticEngine &eng = ( gDiag != nullptr ) ? *gDiag : fallback;
			eng.reportCompileError( err );
			if ( gDiag == nullptr )
				fallback.finish();
		}
	}

	return mod;
}

WhileStatement *WhileStatement::Parse( Lexer &l, Scope *scope )
{
	SourceLocation loc = l.getTokenLocation();
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_WHILE )
	{
		COMPILE_ERROR( l, "Internal Compiler Error" );
	}

	WhileStatement *statement = new WhileStatement;
	statement->setLocation( loc );

	statement->mLoopExpression = Expression::ParseExpr( l, scope, 0 );

	if ( statement->mLoopExpression == nullptr )
	{
		COMPILE_ERROR( l, "Expected expression in while condition" );
	}
	
	Scope *loop_scope = new Scope( Scope::kScope_Loop );
	loop_scope->setParent( scope );
	
	if ( l.peekSymbol() == '{' )
		statement->mLoopStatement = Block::Parse( l, loop_scope );
	else
		statement->mLoopStatement = Statement::Parse( l, loop_scope );

	return statement;
}

IfStatement *IfStatement::Parse( Lexer &l, Scope *scope )
{
	SourceLocation loc = l.getTokenLocation();
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_IF )
	{
		COMPILE_ERROR( l, "Internal Compiler Error" );
	}

	IfStatement *statement = new IfStatement;
	statement->setLocation( loc );

	statement->mIfExpression = Expression::ParseExpr( l, scope, 0 );

	if ( statement->mIfExpression == nullptr )
	{
		COMPILE_ERROR( l, "Expected expression in if condition" );
	}
	
	Scope *if_scope = new Scope( Scope::kScope_IfElse );
	if_scope->setParent( scope );
	
	if ( l.peekSymbol() == '{' )
		statement->mStatement = Block::Parse( l, if_scope );
	else
		statement->mStatement = Statement::Parse( l, if_scope );
	
	if ( l.peekSymbol() == Lexer::KEYWORD_ELSE )
	{
		Scope *else_scope = new Scope( Scope::kScope_IfElse );
		else_scope->setParent( scope );
		
		sym = l.getSymbol();
		if ( l.peekSymbol() == '{' )
			statement->mElseStatement = Block::Parse( l, else_scope );
		else
			statement->mElseStatement = Statement::Parse( l, else_scope );
	}

	return statement;
}

ForStatement *ForStatement::Parse( Lexer &l, Scope *scope )
{
	SourceLocation loc = l.getTokenLocation();
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_FOR )
	{
		COMPILE_ERROR( l, "Internal Compiler Error" );
	}

	sym = l.getSymbol();
	if ( sym != '(' )
	{
		COMPILE_ERROR( l, "Expected \'(\'" );
	}

	ForStatement *statement = new ForStatement;
	statement->setLocation( loc );

	statement->mInitialExpression = Expression::Parse( l, scope );
	if ( statement->mInitialExpression == nullptr )
	{
		COMPILE_ERROR( l, "Expected Expression in for statement" );
	}

	statement->mTestExpression = Expression::Parse( l, scope );
	if ( statement->mTestExpression == nullptr )
	{
		COMPILE_ERROR( l, "Expected Expression in for statement" );
	}

	statement->mIterationExpression = Expression::Parse( l, scope, ')' );
	if ( statement->mIterationExpression == nullptr )
	{
		COMPILE_ERROR( l, "Expected Expression in for statement" );
	}

	Scope *for_scope = new Scope( Scope::kScope_Loop );
	for_scope->setParent( scope );

	if ( l.peekSymbol() == '{' )
		statement->mStatement = Block::Parse( l, for_scope );
	else
		statement->mStatement = Statement::Parse( l, for_scope );

	return statement;
}


// Build the global scope with every compiler builtin. See Frontend.h for the
// ownership contract (caller must hold the result in a SmartPtr).
Scope *createGlobalScope()
{
	// Set up the global scope with built-in types. All per-file module scopes
	// will parent to this scope so they share the same primitive type set.
	Scope *s = new Scope( Scope::kScope_Global );
	s->addType( new Type( "int" ) );
	s->addType( new Type( "char" ) );
	s->addType( new Type( "string" ) );
	s->addType( new Type( "bool" ) );
	s->addType( new Type( "float" ) );
	s->addType( new Type( "double" ) );
	s->addType( new Type( "long" ) );
	s->addType( new Type( "short" ) );
	s->addType( new Type( "byte" ) );
	// Task and Array are compiler-known CORE types (D14): Task drives the
	// spawn/wait ABI (getLLVMType special-case, CGTypes.cpp) and Array has a
	// getLLVMType special-case plus dozens of ==\"Array\" ARC predicates, with this
	// registration its sole scope-resolution path — so they stay registered.
	s->addType( new Type( "Task" ) );
	s->addType( new Type( "Array" ) );
	// Buffer is NOT registered here (modules-v2-graph U3, D14): it is an ordinary
	// BLang struct (stdlib/buffer.b) with no compiler special-case, so a bare-name
	// registration would only let `Buffer x;` parse and defer the failure to a
	// worse position. Buffer is a PRELUDE type — it resolves from its parsed
	// definition in buffer.b (unconditionally loaded + promoted). Absent the
	// prelude, `Buffer` now fails at the TYPE with a located error (fail/sema).

	// Register print/println as compiler builtins
	{
		s->addSymbol( FunctionDefinition::CreateBuiltin( "print",
			new Type( "void" ),
			{ new VariableDefinition( new Type( "string" ), "fmt" ) },
			true /* variadic */ ) );

		s->addSymbol( FunctionDefinition::CreateBuiltin( "println",
			new Type( "void" ),
			{ new VariableDefinition( new Type( "string" ), "fmt" ) },
			true /* variadic */ ) );

		// to_json(value) -> string: serializes a @json-annotated struct.
		// Variadic so the parser accepts any struct argument; codegen resolves
		// the concrete type and dispatches to StructName_to_json.
		s->addSymbol( FunctionDefinition::CreateBuiltin( "to_json",
			new Type( "string" ),
			{},
			true /* variadic */ ) );
	}

	// Register Printable as a builtin protocol.
	//
	// KEEP IN SYNC with BmodEmitter::emit's `exportedProtocols` set: a builtin
	// is resolvable everywhere without appearing in any .bmod, so the emitter
	// must know its name or it will silently drop every conformance record that
	// names it. Adding a second builtin protocol here without adding it there is
	// a silent-drop bug, not a compile error.
	{
		FunctionDefinition *toStr = FunctionDefinition::CreateBuiltin( "to_string",
			new Type( "string" ),
			{ new VariableDefinition( new Type( "self" ), "self" ) } );
		s->addSymbol( ProtocolDefinition::CreateBuiltin( "Printable", { toStr } ) );
	}

	// Register Option<T> and Result<T,E> as builtin generic enums. A program that
	// defines its own Option/Result enum shadows these (user defs land in a child
	// scope), so this is backward compatible.
	{
		s->addType( new Type( "Option" ) );
		s->addSymbol( EnumDefinition::CreateBuiltinOption() );
		s->addType( new Type( "Result" ) );
		s->addSymbol( EnumDefinition::CreateBuiltinResult() );
	}

	return s;
}

// See Frontend.h. Shared by qcc and blangd so the two cannot disagree about a
// definition's origin (M-3).
void stampDefiningOrigin( QLang::Module *mod, const std::string &path )
{
	if ( mod == nullptr )
		return;

	std::string origin = path;
	size_t slash = origin.rfind( '/' );
	if ( slash != std::string::npos )
		origin = origin.substr( slash + 1 );
	size_t dot = origin.rfind( '.' );
	if ( dot != std::string::npos )
		origin = origin.substr( 0, dot );

	for ( const auto &sp : mod->getStructList() )
	{
		QLang::StructDefinition *s = const_cast<QLang::StructDefinition *>(
			(const QLang::StructDefinition *)sp );
		if ( s->getDefiningFile().empty() )
			s->setDefiningFile( origin );
	}
}

// See Frontend.h. The closed prelude-type manifest (U3, D12/D13).
bool isPreludeTypeName( const std::string &name )
{
	return name == "Map" || name == "Set" || name == "Buffer";
}

// See Frontend.h. Canonical module-identity digest (U1, D5/D10).
std::string blangModuleDigest( const std::string &canonicalOrigin )
{
	if ( canonicalOrigin.empty() )
		return "";
	SHA256_CTX ctx;
	sha256_init( &ctx );
	sha256_update( &ctx, (const uint8_t *)canonicalOrigin.data(),
		canonicalOrigin.size() );
	uint8_t hash[32];
	sha256_final( &ctx, hash );
	// 12 hex nibbles = 48 bits (design-audit-U1 B2): sized for the shared
	// cross-build/.bmod symbol namespace, not a single build. The within-build
	// collision backstop is separate and does not substitute for this width.
	std::ostringstream ss;
	for ( int i = 0; i < 6; i++ )
		ss << std::hex << std::setfill( '0' ) << std::setw( 2 ) << (int)hash[i];
	return ss.str();
}

// See Frontend.h. Stamp each struct with its defining module's identity digest.
void stampModuleDigest( QLang::Module *mod, const std::string &digest )
{
	if ( mod == nullptr || digest.empty() )
		return;
	for ( const auto &sp : mod->getStructList() )
	{
		QLang::StructDefinition *s = const_cast<QLang::StructDefinition *>(
			(const QLang::StructDefinition *)sp );
		if ( s->getModuleDigest().empty() )
			s->setModuleDigest( digest );
	}
}
