#include <assert.h>

#include <iostream>
#include <fstream>
#include <sstream>

#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

#include "BmodEmitter.h"

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
	// Module namespacing: `import sys;` enables `sys.args`, `sys.exit()`.
	// In combine mode, stdlib files are parsed into per-module namespace
	// scopes. The expression parser resolves `sys.args` → `sys__args()` etc.

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

				// Validate and register the import for qualified access
				Scope *ns = s->findNamespace( moduleName );
				if ( ns != nullptr )
					s->addImportedModule( moduleName );
				// If namespace not found, it may be an external module — allow for now

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
				cout << "annotation @" << ann.mName << endl;
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
				cout << "Completed table struct " << structDef->getName() << endl;
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
			def->setAnnotations( annotations );

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

	WhileStatement *statement = new WhileStatement;

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
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_IF )
	{
		COMPILE_ERROR( l, "Internal Compiler Error" );
	}

	IfStatement *statement = new IfStatement;

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
#ifdef BLANG_HAS_LLVM
	std::cerr << "  -S, --emit-ir     Emit LLVM IR (.ll file)" << std::endl;
	std::cerr << "  -c, --emit-obj    Emit object file (.o file)" << std::endl;
	std::cerr << "  -o, --output FILE Output file name" << std::endl;
	std::cerr << "  --parse-only      Parse only, no code generation" << std::endl;
	std::cerr << "  --combine         Combine all .b files into a single .ll output" << std::endl;
