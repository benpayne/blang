// modules-v2-lsp U1: the shared project-compile entry point (ProjectCompile.h).
//
// This is qcc.cpp main()'s former per-input loop, .bmod injection and
// import-diagnostics pass, LIFTED VERBATIM and in their original order (research
// R1/R3 in specs/041-shared-project-compile). Local variables became
// CompileContext fields; command-line state became CompilePlan fields. No rule
// was rewritten — any observable difference from the pre-unit qcc is a bug in
// this extraction, never a golden to update.

#include "ProjectCompile.h"

#include <assert.h>
#include <climits>
#include <cstdlib>

#include <fstream>
#include <istream>
#include <sstream>

#include "FileLexer.h"
#include "StringLexerReader.h"
#include "BmodFormat.h"
#include "DiagnosticEngine.h"
#include "Frontend.h"
#include "Resolver.h"
#include "Sema.h"

namespace QLang
{

CompileInput::Kind classifyInput( const std::string &path )
{
	return ( path.size() >= 5 && path.substr( path.size() - 5 ) == ".bmod" )
		? CompileInput::Kind::kInterface : CompileInput::Kind::kSource;
}

std::string moduleNameOf( const std::string &path )
{
	// Derive module name from filename: "stdlib/sys.b" -> "sys"
	std::string fname = path;
	size_t slash = fname.rfind( '/' );
	if ( slash != std::string::npos )
		fname = fname.substr( slash + 1 );
	size_t dot = fname.rfind( '.' );
	if ( dot != std::string::npos )
		fname = fname.substr( 0, dot );
	return fname;
}

void CompileContext::useCapabilityTypes( std::vector<SmartPtr<StructDefinition>> &structs,
	std::vector<SmartPtr<EnumDefinition>> &enums ) const
{
	// Lifted verbatim from qcc.cpp (modules-v2-lsp U4, research R3) — the
	// collection qcc hands CodeGen::registerExternalTypes. modules-v2-graph U3
	// (D13, §5b): the prelude provider's struct of a name a USER module shadows
	// (suppressedPrelude()) is filtered out, making CodeGen's flat mStructDefMap
	// agree with the scope registries (the user's definition wins in all three).
	for ( std::size_t ri : mModuleIndices )
	{
		const ModuleRecord &rec = mRecords[ri];
		bool isProvider = isPreludeProvider( ri );
		for ( auto &st : rec.module->getStructList() )
		{
			// Shadowed prelude struct: drop it so mStructDefMap holds the
			// user's same-named definition (the scope registries already do).
			if ( isProvider && mSuppressedPrelude.count( st->getName() ) )
				continue;
			structs.push_back( st );
		}
		for ( auto &e : rec.module->getEnumList() )
			enums.push_back( e );
	}
}

bool CompileContext::isPreludeProvider( std::size_t recordIndex ) const
{
	return mPreludeModuleNames.count( mRecords[recordIndex].moduleName ) > 0;
}

std::string CompileContext::codegenPrefix( std::size_t recordIndex ) const
{
	// Prelude providers (buffer/collections) are namespaced for resolution
	// (qualified collections.sort) but compile PREFIX-FREE (modules-v2-graph U3,
	// Option B): a prelude type used unqualified must not carry a module prefix —
	// decisive for non-generic Buffer, whose methods would otherwise mangle
	// `buffer__Buffer_*`. Name-keyed, exactly as qcc's former inline rule.
	const std::string &fname = mRecords[recordIndex].moduleName;
	if ( mModuleNamespaces.count( fname ) > 0 &&
		 !mPreludePrefixExempt.count( fname ) )
		return fname;
	return "";
}

namespace
{

// Restores the gDiag alias on every return path out of run().
struct DiagAliasGuard
{
	DiagnosticEngine *mPrev;
	explicit DiagAliasGuard( DiagnosticEngine *next ) : mPrev( gDiag ) { gDiag = next; }
	~DiagAliasGuard() { gDiag = mPrev; }
};

// modules-v2-graph U6a: a .bmod dependency's public interface is registered so
// resolution runs through the IMPORT GRAPH, not a flat merge into gScope.
//
//   - FREE FUNCTIONS land in a PER-MODULE NAMESPACE scope keyed by the dep's
//     import name and registered on gScope (the common ancestor of both the
//     combine user scope and a non-combine module scope, so `mathlib.add`
//     resolves in either build). They are NO LONGER added to gScope's symbol
//     list, so an UNQUALIFIED `add(3,4)` no longer resolves — that is the
//     import-enforcement half of done-condition 6: a dep symbol is reachable
//     only as `module.name` after `import module;` (D1). The namespace path is
//     the same one the namespaced stdlib already uses (QExpression.cpp:251).
//
//   - TYPES (struct/enum) and PROTOCOLS land in the same namespace (U6b, below).
//
// U5: called INCREMENTALLY in topological order (a dependency before its
// dependent), so a later .bmod's foreign types resolve at parse time.
//
// U3 (I6): must move under a context-owned dependency scope — registering on the
// process GLOBAL scope is harmless for one-compile-per-process qcc and for U1's
// blangd (no interface inputs), but a long-lived blangd feeding interfaces
// would accumulate namespaces there (plan A3).
void injectBmodSymbols( Module *bmod, const std::string &moduleName )
{
	// Per-module export namespace for this dep's free functions. Registered on
	// gScope so a consumer in either build mode reaches it up the parent chain.
	Scope *depNs = gScope->findNamespace( moduleName );
	if ( depNs == nullptr )
	{
		depNs = new Scope( Scope::kScope_Namespace );
		depNs->setParent( gScope );
		// U6b: a .bmod dep namespace grants D7 name-capability on import — its
		// exported TYPE names become namable (unqualified) in an importer. (A
		// combine-mode stdlib namespace is NOT marked, so `import net;` keeps
		// net's types qualified-only, never dumped into the user scope.)
		depNs->setGrantsNameCapability( true );
		gScope->addNamespace( moduleName, depNs );
	}
	for ( const auto &sp : bmod->getFunctionList() )
	{
		FunctionDefinition *f = const_cast<FunctionDefinition*>( (const FunctionDefinition*)sp );
		if ( f->isPublic() )
		{
			if ( !f->isGeneric() )
				f->setFunctionExtern( true );
			// Namespace only — NOT gScope. Qualified access resolves it here;
			// unqualified access is a located error (enforcement).
			depNs->addSymbol( f );
		}
	}
	// modules-v2-graph U6b: TYPES (struct/enum) and PROTOCOLS also land in the
	// per-module namespace, NOT gScope. A foreign type is namable in ordinary
	// source only through D7 *name-capability* — granted when the module is
	// imported (the import handler in QModule.cpp copies these namespace entries
	// into the importing scope). *Use-capability* (holding a foreign value,
	// calling its pub methods, monomorphizing a foreign generic returned by an
	// imported function) needs NO import: it rides on CodeGen's flat
	// mStructDefMap/mEnumDefMap (populated by registerExternalTypes from every
	// module's struct/enum list), never on this scope. This is the retirement of
	// the gScope type-injection bridge — all name resolution now runs through the
	// import graph (done-condition 6).
	for ( const auto &sp : bmod->getStructList() )
	{
		StructDefinition *s = const_cast<StructDefinition*>( (const StructDefinition*)sp );
		if ( s->isPublic() )
		{
			depNs->addSymbol( s );
			depNs->addType( new Type( s->getName() ) );
		}
	}
	for ( const auto &sp : bmod->getEnumList() )
	{
		EnumDefinition *e = const_cast<EnumDefinition*>( (const EnumDefinition*)sp );
		if ( e->isPublic() )
		{
			depNs->addSymbol( e );
			depNs->addType( new Type( e->getName() ) );
		}
	}
	for ( const auto &sp : bmod->getProtocolList() )
	{
		ProtocolDefinition *p = const_cast<ProtocolDefinition*>( (const ProtocolDefinition*)sp );
		if ( p->isPublic() )
			depNs->addSymbol( p );
	}
}

// modules-v2-graph U5 (done-condition 7): pre-scan a .bmod for FOREIGN-TYPE
// headers and register each named type BEFORE parsing. Shared by the disk path
// (an std::ifstream, as before) and the in-memory path (an std::istringstream)
// so both apply the identical line rule (research R6). Returns false iff a
// malformed header was reported (each one is reported; the scan continues).
bool prescanForeignTypes( std::istream &fin, const std::string &inputFile,
	Scope *fileScope )
{
	bool ok = true;
	std::string fline;
	int lineNo = 0;
	while ( std::getline( fin, fline ) )
	{
		lineNo++;
		const std::string tag = "// foreign-type:";
		size_t at = fline.find( tag );
		if ( at == std::string::npos )
			continue;
		std::istringstream fs( fline.substr( at + tag.size() ) );
		std::string fname2, fhuman, fdigest, extra;
		fs >> fname2 >> fhuman >> fdigest;
		if ( fname2.empty() || fhuman.empty() || fdigest.empty() ||
			 ( fs >> extra ) )
		{
			SourceLocation floc;
			floc.file = inputFile;
			floc.line = lineNo;
			floc.col = 1;
			gDiag->error( floc,
				"malformed foreign-type reference in interface file "
				"(expected '// foreign-type: <name> <module> <digest>')" );
			ok = false;
			continue;
		}
		// U6b: register the placeholder in THIS .bmod's own fileScope, not
		// gScope — so a foreign type referenced only in an interface signature
		// stays parseable HERE without leaking into a consumer's namable scope
		// (which would silently grant name-capability the consumer never
		// imported). The real definition still reaches CodeGen via the
		// transitive closure + registerExternalTypes (use-capability).
		if ( fileScope->findType( fname2 ) == nullptr )
			fileScope->addType( new Type( fname2 ) );
	}
	return ok;
}

// The interface FORMAT VERSION marker probe: at most 8 lines; no marker at all
// == format 1 (pre-versioned, emitted before the marker existed).
int probeFormatVersion( std::istream &bin )
{
	std::string bline;
	int fileVersion = -1;
	for ( int probe = 0; probe < 8 && std::getline( bin, bline ); probe++ )
	{
		const std::string marker = "// blang-bmod-format:";
		size_t at = bline.find( marker );
		if ( at != std::string::npos )
		{
			fileVersion = atoi( bline.c_str() + at + marker.size() );
			break;
		}
	}
	if ( fileVersion < 0 )
		fileVersion = 1;
	return fileVersion;
}

} // namespace

std::unique_ptr<CompileContext> ProjectCompile::run( const CompilePlan &plan,
	Resolver &resolver, DiagnosticEngine &diag )
{
	// I3 (F2): the CALLER installs gScope (qcc via the Resolver's constructor,
	// blangd per compileDocument call); run only asserts it and never writes it.
	assert( gScope == resolver.globalScope() );
	DiagAliasGuard diagGuard( &diag );

	std::unique_ptr<CompileContext> ctx( new CompileContext() );
	ctx->mBmodEmitScope = gScope;
	const bool combineMode = plan.combine;

	// Reorder: .bmod first, then .b (stable).
	std::vector<const CompileInput *> inputs;
	bool anyInterfaceInput = false;
	for ( const auto &in : plan.inputs )
		if ( in.kind == CompileInput::Kind::kInterface )
		{
			inputs.push_back( &in );
			anyInterfaceInput = true;
		}
	for ( const auto &in : plan.inputs )
		if ( in.kind != CompileInput::Kind::kInterface )
			inputs.push_back( &in );

	// In combine mode, create a shared scope for all .b files.
	// Stdlib .b files get their own namespace scopes registered on combineScope.
	// The user's .b file (last in order) uses combineScope directly.
	//
	// modules-v2-graph U3 (D13): the PRELUDE scope sits between core (gScope) and
	// the user scope (combineScope). Promoted prelude types (Map/Set/Buffer) live
	// here — "base scope like int," never imported. Because combineScope is its
	// CHILD, a user's own `struct Map` in combineScope shadows the prelude Map by
	// normal child-first lookup, UNIFORMLY across both scope registries (mTypeList
	// and mSymbolList) with no addSymbol/addType disagreement. (mStructDefMap, a
	// flat CodeGen map, is reconciled separately by the allStructs shadow filter,
	// which reads suppressedPrelude().)
	//
	//   preludeModuleNames  — stdlib modules that PROVIDE prelude types (buffer,
	//                         collections). LOADED into a holding namespace scope
	//                         (never combineScope), then PROMOTEd (eagerly, below).
	//   preludePrefixExempt — those same modules compile PREFIX-FREE (Option B).
	Scope *combineScope = nullptr;
	Scope *preludeScope = nullptr;
	if ( combineMode )
	{
		// U4: module scopes come from the resolver (parented into its environment).
		preludeScope = resolver.newModuleScope();   // parent = global
		ctx->mOwnedModuleScopes.push_back( preludeScope );
		combineScope = resolver.newModuleScope( preludeScope );
		ctx->mOwnedModuleScopes.push_back( combineScope );
	}
	ctx->mPreludeScope = preludeScope;
	ctx->mUserScope = combineScope;

	// U1 (multi-error): accumulate failure across all files instead of aborting
	// at the first, so one compile reports every file's diagnostics.
	bool hadError = false;

	for ( std::size_t fileIdx = 0; fileIdx < inputs.size(); fileIdx++ )
	{
		const CompileInput &input = *inputs[fileIdx];
		const std::string &inputFile = input.path;
		bool isBmod = ( input.kind == CompileInput::Kind::kInterface );

		ModuleRecord rec;
		rec.path = inputFile;
		rec.moduleName = moduleNameOf( inputFile );

		Scope *fileScope;
		if ( combineMode && !isBmod )
		{
			const std::string &moduleName = rec.moduleName;

			// Last source file is the user's code — use combineScope directly.
			// Stdlib files (not last) get their own namespace scope.
			//
			// modules-v2-graph U3 (D12/D13): three type tiers.
			//   - PRELUDE modules (buffer, collections) PROVIDE prelude TYPES
			//     (Map/Set/Buffer). They are LOADED into a holding namespace scope
			//     (prefix-exempt), NOT combineScope: their parse-time addSymbol/
			//     addType land in the namespace, never combineScope's registries.
			//     The eager PROMOTE below then injects only their prelude types
			//     into preludeScope, so "user wins" shadowing is uniform.
			//     collections' free function `sort` stays in the `collections`
			//     namespace: qualified `collections.sort` (library tier).
			//   - `cli` is an ORDINARY namespaced library module (modules-v2-graph
			//     U6, closes KG-6): qualified `cli.has_flag(...)`, module-prefixed
			//     like every other stdlib namespace (net/fs/timer).
			bool isUserFile = ( fileIdx == inputs.size() - 1 );
			bool isPreludeModule =
				( moduleName == "buffer" || moduleName == "collections" );
			if ( isUserFile )
			{
				fileScope = combineScope;
				rec.role = ModuleRole::kUser;
			}
			else if ( isPreludeModule )
			{
				// LOAD into a prefix-free holding namespace scope; PROMOTE later.
				Scope *nsScope = new Scope( Scope::kScope_Namespace );
				nsScope->setParent( combineScope );
				combineScope->addNamespace( moduleName, nsScope );
				ctx->mModuleNamespaces[moduleName] = nsScope;
				ctx->mPreludeModuleNames.insert( moduleName );
				ctx->mPreludePrefixExempt.insert( moduleName );
				fileScope = nsScope;
				rec.role = ModuleRole::kPreludeProvider;
			}
			else
			{
				// Create a namespace scope for this stdlib module
				Scope *nsScope = new Scope( Scope::kScope_Namespace );
				nsScope->setParent( combineScope );
				combineScope->addNamespace( moduleName, nsScope );
				ctx->mModuleNamespaces[moduleName] = nsScope;
				fileScope = nsScope;
				rec.role = ModuleRole::kNamespaced;
			}
		}
		else
		{
			// U4: non-combine module scope from the resolver (parent = global).
			fileScope = resolver.newModuleScope();
			ctx->mOwnedModuleScopes.push_back( fileScope );
			rec.role = isBmod ? ModuleRole::kInterface : ModuleRole::kStandalone;
		}
		rec.scope = fileScope;
		std::size_t recIdx = ctx->mRecords.size();
		ctx->mRecords.push_back( rec );

		// modules-v2-graph U5 (done-condition 7): pre-scan a .bmod for FOREIGN-TYPE
		// headers and register each named type BEFORE parsing, so signatures that
		// reference a type owned by another module (`-> Box<int>`, Box from boxq)
		// resolve — the interface parses STANDALONE (the bmod_parses check) even
		// when the defining .bmod is not also on the command line.
		// SECURITY: the .bmod is untrusted parsed input — a malformed foreign-ref is
		// a LOCATED error, never a crash (Quality Gate 7).
		if ( isBmod )
		{
			bool ok;
			if ( input.inMemory )
			{
				std::istringstream fin( input.text );
				ok = prescanForeignTypes( fin, inputFile, fileScope );
			}
			else
			{
				std::ifstream fin( inputFile );
				ok = prescanForeignTypes( fin, inputFile, fileScope );
			}
			if ( !ok )
				hadError = true;
			// NOTE: tests the GLOBAL hadError (an earlier file's error also skips
			// this .bmod's parse) — preserved verbatim (research R3.6).
			if ( hadError )
				continue;
		}

		// The reader and lexer live to the end of the iteration, as before.
		std::unique_ptr<LexerReader> reader;
		if ( input.inMemory )
			reader.reset( new StringLexerReader( input.text, inputFile ) );
		else
			reader.reset( new LexerReader( inputFile.c_str() ) );
		Lexer l( reader.get() );
		// Per-token "Symbol …" trace only under -v, never in --dump-locations.
		l.setTraceEnabled( plan.lexerTrace );

		// modules-v2-lsp U4 (research R4): blangd's navigation records each
		// resolved type-position name; qcc never sets the flag, so its parse is
		// untouched (the sink stays null). Collected locally and attached to the
		// record below (KG-7: the context owns it).
		std::vector<TypeReference> typeRefs;
		SmartPtr<Module> mod;
		{
			TypeReferenceSinkScope sinkScope( plan.recordTypeReferences ? &typeRefs : nullptr );
			mod = Module::Parse( l, fileScope );
		}
		if ( mod != nullptr )
			ctx->mRecords[recIdx].typeReferences = std::move( typeRefs );
		if ( mod == nullptr )
		{
			// Catastrophic (unrecoverable) parse failure for this file. The
			// located diagnostic was already buffered; record failure and move on
			// so remaining files still report their errors.
			hadError = true;
			continue;
		}

		// Stamp each struct with the SOURCE FILE that defines it. Applied to .b
		// and .bmod inputs alike, because the namespaced stdlib modules have a
		// real module boundary but arrive as .b source. Shared with blangd via
		// stampDefiningOrigin so the compiler and the editor cannot disagree.
		stampDefiningOrigin( (Module *)mod, inputFile );

		// modules-v2-graph U1: stamp each struct with its DEFINING module's
		// canonical-identity digest (D5/D10) so generic mangling can key on it.
		// Origin source (driver-side):
		//   - a dependency's .bmod: the dep's origin passed via --bmod-origin
		//     (so its generics mangle identically to the dep's own build => dedup);
		//   - a compiled .b module: --module-origin when bcc supplies it, else
		//     realpath(inputFile) in single-file/combine mode.
		// IdentityPolicy::kNone (blangd U1) skips the whole block: no realpath,
		// no digest (research R4).
		if ( plan.identity != IdentityPolicy::kNone )
		{
			std::string origin;
			if ( isBmod )
			{
				origin = input.origin;
			}
			else if ( !plan.moduleOrigin.empty() )
			{
				origin = plan.moduleOrigin;
			}
			if ( origin.empty() )
			{
				char resolved[PATH_MAX];
				if ( realpath( inputFile.c_str(), resolved ) )
					origin = resolved;
				else
				{
					// realpath failure is a located hard error, NEVER an empty
					// identity (design-audit-U1 §2.1): an empty/degenerate digest
					// would collapse distinct modules onto one symbol (a P10-class
					// miscompile through the back door).
					SourceLocation oloc;
					oloc.file = inputFile;
					oloc.line = 1;
					oloc.col = 1;
					gDiag->error( oloc,
						"cannot resolve a canonical origin for module identity "
						"(realpath failed for '" + inputFile + "')" );
					hadError = true;
					continue;
				}
			}
			stampModuleDigest( (Module *)mod, blangModuleDigest( origin ) );
		}

		if ( isBmod )
		{
			// Validate the interface FORMAT VERSION before trusting the file.
			// Without this the marker is write-only: a format-2 .bmod read by a
			// format-3 compiler would silently invert the meaning of every
			// unmarked `init` (exported in 2, private in 3) — the exact inversion
			// the marker exists to prevent. A mismatch is a located error naming
			// the fix, not a mysterious downstream failure.
			{
				int fileVersion;
				if ( input.inMemory )
				{
					std::istringstream bin( input.text );
					fileVersion = probeFormatVersion( bin );
				}
				else
				{
					std::ifstream bin( inputFile );
					fileVersion = probeFormatVersion( bin );
				}
				if ( fileVersion != BlangBmod::kFormatVersion )
				{
					SourceLocation bloc;
					bloc.file = inputFile;
					bloc.line = 1;
					bloc.col = 1;
					gDiag->error( bloc,
						"interface file was produced by a different compiler version "
						"(.bmod format " + std::to_string( fileVersion ) +
						", this compiler expects " +
						std::to_string( BlangBmod::kFormatVersion ) +
						"); rebuild the dependency" );
					hadError = true;
					continue;
				}
			}

			mod->setExtern( true );

			// Stamp ABI provenance on every struct this interface declares.
			// Construction of a struct that arrived through a .bmod must go
			// through the library-emitted factory (the consumer has no field
			// layout to size or destroy it with); construction of a struct
			// defined in this compilation keeps the inline path.
			for ( const auto &sp : mod->getStructList() )
			{
				StructDefinition *s = const_cast<StructDefinition*>(
					(const StructDefinition*)sp );
				s->setFromInterface( true );
			}

			// U5/U6a: register NOW (topological order) so a subsequent .bmod
			// referencing this one's foreign types resolves at parse time. The
			// .bmod basename is the dep's import name — the key for its
			// per-module function namespace.
			injectBmodSymbols( (Module *)mod, rec.moduleName );
		}

		// RECORDED (F1): the point qcc formerly pushed onto modules/moduleScopes/
		// moduleNames. Only a recorded module reaches any downstream pass.
		ctx->mRecords[recIdx].module = mod;
		ctx->mRecords[recIdx].recorded = true;
		ctx->mModuleIndices.push_back( recIdx );
		if ( !isBmod )
			// U6b: the interface being emitted is a compiled .b module's; capture
			// its scope so BmodEmitter can resolve foreign types this module imported.
			ctx->mBmodEmitScope = fileScope;

		PARSE_TRACE( "Completed parse" );
		// Semantic analysis: runs in ALL build modes, immediately after a
		// non-extern module parses and before any code generation, through the
		// single DiagnosticEngine. Extern .bmod modules provide types only and are
		// not analyzed.
		if ( !isBmod && !Sema::analyze( (Module *)mod, fileScope, *gDiag ) )
			hadError = true;

		// modules-v2-graph U3 EAGER PROMOTE (D13): if the just-parsed module is a
		// prelude PROVIDER (buffer/collections), inject its prelude TYPES into
		// preludeScope NOW — before the user file (parsed last) reaches its own
		// sema — so `Map`/`Set`/`Buffer` resolve unqualified with zero imports.
		// Only prelude-manifest types are promoted; a mixed module's free functions
		// (collections.sort) stay in its namespace scope for qualified access.
		if ( combineMode && preludeScope != nullptr &&
			 ctx->mPreludeModuleNames.count( rec.moduleName ) )
		{
			for ( const auto &sp : mod->getStructList() )
			{
				StructDefinition *s = const_cast<StructDefinition*>(
					(const StructDefinition*)sp );
				if ( isPreludeTypeName( s->getName() ) )
				{
					preludeScope->addSymbol( s );
					preludeScope->addType( new Type( s->getName() ) );
				}
			}
		}
	}

	// modules-v2-graph U6b-2 (DC8) — import diagnostics through the shared
	// DiagnosticEngine: an UNKNOWN-module import is a located error; an UNUSED
	// import is a located warning. Gated on an AUTHORITATIVE module set — a real
	// build (combine mode, or dependency .bmods provided — counted as INPUTS,
	// recorded or not) where the set of importable modules is known. A bare
	// single-file parse (a syntax-only fixture like `import std;` with no deps)
	// has no authoritative set, so imports there stay lenient. Only RECORDED
	// modules are scanned (F1/I9).
	{
		bool authoritativeModules = combineMode || anyInterfaceInput;
		if ( authoritativeModules )
		{
			for ( std::size_t ri : ctx->mModuleIndices )
			{
				Module *m = (Module *)ctx->mRecords[ri].module;
				if ( m == nullptr || m->isExtern() )
					continue;
				Scope *msc = ctx->mRecords[ri].scope;
				if ( msc == nullptr )
					continue;
				for ( const auto &impSp : m->getImports() )
				{
					ImportStatement *imp = const_cast<ImportStatement *>(
						(const ImportStatement *)impSp );
					if ( imp == nullptr )
						continue;
					// The unknown-module check verifies the REAL module exists; the
					// unused check + its message key on the LOCAL qualifier (the
					// alias `y` in `import x as y;`, which is what usage marks).
					const std::string &realName = imp->getModuleName();
					const std::string &localQ = imp->getLocalQualifier();
					if ( msc->findNamespace( realName ) == nullptr )
					{
						gDiag->error( imp->getLocation(),
							"unknown module '" + realName + "' — no such dependency or "
							"stdlib module is available to import" );
						hadError = true;
					}
					else if ( !msc->wasModuleUsed( localQ ) )
					{
						gDiag->warning( imp->getLocation(),
							"unused import '" + localQ + "'", "unused-import" );
					}
				}
			}
		}
	}

	// modules-v2-graph U3 (D13, §5b): which prelude type names a USER module
	// shadows, so the prelude provider's struct of that name is filtered out of
	// codegen's allStructs — making CodeGen's flat mStructDefMap agree with the
	// scope registries (the user's definition wins in all three). A "user module"
	// is one routed to combineScope by NAME: not a prelude provider and not a
	// namespaced stdlib module (the test also admits .bmod modules; research R10).
	// Recorded modules only (F1/F6).
	{
		std::set<std::string> userTypeNames;
		for ( std::size_t ri : ctx->mModuleIndices )
		{
			const std::string &mn = ctx->mRecords[ri].moduleName;
			bool routedToCombine =
				!ctx->mPreludeModuleNames.count( mn ) && !ctx->mModuleNamespaces.count( mn );
			if ( routedToCombine )
				for ( auto &s : ctx->mRecords[ri].module->getStructList() )
					userTypeNames.insert( s->getName() );
		}
		for ( const auto &n : userTypeNames )
			if ( isPreludeTypeName( n ) )
				ctx->mSuppressedPrelude.insert( n );
	}

	ctx->mHadError = hadError;
	return ctx;
}

} // namespace QLang
