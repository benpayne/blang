#ifndef BLANG_PROJECT_COMPILE_H_
#define BLANG_PROJECT_COMPILE_H_

// modules-v2-lsp U1 (REQ-001): the ONE shared project-compile entry point.
//
// Turns an ordered list of compile inputs (a CompilePlan) into resolved,
// analyzed modules plus buffered diagnostics (a CompileContext). It owns every
// compile rule that used to live inline in qcc.cpp main(): per-input scope
// routing, prelude LOAD/PROMOTE, the .bmod foreign-type pre-scan, format check
// and injection, origin/digest stamping, Sema, and the import-diagnostics pass.
// qcc (the compiler) and lsp/Compile.cpp (blangd) both call ProjectCompile::run,
// so the editor can never apply a rule the compiler does not (and vice versa).
// The stdlib-set selection shared with bcc lives in StdlibSelect.h.
//
// Invariants (contracts/project-compile-api.md in specs/041-shared-project-compile):
//   I1 neutral: a plan qcc builds from its args reports exactly qcc's former diagnostics/hadError.
//   I2 no rendering: run never calls diag.finish() and writes nothing but PARSE_TRACE / lexer trace.
//   I3 aliases: the CALLER installs gScope; run asserts it, never writes it. run sets gDiag and restores it.
//   I4 ownership (KG-7): the context owns every scope it creates and every AST; destroy it before the Resolver.
//   I5 ordering: interfaces first (stable), user input last in combine; PROMOTE right after a provider's Sema;
//      the import pass after all inputs, only when combine || any interface input.
//   I6 known U3 item: .bmod injection still registers dependency namespaces on the GLOBAL scope.
//   I7 kNone: no realpath, no stampModuleDigest; stampDefiningOrigin still runs.
//   I8 no process, no network: run only reads files for !inMemory inputs (ctest blangd_no_subprocess).
//   I9 recorded-only downstream: only RECORDED modules are analyzed, promoted, import-scanned, shadow-counted.

#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Type.h"
#include "Expression.h"
#include "RefCount.h"

namespace QLang
{

class Resolver;
class DiagnosticEngine;

// One compile input.
struct CompileInput
{
	enum class Kind { kSource, kInterface };
	std::string path;              // as given; diagnostics, module name, IR name derive from it
	Kind kind = Kind::kSource;     // kInterface iff ".bmod" (classifyInput)
	bool inMemory = false;         // true: lex/scan `text`; false: read `path` from disk
	std::string text;              // only when inMemory
	std::string origin;            // explicit canonical origin (interface: --bmod-origin); may be empty
};

enum class IdentityPolicy
{
	kCanonical,   // today's origin selection + stampModuleDigest (realpath failure is a located error)
	kNone         // skip origin/digest entirely (blangd U1: unsaved/in-memory documents)
};

// An ordered, explicit description of one compile. No parse state.
struct CompilePlan
{
	std::vector<CompileInput> inputs;   // command-line order; run applies the interfaces-first reorder
	bool combine = false;               // qcc --combine
	std::string moduleOrigin;           // qcc --module-origin (applies to kSource inputs)
	IdentityPolicy identity = IdentityPolicy::kCanonical;
	bool lexerTrace = false;            // per-token lexer trace (qcc: verbose && !dumpLocations)
};

enum class ModuleRole { kUser, kPreludeProvider, kNamespaced, kInterface, kStandalone };

// One record per input that reached routing, in processing order.
struct ModuleRecord
{
	std::string path;
	std::string moduleName;
	ModuleRole role = ModuleRole::kStandalone;
	Scope *scope = nullptr;          // set at routing; owned by the CompileContext
	bool recorded = false;           // set ONLY at the recorded step (after injection) — F1
	SmartPtr<Module> module;         // assigned ONLY when recorded; null for every dropped input
};

class CompileContext
{
public:
	const std::vector<ModuleRecord> &records() const { return mRecords; }
	const ModuleRecord &record( std::size_t i ) const { return mRecords[i]; }
	// Indices (into records()) of RECORDED modules, in order — the recorded-only
	// view every post-loop pass and every qcc output stage reads (F1).
	const std::vector<std::size_t> &moduleIndices() const { return mModuleIndices; }
	// The compile's own failure flag. Does NOT include diag.hasErrors(): callers
	// test `ctx->hadError() || diag.hasErrors()`, as qcc's error exit does (F6).
	bool hadError() const { return mHadError; }
	Scope *userScope() const { return mUserScope; }        // null unless combine
	Scope *preludeScope() const { return mPreludeScope; }  // null unless combine
	// The scope of the last RECORDED source module, else the global scope.
	Scope *bmodEmitScope() const { return mBmodEmitScope; }
	// Name-keyed, exactly as qcc's former inline rules (research R10).
	bool isPreludeProvider( std::size_t recordIndex ) const;
	std::string codegenPrefix( std::size_t recordIndex ) const;
	const std::set<std::string> &suppressedPrelude() const { return mSuppressedPrelude; }

private:
	friend class ProjectCompile;

	// KG-7: every scope obtained from resolver.newModuleScope(); namespace scopes
	// under the combine scope are owned through it (setParent). Declared FIRST so
	// it is destroyed LAST — after the records' ASTs.
	std::vector<SmartPtr<Scope>> mOwnedModuleScopes;
	std::vector<ModuleRecord> mRecords;
	std::vector<std::size_t> mModuleIndices;
	std::set<std::string> mPreludeModuleNames;
	std::set<std::string> mPreludePrefixExempt;
	std::set<std::string> mSuppressedPrelude;
	std::map<std::string, Scope *> mModuleNamespaces;
	bool mHadError = false;
	Scope *mUserScope = nullptr;
	Scope *mPreludeScope = nullptr;
	Scope *mBmodEmitScope = nullptr;
};

class ProjectCompile
{
public:
	// Compile `plan` in `resolver`'s environment, buffering every diagnostic in
	// `diag` (never rendered here). The caller must have installed
	// gScope == resolver.globalScope() and must destroy the returned context
	// before `resolver`.
	static std::unique_ptr<CompileContext> run( const CompilePlan &plan,
		Resolver &resolver, DiagnosticEngine &diag );
};

// ".bmod" suffix rule (the one shared copy of qcc's input classification).
CompileInput::Kind classifyInput( const std::string &path );
// Basename with everything from the last '.' removed ("stdlib/sys.b" -> "sys").
std::string moduleNameOf( const std::string &path );

} // namespace QLang

#endif // BLANG_PROJECT_COMPILE_H_