#endif
	std::cerr << "  --emit-bmod FILE  Emit .bmod interface file" << std::endl;
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
	bool combineMode = false;
	std::string outputFile;
	std::string emitBmodFile;
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
		else if ( arg == "--combine" )
			combineMode = true;
		else if ( arg == "--emit-bmod" )
		{
			if ( i + 1 < argc )
				emitBmodFile = argv[++i];
			else
			{
				std::cerr << "Error: --emit-bmod requires an argument" << std::endl;
				return -1;
			}
		}
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
	gScope->addType( new Type( "Task" ) );
	gScope->addType( new Type( "Array" ) );
	gScope->addType( new Type( "Buffer" ) );

	// Register print/println as compiler builtins
	{
		gScope->addSymbol( FunctionDefinition::CreateBuiltin( "print",
			new Type( "void" ),
			{ new VariableDefinition( new Type( "string" ), "fmt" ) },
			true /* variadic */ ) );

		gScope->addSymbol( FunctionDefinition::CreateBuiltin( "println",
			new Type( "void" ),
			{ new VariableDefinition( new Type( "string" ), "fmt" ) },
			true /* variadic */ ) );

		// to_json(value) -> string: serializes a @json-annotated struct.
		// Variadic so the parser accepts any struct argument; codegen resolves
		// the concrete type and dispatches to StructName_to_json.
		gScope->addSymbol( FunctionDefinition::CreateBuiltin( "to_json",
			new Type( "string" ),
			{},
			true /* variadic */ ) );
	}

	// Register Printable as a builtin protocol
	{
		FunctionDefinition *toStr = FunctionDefinition::CreateBuiltin( "to_string",
			new Type( "string" ),
			{ new VariableDefinition( new Type( "self" ), "self" ) } );
		gScope->addSymbol( ProtocolDefinition::CreateBuiltin( "Printable", { toStr } ) );
	}

	// Register Option<T> and Result<T,E> as builtin generic enums. A program that
	// defines its own Option/Result enum shadows these (user defs land in a child
	// scope), so this is backward compatible.
	{
		gScope->addType( new Type( "Option" ) );
		gScope->addSymbol( EnumDefinition::CreateBuiltinOption() );
		gScope->addType( new Type( "Result" ) );
		gScope->addSymbol( EnumDefinition::CreateBuiltinResult() );
	}

	// Parse each input file into its own Module. Each module gets its own
	// module-level scope parented to the shared global scope so that built-in
	// types are visible everywhere but top-level symbols remain per-file.
	// Cross-module symbol resolution (pub visibility enforcement) will be
	// layered on top once multi-module linking is implemented (Task 63).
	// Phase 1: Parse .bmod files first (they provide type info for .b files).
	// Build a map from module name to its parsed scope for import resolution.
	std::vector<SmartPtr<Module>> modules;
	std::map<std::string, Module*> bmodMap;

	// Separate input files into .bmod and .b
	std::vector<std::string> bmodFiles, sourceFiles;
	for ( const auto &f : inputFiles )
	{
		if ( f.size() >= 5 && f.substr( f.size() - 5 ) == ".bmod" )
			bmodFiles.push_back( f );
		else
			sourceFiles.push_back( f );
	}
	// Reorder: .bmod first, then .b
	std::vector<std::string> orderedFiles;
	orderedFiles.insert( orderedFiles.end(), bmodFiles.begin(), bmodFiles.end() );
	orderedFiles.insert( orderedFiles.end(), sourceFiles.begin(), sourceFiles.end() );
	inputFiles = orderedFiles;

	// In combine mode, create a shared scope for all .b files.
	// Stdlib .b files get their own namespace scopes registered on combineScope.
	// The user's .b file (last in order) uses combineScope directly.
	Scope *combineScope = nullptr;
	// Track module names for namespace scopes in combine mode
	std::map<std::string, Scope*> moduleNamespaces;
	if ( combineMode )
	{
		combineScope = new Scope( Scope::kScope_Module );
		combineScope->setParent( gScope );
	}

	for ( std::size_t fileIdx = 0; fileIdx < inputFiles.size(); fileIdx++ )
	{
		const auto &inputFile = inputFiles[fileIdx];
		bool isBmod = ( inputFile.size() >= 5 &&
			inputFile.substr( inputFile.size() - 5 ) == ".bmod" );

		Scope *fileScope;
		if ( combineMode && !isBmod )
		{
			// Derive module name from filename: "stdlib/sys.b" -> "sys"
			std::string moduleName;
			{
				std::string fname = inputFile;
				size_t slash = fname.rfind( '/' );
				if ( slash != std::string::npos )
					fname = fname.substr( slash + 1 );
				size_t dot = fname.rfind( '.' );
				if ( dot != std::string::npos )
					fname = fname.substr( 0, dot );
				moduleName = fname;
			}

			// Last source file is the user's code — use combineScope directly.
			// Stdlib files (not last) get their own namespace scope.
			bool isUserFile = ( fileIdx == inputFiles.size() - 1 );
			if ( isUserFile )
			{
				fileScope = combineScope;
			}
			else
			{
				// Create a namespace scope for this stdlib module
				Scope *nsScope = new Scope( Scope::kScope_Namespace );
				nsScope->setParent( gScope );
				combineScope->addNamespace( moduleName, nsScope );
				moduleNamespaces[moduleName] = nsScope;
				fileScope = nsScope;
			}
		}
		else
		{
			fileScope = new Scope( Scope::kScope_Module );
			fileScope->setParent( gScope );
		}

		// For .b files: inject symbols from any .bmod modules that match imports.
		// Since we don't know imports yet (they're parsed inside Module::Parse),
		// we inject ALL bmod symbols into the global scope so they're available
		// during parsing. This implements the flat merge.
		if ( !isBmod && !bmodMap.empty() )
		{
			for ( auto &pair : bmodMap )
			{
				Module *bmod = pair.second;
				for ( const auto &sp : bmod->getFunctionList() )
				{
					FunctionDefinition *f = const_cast<FunctionDefinition*>( (const FunctionDefinition*)sp );
					if ( f->isPublic() )
					{
						// Mark as extern so codegen only declares (no body)
						f->setFunctionExtern( true );
						gScope->addSymbol( f );
					}
				}
				for ( const auto &sp : bmod->getStructList() )
				{
					StructDefinition *s = const_cast<StructDefinition*>( (const StructDefinition*)sp );
					if ( s->isPublic() )
					{
						gScope->addSymbol( s );
						gScope->addType( new Type( s->getName() ) );
					}
				}
				for ( const auto &sp : bmod->getEnumList() )
				{
					EnumDefinition *e = const_cast<EnumDefinition*>( (const EnumDefinition*)sp );
					if ( e->isPublic() )
					{
						gScope->addSymbol( e );
						gScope->addType( new Type( e->getName() ) );
					}
				}
				for ( const auto &sp : bmod->getProtocolList() )
				{
					ProtocolDefinition *p = const_cast<ProtocolDefinition*>( (const ProtocolDefinition*)sp );
					if ( p->isPublic() )
						gScope->addSymbol( p );
				}
			}
			bmodMap.clear(); // only inject once
		}

		LexerReader reader( inputFile.c_str() );
		Lexer l( &reader );

		SmartPtr<Module> mod = Module::Parse( l, fileScope );
		if ( mod == nullptr )
			return -1;

		if ( isBmod )
		{
			mod->setExtern( true );

			// Extract module name: "/path/to/foo.bmod" -> "foo"
			std::string fname = inputFile;
			size_t slash = fname.rfind( '/' );
			if ( slash != std::string::npos )
				fname = fname.substr( slash + 1 );
			size_t dot = fname.rfind( '.' );
			if ( dot != std::string::npos )
				fname = fname.substr( 0, dot );
			bmodMap[fname] = mod;
		}

		modules.push_back( mod );
		cout << "Completed parse" << endl;
	}

	// Emit .bmod interface file if requested (runs after parsing, before codegen)
	if ( !emitBmodFile.empty() )
	{
		std::vector<Module*> modPtrs;
		for ( auto &mod : modules )
		{
			if ( !mod->isExtern() )
				modPtrs.push_back( mod );
		}

		std::ofstream bmodOut( emitBmodFile );
		if ( !bmodOut.is_open() )
		{
			cerr << "Error: cannot open " << emitBmodFile << " for writing" << endl;
			return -1;
		}
		QLang::BmodEmitter::emit( modPtrs, bmodOut );
		cout << "Wrote .bmod to " << emitBmodFile << endl;
	}

