# Contract: `ProjectCompile.h` (shared frontend, no LLVM)

The seam that `qcc` (U1), `blangd` (U1 single-file; U2 stdlib; U3 project) and the `project_compile` ctest call. Entity details are in [../data-model.md](../data-model.md).

```cpp
#ifndef BLANG_PROJECT_COMPILE_H_
#define BLANG_PROJECT_COMPILE_H_

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

struct CompileInput
{
	enum class Kind { kSource, kInterface };
	std::string path;
	Kind kind = Kind::kSource;
	bool inMemory = false;
	std::string text;
	std::string origin;
};

enum class IdentityPolicy { kCanonical, kNone };

struct CompilePlan
{
	std::vector<CompileInput> inputs;
	bool combine = false;
	std::string moduleOrigin;
	IdentityPolicy identity = IdentityPolicy::kCanonical;
	bool lexerTrace = false;
};

enum class ModuleRole { kUser, kPreludeProvider, kNamespaced, kInterface, kStandalone };

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
	const std::vector<ModuleRecord> &records() const;
	const ModuleRecord &record( std::size_t i ) const;
	// Indices (into records()) of RECORDED modules, in order — the recorded-only view
	// every post-loop pass and every qcc output stage reads (F1).
	const std::vector<std::size_t> &moduleIndices() const;
	bool hadError() const;
	Scope *userScope() const;        // null unless combine
	Scope *preludeScope() const;     // null unless combine
	Scope *bmodEmitScope() const;
	bool isPreludeProvider( std::size_t recordIndex ) const;
	std::string codegenPrefix( std::size_t recordIndex ) const;
	const std::set<std::string> &suppressedPrelude() const;
	// (private: owned module scopes, records, name sets; ProjectCompile is a friend)
};

class ProjectCompile
{
public:
	static std::unique_ptr<CompileContext> run( const CompilePlan &plan,
		Resolver &resolver, DiagnosticEngine &diag );
};

CompileInput::Kind classifyInput( const std::string &path );   // ".bmod" suffix
std::string moduleNameOf( const std::string &path );           // basename, last-dot stripped

} // namespace QLang

#endif
```

## Invariants (each is a review checkpoint and, where marked, a test)

- **I1 — Neutral reproduction.** For any `CompilePlan` that `qcc` builds from its arguments, `run` reports the same diagnostics (same calls to `diag.error`/`warning`/`reportCompileError`, in the same order) and the same `hadError` as qcc's inline loop at `epic/modules-v2-lsp` @ `2f68efe`. *Tests:* all existing suites (SC-001), the `project_compile` C1 case, and the quickstart §3 byte-identity check.
- **I2 — No rendering.** `run` never calls `diag.finish()` and never writes to stdout or stderr itself. The only exceptions are `PARSE_TRACE` and lexer trace lines, which are emitted only when the driver enabled them, exactly as today.
- **I3 — Aliases (amended, F2).** The **caller installs `gScope`**. `run` asserts `gScope == resolver.globalScope()` in debug builds and never writes `gScope`.
  - qcc's `Resolver` constructor installs it.
  - `lsp/Compile.cpp` keeps `gScope = sResolver.globalScope();` on every call. This is required because a test-local `Resolver` (`resolver_reuse`, `parser_no_leak`) restores `gScope` to its saved, possibly null, value when it is destroyed.

  For `gDiag`, by contrast, `run` itself sets `gDiag = &diag` on entry and restores the entry value on return.
- **I4 — Ownership (KG-7).** Every scope `run` obtains from `resolver.newModuleScope` is owned by the returned context. Every namespace scope it creates under the combine scope is owned through `setParent` (the parent's `mChildren`). Every AST is owned by `ModuleRecord::module`. The context must be destroyed before `resolver`. *Test:* `project_compile` and `parser_no_leak` under `build-asan` (LSan).
- **I5 — Ordering.** Inputs are processed interfaces-first (stable), then in plan order. In combine mode the user input is the last one after the reorder. Prelude PROMOTE happens immediately after each provider's Sema. The import-diagnostics pass runs after all inputs and only when `combine || any kInterface input`.
- **I6 — Known U3 item (documented, not changed).** `.bmod` injection registers dependency namespaces on the **global** scope (adopted via `setParent(gScope)`), as today. U3, which feeds interfaces from long-lived blangd, must move them under a context-owned scope; see plan A3. No API change is needed for that.
- **I7 — `kNone`.** With `IdentityPolicy::kNone`, `run` performs no `realpath` and no `stampModuleDigest`. `stampDefiningOrigin( module, path )` still runs, as blangd does today.
- **I8 — No process, no network (amended, F3).** `run` performs only file reads for `!inMemory` inputs.
  - *Test:* the `blangd_no_subprocess` ctest runs `tools/check_no_subprocess.sh` against the built `blangd`. `nm -D --undefined-only` must list none of `popen system fork vfork execl execlp execle execv execvp execvpe execve fexecve posix_spawn posix_spawnp socket connect getaddrinfo`, and the binary must have a non-empty undefined-symbol list.
  - *Teeth:* `blangd_no_subprocess_teeth` runs the same script on `bcc` with `WILL_FAIL`.
  - This binary-level check replaces the former source grep.
- **I9 — Recorded-only downstream (F1).** `record.module` and `record.recorded` are set only at the recorded step, after injection. Sema, eager PROMOTE, the import-diagnostics pass, the prelude-shadow set, `bmodEmitScope` and `moduleIndices()` consume only recorded modules. A dropped input contributes exactly the one diagnostic its drop point emits today:
  - a malformed foreign-type header;
  - a catastrophic parse failure;
  - a `realpath` failure;
  - a wrong-format `.bmod`.

  Its `import` lines are never scanned. *Test:* `project_compile` C7 (a wrong-format `.bmod` containing `import` lines, checked against the qcc oracle).

## Callers in U1

| Caller | Plan |
|---|---|
| `qcc.cpp` | one `CompileInput` per positional argument, `kind = classifyInput(path)`, `origin = bmodOrigins[path]` for interfaces; `combine`, `moduleOrigin`, `lexerTrace = verbose && !dumpLocations`; `kCanonical` |
| `lsp/Compile.cpp` | one in-memory `kSource` input `{ path, text }`; `combine = false`; `kNone` |
| `ProjectCompileTest.cpp` | combine plans over `selectStdlibFiles(...)` + a fixture (C1–C4, and C7 with a wrong-format `.bmod`); one in-memory `kNone` plan (C6) |
