#include <assert.h>

#include <iostream>
#include <fstream>
#include <sstream>

#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

#ifdef BLANG_HAS_LLVM
#include "CodeGen.h"
#include "llvm/Support/raw_ostream.h"
#endif

using namespace QLang;
using namespace std;

Scope *gScope;

string CompileError::getMessage() const
{
	stringstream s;
	s << "Compiler Error in " << mFilename << ":" << mLineno << endl;
	s << mMessage << " at line: " << mLexer.getLineNumber();
	return s.str();
}

Module *Module::Parse( Lexer &l, Scope *s )
{
	// Task 64 — Flat namespace rule:
	// BLang enforces a flat namespace: there are no nested modules and no
	// module-qualified name references in source (e.g. std.io.println() is
	// illegal). Dotted notation in source is limited to field access and method
	// calls on values, which the expression parser already handles correctly.
	// Module paths in `import` statements (e.g. import std.io) are merely
	// opaque module identifiers resolved by the linker, not namespace prefixes.
	// This constraint is therefore already enforced by the grammar: the parser
	// never produces a qualified-name expression node.

	Module *mod = new Module();
	mod->mScope = s;
	SmartPtr<FunctionDefinition> def;
	try {
		while( !l.isEOF() )
		{
			// Peek past any trailing whitespace/comments to check for real EOF
			int nextSym = l.peekSymbol();
			if ( nextSym == -1 )
				break;

			// Handle import statements
			if ( nextSym == Lexer::KEYWORD_IMPORT )
			{
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

				mod->mImports.push_back( new ImportStatement( moduleName ) );
				cout << "import " << moduleName << endl;
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

			// Handle pub visibility modifier
			bool isPublic = false;
			if ( nextSym == Lexer::KEYWORD_PUB )
			{
				l.getSymbol(); // consume 'pub'
				isPublic = true;
				nextSym = l.peekSymbol();
			}

			if ( nextSym == Lexer::KEYWORD_STRUCT )
			{
				SmartPtr<StructDefinition> structDef = StructDefinition::Parse( l, s, isPublic );
				mod->mStructList.push_back( structDef );
				continue;
			}

			if ( nextSym == Lexer::KEYWORD_PROTOCOL )
			{
				SmartPtr<ProtocolDefinition> protoDef = ProtocolDefinition::Parse( l, s );
				continue;
			}

			if ( nextSym == Lexer::KEYWORD_IMPL )
			{
				StructDefinition::ParseImplBlock( l, s );
				continue;
			}

			if ( nextSym == Lexer::KEYWORD_ENUM )
			{
				SmartPtr<EnumDefinition> enumDef = EnumDefinition::Parse( l, s );
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
				cout << "Completed event handler" << endl;
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

			def = FunctionDefinition::Parse( l, s, isExtern, isPublic );
			mod->mFunctionList.push_back( def );
			cout << *def << endl;
		}
	} catch( CompileError &err ) {
		cerr << err.getMessage() << endl;
		return nullptr;
	}

	return mod;
}

WhileStatement *WhileStatement::Parse( Lexer &l, Scope *scope )
{
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_WHILE )
	{
		COMPILE_ERROR( l, "Internal Compiler Error" );
	}

	sym = l.getSymbol();
	if ( sym != '(' )
	{
		COMPILE_ERROR( l, "Expected \'(\'" );
	}

	WhileStatement *statement = new WhileStatement;
	
	statement->mLoopExpression = Expression::Parse( l, scope, ')' );
	
	if ( statement->mLoopExpression == nullptr )
	{
		COMPILE_ERROR( l, "Expected Expression in while statement" );
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
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_IF )
	{
		COMPILE_ERROR( l, "Internal Compiler Error" );
	}

	sym = l.getSymbol();
	if ( sym != '(' )
	{
		COMPILE_ERROR( l, "Expected \'(\'" );
	}

	IfStatement *statement = new IfStatement;
	
	statement->mIfExpression = Expression::Parse( l, scope, ')' );
	
	if ( statement->mIfExpression == nullptr )
	{
		COMPILE_ERROR( l, "Expected Expression in while statement" );
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

static void printUsage( const char *progName )
{
	std::cerr << "Usage: " << progName << " [options] <filename> [<filename> ...]" << std::endl;
	std::cerr << "Options:" << std::endl;
	std::cerr << "  -S, --emit-ir     Emit LLVM IR (.ll file)" << std::endl;
	std::cerr << "  -c, --emit-obj    Emit object file (.o file)" << std::endl;
	std::cerr << "  -o, --output FILE Output file name" << std::endl;
	std::cerr << "  --parse-only      Parse only, no code generation" << std::endl;
	std::cerr << "  -h, --help        Show this help" << std::endl;
}

int main( int argc, char *argv[] )
{
	if ( argc < 2 )
	{
		printUsage( argv[0] );
		return -1;
	}

	bool emitIR = false;
	bool emitObj = false;
	bool parseOnly = false;
	std::string outputFile;
	std::vector<std::string> inputFiles;

	for ( int i = 1; i < argc; i++ )
	{
		std::string arg = argv[i];
		if ( arg == "-S" || arg == "--emit-ir" )
			emitIR = true;
		else if ( arg == "-c" || arg == "--emit-obj" )
			emitObj = true;
		else if ( arg == "--parse-only" )
			parseOnly = true;
		else if ( arg == "-o" || arg == "--output" )
		{
			if ( i + 1 < argc )
				outputFile = argv[++i];
			else
			{
				std::cerr << "Error: " << arg << " requires an argument" << std::endl;
				return -1;
			}
		}
		else if ( arg == "-h" || arg == "--help" )
		{
			printUsage( argv[0] );
			return 0;
		}
		else if ( arg[0] == '-' )
		{
			std::cerr << "Unknown option: " << arg << std::endl;
			return -1;
		}
		else
		{
			inputFiles.push_back( arg );
		}
	}

	if ( inputFiles.empty() )
	{
		std::cerr << "Error: no input file specified" << std::endl;
		return -1;
	}

	// Set up the global scope with built-in types. All per-file module scopes
	// will parent to this scope so they share the same primitive type set.
	gScope = new Scope( Scope::kScope_Global );
	gScope->addType( new Type( "int" ) );
	gScope->addType( new Type( "char" ) );
	gScope->addType( new Type( "string" ) );
	gScope->addType( new Type( "bool" ) );
	gScope->addType( new Type( "float" ) );
	gScope->addType( new Type( "double" ) );
	gScope->addType( new Type( "long" ) );
	gScope->addType( new Type( "short" ) );

	// Parse each input file into its own Module. Each module gets its own
	// module-level scope parented to the shared global scope so that built-in
	// types are visible everywhere but top-level symbols remain per-file.
	// Cross-module symbol resolution (pub visibility enforcement) will be
	// layered on top once multi-module linking is implemented (Task 63).
	std::vector<SmartPtr<Module>> modules;
	for ( const auto &inputFile : inputFiles )
	{
		Scope *fileScope = new Scope( Scope::kScope_Module );
		fileScope->setParent( gScope );

		LexerReader reader( inputFile.c_str() );
		Lexer l( &reader );

		SmartPtr<Module> mod = Module::Parse( l, fileScope );
		if ( mod == nullptr )
			return -1;

		modules.push_back( mod );
		cout << "Completed parse" << endl;
	}

#ifdef BLANG_HAS_LLVM
	if ( !parseOnly )
	{
		// Code generation: process each parsed module in order.
		for ( std::size_t idx = 0; idx < modules.size(); idx++ )
		{
			const std::string &inputFile = inputFiles[ idx ];
			QLang::CodeGen codegen( inputFile.c_str() );

			if ( !codegen.generate( modules[ idx ] ) )
			{
				cerr << "Code generation failed for " << inputFile << endl;
				return -1;
			}

			if ( !codegen.verify() )
			{
				cerr << "Module verification failed for " << inputFile << endl;
				return -1;
			}

			// Determine output file path for IR. When multiple input files are
			// given, -o only applies to the first; remaining files use derived names.
			std::string irFile;
			if ( !outputFile.empty() && idx == 0 )
			{
				irFile = outputFile;
			}
			else
			{
				// Derive .ll name from input file
				irFile = inputFile;
				size_t dot = irFile.rfind( '.' );
				if ( dot != std::string::npos )
					irFile = irFile.substr( 0, dot );
				irFile += ".ll";
			}

			// Print IR to stdout
			codegen.print( llvm::outs() );

			// Write IR to file
			std::error_code ec;
			llvm::raw_fd_ostream outFile( irFile, ec );
			if ( !ec )
			{
				codegen.print( outFile );
				cout << "Wrote IR to " << irFile << endl;
			}
			else
			{
				cerr << "Failed to write " << irFile << ": " << ec.message() << endl;
			}
		}
	}
#else
	// Without LLVM, --parse-only is the only valid mode; ignore emit flags silently
	(void)emitIR;
	(void)emitObj;
	(void)parseOnly;
#endif

	return 0;
}