#ifdef BLANG_HAS_LLVM
	if ( !parseOnly )
	{
		// Collect all struct and enum definitions across all modules for
		// cross-module type sharing (Task 67).
		std::vector<SmartPtr<StructDefinition>> allStructs;
		std::vector<SmartPtr<EnumDefinition>> allEnums;
		for ( auto &mod : modules )
		{
			for ( auto &s : mod->getStructList() )
				allStructs.push_back( s );
			for ( auto &e : mod->getEnumList() )
				allEnums.push_back( e );
		}

		if ( combineMode )
		{
			// Combined mode: all .b files compile into a single .ll output.
			// Create one CodeGen instance and generate all modules into it.
			std::string combinedName = "combined";
			if ( !outputFile.empty() )
				combinedName = outputFile;
			else if ( !sourceFiles.empty() )
			{
				// Use the last source file (user code) as the module name
				combinedName = sourceFiles.back();
				size_t dot = combinedName.rfind( '.' );
				if ( dot != std::string::npos )
					combinedName = combinedName.substr( 0, dot );
			}

			QLang::CodeGen codegen( combinedName.c_str() );
			codegen.registerExternalTypes( allStructs, allEnums );

			for ( std::size_t idx = 0; idx < modules.size(); idx++ )
			{
				if ( modules[idx]->isExtern() )
					continue;

				// Determine module prefix for namespace name mangling.
				// Stdlib modules (those with a registered namespace) get a prefix;
				// the user's code (last source file) gets no prefix.
				std::string modPrefix;
				{
					std::string fname = inputFiles[idx];
					size_t slash = fname.rfind( '/' );
					if ( slash != std::string::npos )
						fname = fname.substr( slash + 1 );
					size_t dot = fname.rfind( '.' );
					if ( dot != std::string::npos )
						fname = fname.substr( 0, dot );
					if ( moduleNamespaces.count( fname ) > 0 )
						modPrefix = fname;
				}
				codegen.setModulePrefix( modPrefix );

				if ( !codegen.generate( modules[idx] ) )
				{
					cerr << "Code generation failed for " << inputFiles[idx] << endl;
					return -1;
				}
			}

			// Clear module prefix after all modules are generated
			codegen.setModulePrefix( "" );

			if ( !codegen.verify() )
			{
				cerr << "Module verification failed (combined)" << endl;
				return -1;
			}

			// Determine output IR file path
			std::string irFile;
			if ( !outputFile.empty() )
			{
				irFile = outputFile;
			}
			else if ( !sourceFiles.empty() )
			{
				// Derive from last source file (the user's main file)
				irFile = sourceFiles.back();
				size_t dot = irFile.rfind( '.' );
				if ( dot != std::string::npos )
					irFile = irFile.substr( 0, dot );
				irFile += ".ll";
			}
			else
			{
				irFile = "combined.ll";
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
		else
		{
			// Normal mode: each .b file gets its own .ll output.
			// Code generation: process each parsed module in order.
			// Skip extern-only modules (.bmod files) — they provide type info only.
			for ( std::size_t idx = 0; idx < modules.size(); idx++ )
			{
				if ( modules[idx]->isExtern() )
					continue;

				const std::string &inputFile = inputFiles[ idx ];
				QLang::CodeGen codegen( inputFile.c_str() );

				// Register types from all other modules before generating
				codegen.registerExternalTypes( allStructs, allEnums );

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
	}
#else
	// Without LLVM, --parse-only is the only valid mode; ignore emit flags silently
	(void)emitIR;
	(void)emitObj;
	(void)parseOnly;
	(void)combineMode;
#endif

	return 0;
}
