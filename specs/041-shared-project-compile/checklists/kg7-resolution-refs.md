# Design note — non-owning expression→definition resolution links (U1, KG-7 completion)

**Status**: implemented in commit `9ea8214` on `041-shared-project-compile`. Approved by the manager as behavior-neutral. Flagged for the independent reviewer.

## Problem

KG-7 (`efb099a`) fixed **scope** ownership: a parent scope owns its children, and `mParent` is non-owning. It did not cover the links from an *expression* to the *definition it resolved to*. Four of these were owning `SmartPtr`s:

| Member | Target |
|---|---|
| `CallExpression::mFunction` | `FunctionDefinition` |
| `FunctionRefExpression::mFunction` | `FunctionDefinition` |
| `ConstructExpression::mStructDef` | `StructDefinition` |
| `EnumConstructExpression::mEnumDef` | `EnumDefinition` |

A definition owns its body. When the body refers back to the definition, the result is a refcount cycle, and the whole module AST is never freed. Two common cases trigger it:
- a recursive function, whose body calls itself;
- a method that constructs its own struct, such as `Buffer.slice` in `stdlib/buffer.b`.

Separately, `CallExpression::Parse` allocated a node and then hit a speculative `return nullptr` (the backtrack when the name is not followed by `(`, e.g. a function name used as a value), leaking that node.

## Why U1 needs it

SC-004 and FR-019 require the `project_compile` ctest to be LSan-clean while it compiles the **base stdlib**. Without this fix, compiling `buffer.b` alone leaks 70,000 bytes, so SC-004 cannot be met. U2 adds a ≥ 50-reparse stdlib-context loop to `parser_no_leak` (workplan U2 (d)). It would leak about 70 KB per reparse without this fix.

## Invariant

The links are now raw, non-owning pointers.

**A resolution link never owns its target.** The target is always owned by the scope it was looked up in, and by that module's definition lists. Every writer of these four members is a parser constructor whose argument comes from `scope->findSymbol`, the struct being parsed, or the namespace being searched. This was checked by grepping for every assignment and every `new CallExpression`, `new ConstructExpression`, `new FunctionRefExpression` and `new EnumConstructExpression`; the parser is the only creator.

Each owning scope lives at least as long as the AST that refers into it:
- a module scope is owned by the `CompileContext` (or `CompileResult`);
- a builtin is owned by the `Resolver`'s global scope, which outlives the context.

On backtrack, `CallExpression::Parse` adopts the speculative node into a local `SmartPtr` so that it is released.

## Evidence

**The leak exists in the pre-unit compiler.** The pre-unit binary is `/tmp/u1-base/build-asan/qcc` at `epic/modules-v2-lsp` @ `2f68efe`. Each command below was run with `ASAN_OPTIONS=detect_leaks=1 … --parse-only`.

| Input | Pre-unit | Post-unit |
|---|---|---|
| `--combine stdlib/buffer.b dropped_user.b` | 70000 B / 874 allocs | clean |
| `--combine stdlib/fs.b dropped_user.b` | 4928 B | clean |
| `--combine stdlib/net.b dropped_user.b` | 7342 B | clean |
| method that constructs its own struct | 3999 B (post-extraction, pre-fix) | clean |
| recursive `fn f` | 2928 B (post-extraction, pre-fix) | clean |
| the C8 program | 6967 B / 58 allocs | clean |

**Neutrality**, measured on the final tree with both hunks applied:
- 2279 diagnostic runs and 596 IR/`.bmod`/xmodule runs against the pre-unit `qcc`: zero diffs;
- zero tracked golden edits;
- `run_tests.sh` (×3), `test_codegen.sh` with `--leak-check` (Leaks: 0) and `--selfcheck`, `run_build_tests.sh`, ctest 85/85 ×3 and `test_lsp.sh` 70/0: all green.

**No new use-after-free.**
- `BUILD_DIR=build-asan ./run_tests.sh` passes 251/0.
- The ASan `qcc` runs combine IR generation over 178 programs with no sanitizer report. Its output is identical to the plain build, except `codegen_selector.b`. That file's lambda-capture order differs between the ASan and plain builds, and the pre-unit compiler shows exactly the same difference.
- The leak reproductions, including the `apply(f, 2)` backtrack path, run clean under ASan.

## Test case C8 (justification for the case beyond C1–C7)

`project_compile` C8 compiles, in memory, a recursive function, a method that constructs its own struct, an enum construct, and a function reference passed as an argument (the backtrack path). Under `build-asan`, LeakSanitizer fails the process if any of these cycles or leaks returns. It is the regression lock for this note. C1–C7 would catch only the `buffer.b` case, and only indirectly.
