# Contract: `StdlibSelect.h` (no parser, no LLVM; linked by `bcc` and the frontend)

```cpp
#ifndef BLANG_STDLIB_SELECT_H_
#define BLANG_STDLIB_SELECT_H_

#include <set>
#include <string>
#include <vector>

namespace BlangStdlib
{

// Imported module names in `sourceText`, by bcc's historical parseImports rule,
// copied VERBATIM (F5). For each std::getline line:
//   1. truncate at the first "//" (even one inside a string literal);
//   2. skip while isspace( (unsigned char)c );
//   3. require line.compare( i, 7, "import " ) == 0 — a literal "import" followed by
//      exactly ONE ASCII space (a tab after `import` does NOT match);
//   4. skip while isspace( (unsigned char)c );
//   5. take the maximal isalnum(c) || c == '_' run; insert it if non-empty.
// Block comments are NOT recognized. Only the leading identifier of a dotted path
// is taken (`import env.x;` -> "env").
std::set<std::string> scanImports( const std::string &sourceText );

// The stdlib .b files to combine, in dependency-safe order:
//   1. sys, buffer, collections, fs, net              (unconditional)
//   2. timer, math, time, random, env, cli            (each iff imported)
//   3. every other imported name, in std::set order   (iff imported)
// Each candidate is `stdlibDir + "/" + name + ".b"` and is included only if
// access(candidate, F_OK) == 0. A name is considered at most once.
std::vector<std::string> selectStdlibFiles( const std::string &stdlibDir,
	const std::set<std::string> &imports );

} // namespace BlangStdlib

#endif
```

## Invariants

- **S1 — Byte-identical to `bcc` at `2f68efe`.** `scanImports( readFileToString( p ) )` equals the old `parseImports( p )` for every file. `selectStdlibFiles( exeDir + "/stdlib", I )` equals the old `resolveStdlibFiles( exeDir, I )` element-for-element, as strings. The path strings must be identical (research R5). *Test:* `project_compile` C5, plus `test_build/run_build_tests.sh` and `bcc test` / `bcc migrate` legs (unchanged outputs).
- **S2 — Pure.** No output, no global state, no process. Its only system call is `access(F_OK)`.
- **S3 — Single implementation.** After U1, `bcc.cpp` has no `resolveStdlibFiles` and no `parseImports` (neither forward declarations nor bodies). All four call sites call `BlangStdlib::` (F4 labels):
  - `compileAndRunTestFile` (L571, `bcc test`);
  - `runMigrate` (L798–L800, `bcc migrate`);
  - `buildProject` (L1336–L1339, `bcc build`);
  - `main`'s single-file compile (L1698, `bcc file.b`).

  The two union loops (migrate, build) stay in `bcc` unchanged.
- **S4 — Consumed by blangd from U2.** blangd will call `selectStdlibFiles( <discovered stdlib dir>, scanImports( bufferText ) )`, which is the same rule over the unsaved buffer. U1 adds no blangd caller (single-file mode).
