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
	Scope *scope = nullptr;          // owned by the CompileContext
	SmartPtr<Module> module;         // null on a failed parse
};

class CompileContext
{
public:
	const std::vector<ModuleRecord> &records() const;
	const ModuleRecord &record( std::size_t i ) const;
	// Indices (into records()) of the successfully parsed modules, in order.
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
- **I3 — Aliases.** On entry, `run` sets `gDiag = &diag`. On return, `gDiag` is restored to its entry value. `gScope` must already equal `resolver.globalScope()`; `run` asserts this in debug builds and never reassigns it.
- **I4 — Ownership (KG-7).** Every scope `run` obtains from `resolver.newModuleScope` is owned by the returned context. Every namespace scope it creates under the combine scope is owned through `setParent` (the parent's `mChildren`). Every AST is owned by `ModuleRecord::module`. The context must be destroyed before `resolver`. *Test:* `project_compile` and `parser_no_leak` under `build-asan` (LSan).
- **I5 — Ordering.** Inputs are processed interfaces-first (stable), then in plan order. In combine mode the user input is the last one after the reorder. Prelude PROMOTE happens immediately after each provider's Sema. The import-diagnostics pass runs after all inputs and only when `combine || any kInterface input`.
- **I6 — Known U3 item (documented, not changed).** `.bmod` injection registers dependency namespaces on the **global** scope (adopted via `setParent(gScope)`), as today. U3, which feeds interfaces from long-lived blangd, must move them under a context-owned scope; see plan A3. No API change is needed for that.
- **I7 — `kNone`.** With `IdentityPolicy::kNone`, `run` performs no `realpath` and no `stampModuleDigest`. `stampDefiningOrigin( module, path )` still runs, as blangd does today.
- **I8 — No process, no network.** `run` performs only file reads for `!inMemory` inputs. *Inspection:* there are no `popen`/`system`/`fork`/`exec`/socket calls in `ProjectCompile.cpp`.

## Callers in U1

| Caller | Plan |
|---|---|
| `qcc.cpp` | one `CompileInput` per positional argument, `kind = classifyInput(path)`, `origin = bmodOrigins[path]` for interfaces; `combine`, `moduleOrigin`, `lexerTrace = verbose && !dumpLocations`; `kCanonical` |
| `lsp/Compile.cpp` | one in-memory `kSource` input `{ path, text }`; `combine = false`; `kNone` |
| `ProjectCompileTest.cpp` | combine plans over `selectStdlibFiles(...)` + a fixture (C1–C4); one in-memory `kNone` plan (C6) |
