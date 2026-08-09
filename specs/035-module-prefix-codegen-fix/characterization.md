# U2 characterization + regression-lock: module-prefix string-ARC path

**Epic**: modules-v2-graph · **Unit**: U2 · **Covers**: REQ-005 (the "fix" half,
re-scoped) · **Design artifact** (Principle VI) · **Auditor**: signed off on the
re-scope from bug-fix → characterization + regression-lock (2026-08-09).

See also `docs/epics/modules-v2-graph/design-audit-U2.md` (the design audit + §8
spike findings). This document is the characterization artifact the re-scope
requires: it explains *why* the double-free is gone and *why the fix is
class-level*, and specifies the committed regression lock.

## 1. What U2 is (and is not)

The retired rationale at `qcc.cpp:303-307` (dated `2619191`, 2026-07-20) claimed a
namespaced module's internal string-returning call (`has_flag → flag_name_of`)
double-freed under the module-prefix codegen, and that is why `buffer`,
`collections`, and `cli` are promoted into the user scope instead of getting a
real module boundary.

The U2 spike (design-audit-U2 §8) proved the double-free **no longer reproduces**
on current `master`, across five patterns, two allocator regimes, and a byte-exact
golden. So U2 does not fix a live bug. It **characterizes** why the path is clean
and **locks it with a committed regression** so:

- a future change cannot silently reintroduce the double-free, and
- **U3 can safely demote `cli`** off the promotion list (F3 sequencing: the
  demotion may land only after this regression-lock merges).

## 2. Why the double-free is gone — and why the fix is class-level (robustness argument)

The ARC decision that governs a string return — "is the returned value an owned
temp the caller must release, or a borrow" — is driven by
`CodeGen::callReturnTypeName` (`CGTypes.cpp:1179-1199`), which reads the call's
**AST `FunctionDefinition` pointer** `call->mFunction` and its `getReturnType()`:

```cpp
std::string CodeGen::callReturnTypeName( CallExpression *call ) {
    if ( call == nullptr || call->mFunction == nullptr ) return "";
    FunctionDefinition *fd = call->mFunction;      // <-- AST pointer, not a symbol
    Type *rt = fd->getReturnType();
    ...
}
```

The module **prefix** is applied only when emitting the LLVM **symbol name**
(`CodeGen.cpp:157-158`: `llvmFuncName = mModulePrefix + "__" + func->getName()`).
It **never** enters the ARC decision: `call->mFunction` is the same AST pointer
whether or not the callee is prefixed, set at parse/sema independent of codegen
mangling.

**Consequence (the class-level argument):** the return-retain / temp-tracking
decision is *identical* on the prefixed and prefix-free paths. Therefore the
general return-retain/borrow ARC fixes that landed after 2026-07-20 — candidates
`cfb92d8` (UAF on borrowed struct returns from generic methods), `ca1f7c7`
(sync-struct init double-free + string-subject match UAF hardening), and the
return-retain borrow-rule series — repaired **every** string-return shape
uniformly, prefixed ones included. The prefix was never a distinct ARC code path;
it was only ever a distinct symbol string. This is why the promotion became
unnecessary the moment the general fix landed, and why no prefix-specific fix is
needed now.

## 3. The regression lock (committed)

**Module** `test_files/nsarc.b` — a namespaced module (its own namespace scope),
deliberately **kept out of every promotion list** (`buffer`/`collections`/`cli`),
so `qcc` assigns it a module **prefix** and emits its internal calls as
`@nsarc__key_of` / `@nsarc__lookup` / `@nsarc__value_of`. It mirrors `cli.b`'s
shapes: an internal string-returning helper (`key_of`, returns an owned substring
temp in one branch and a borrowed local in another), a `has_flag`-shape `pub`
consumer (`lookup`), and a `flag_value`-shape `pub` fn returning an owned string
across the prefix boundary (`value_of`).

**Driver** `test_files/codegen_nsprefix_arc.b` — `import nsarc;`, exercises the
full surface deterministically plus a 25-iteration loop stressing the
owned-string-return-across-prefix path; committed golden
`codegen_nsprefix_arc.expected.out`.

**Both teeth (auditor-required), wired in `test_codegen.sh`:**

1. **Prefixed-config assertion.** The harness combines `nsarc.b` as a namespaced
   module (content-gated on `import nsarc;`) and, after IR generation, **asserts
   `@nsarc__` mangled callees are present** (`need_nsarc`). If a regression ever
   promotes the module (prefix-free path), the assertion fails loudly instead of
   passing on the wrong config. Verified to bite: the prefix-free combination
   emits no `@nsarc__` and the assertion fails.
2. **build-asan fail-not-skip.** Under `--leak-check`, if the ASan-instrumented
   `build-asan/` archives are absent, the run is a **hard failure under `CI=true`**
   (KI-5 action 3 precedent) rather than a silent fallback to plain archives that
   would not trap a UAF-read inside runtime code; locally it degrades to a loud
   warning.

## 4. Gates (all green on the U2 branch)

- `./run_tests.sh` (LLVM) **239/0**; parse-only (`BUILD_DIR=build-nollvm`) **232/0**.
- `./test_codegen.sh` **166/0** (was 165/0; +1 fixture).
- `./test_codegen.sh --leak-check` **166/0, Leaks: 0** — the fixture is CLEAN in the
  prefixed config against the ASan-instrumented runtime.
- `./test_lsp.sh` **62/0**.
- Teeth verified: prefix-free config → no `@nsarc__` → assertion fails.

**"All green" means the full CI matrix, not just local.** The `--leak-check`
fail-not-skip flip is a HARD failure under `CI=true` when `build-asan` is absent, so
**every** CI job that runs `./test_codegen.sh --leak-check` must provision the
ASan-instrumented archives — not only the `sanitizers` job. There are **four** such
call sites: `sanitizers` (injected-leak probe + full run — already provisions
`build-asan`), plus `opt-suite` (`-O2` ARC leg) and `debug-suite` (`-g` ARC leg),
which this unit updated to add the same two `cmake -S . -B build-asan
-DBLANG_SANITIZE=address,undefined …` provisioning lines (@reviewer
CHANGES-REQUESTED on #145). A local `--leak-check` pass does not certify the matrix;
the claim is complete only when all four legs are green in CI.

## 5. Sequencing

F3 holds: **U3 must not demote `cli`** until this regression-lock merges. Its
content changed from "fix a double-free" to "prove the path clean and lock it," but
the gate ordering is unchanged.
