# Design audit — U2: Module-prefix string-ARC codegen fix (crux; spike-first)

**Epic**: modules-v2-graph · **Unit**: U2 · **Speckit**: `035-module-prefix-codegen-fix`
**Principle VI design-audit gate + security dimension MANDATORY (ARC/double-free).**
**Reviewed by @auditor BEFORE implementation.**
**Covers**: REQ-005 (the fix half) · **Unblocks**: U3 (`cli` demotion) · **Closes (with U3/U6)**: KI-3

---

## 1. Problem this unit solves

The driver promotes `buffer`/`collections`/`cli` into the **user's own scope**
(`qcc.cpp:308-313`) instead of a namespace scope. The rationale is recorded
verbatim at `qcc.cpp:303-307`:

> A namespaced module's internal string-returning calls (`has_flag → flag_name_of`)
> hit a **string-ARC double-free** under the module-prefix codegen; global modules
> (collections' Map methods calling each other) are clean.

So the promotion list is a **workaround for a codegen bug**, not a design choice.
Review finding **F3** fixes the sequencing: the double-free must be fixed
(**U2**) **before** `cli` is demoted to a namespaced module (**U3**) — demoting
first would ship the known double-free. This unit fixes the root cause and
**retires no promotion** (that is U3).

## 2. The suspected mechanism (to be confirmed by the spike, not assumed)

When a module is compiled with a non-empty `mModulePrefix`, its functions and
methods are emitted with mangled names (`CodeGen.cpp:157-158`, `185-188`,
`539-543`): `cli__has_flag`, `cli__flag_name_of`. Internal call `has_flag →
flag_name_of` returns a `string` (from `substring`, a freshly-owned temp).

**Working hypothesis** (three candidate root causes the spike will discriminate):

- **(H1) Owned-string temp double-counted on the prefixed path** (re-worded per
  auditor). The return-type ARC decision reads the **AST `call->mFunction` pointer
  directly** — it does **not** do a mangled-symbol lookup — so the question is not
  "does the prefix break symbol resolution" but **"is the owned `string` temp
  returned by the prefixed internal call counted more than once"** (retained on
  bind *and* released as a temp, or released by two different scope-cleanup paths)
  → over-release / double-free at scope exit. **Step 2 must first verify
  `call->mFunction` is actually populated on the prefixed internal-call path** — if
  it is (as expected, since the AST pointer is set at parse/sema regardless of
  codegen name mangling), H1 is purely an ARC-accounting imbalance and the fix
  stays inside the return/temp-tracking path.
- **(H2) Double bind-retain + temp-release.** The temp is both retained on bind
  (borrowed-source rule) and released as a temp, unbalanced only on the prefixed
  path.
- **(H3) Deeper — mangling identity.** If the defect is that the prefix makes the
  callee un-findable in `mFunctionMap`/`mModule` and the real fix touches mangling
  identity, that overlaps U1. **Per the workplan: if the root cause is deeper than
  the ARC path (touches mangling), STOP and raise before widening scope.**

The spike's first job is to **localize which of H1/H2/H3 is real** under ASan.

## 3. Spike plan (spike-first, `--leak-check`/ASan proof required)

**Step 0 — baseline.** Confirm the current suite is green and that the promotions
currently *mask* the bug (removing a promotion reproduces it). Record the exact
green baseline (run_tests 239/0 + 232/0, test_codegen 165/0, leak 0).

**Step 1 — minimal reproduction.** Author a **namespaced** module fixture (its own
namespace scope, i.e. NOT promoted) whose `pub` function makes an **internal
string-returning call** — mirroring `has_flag → flag_name_of`:

```blang
// cliish.b  (compiled with module prefix "cliish", namespace scope)
fn name_of(string a) -> string {
    if a.starts_with("--") { return a.substring(2, a.length); }
    return "";
}
pub fn has(Array<string> args, string want) -> bool {
    for x in args { if name_of(x) == want { return true; } }
    return false;
}
```

Drive it through `qcc --combine` with the module in a namespace scope + a `main.b`
consumer, compile, run under `test_codegen.sh --leak-check` (linking the
**ASan-instrumented runtime** from `build-asan/` so a use-after-free *inside*
`__blang_rc_release`/`retain` is trapped, not just leaks). **Expect: red**
(double-free / heap-use-after-free reported at the string free path).

**Step 2 — diagnose.** First **verify `call->mFunction` is populated** on the
prefixed internal-call node (rules H1 in/out at the AST level, per the auditor
re-wording). Then, with ASan's allocation/free stacks, confirm which of H1/H2/H3
holds; capture the before-trace in the spike write-up.

**Step 3 — fix the root cause.** Repair the ARC accounting on the prefixed
string-returning call path (expected: the return-type/temp-tracking resolution
must see through the module prefix so a prefixed callee's owned-string return is
tracked exactly once). Keep the change **confined to the ARC/return-tracking
path**; do **not** alter ARC *semantics* (Principle IV, epic constraint) — this
repairs an existing double-free, it does not change the retain/release contract.

**Step 4 — prove.** Same fixture now `--leak-check` **clean**; full suite green in
both build modes. Commit the before/after ASan traces in the spike write-up under
`specs/035-…`.

## 4. Security dimension (mandatory — Quality Gate 7)

- The defect **is** the security dimension: an unbalanced free is a
  double-free/UAF at a trust boundary (the C string runtime). The fix is judged on
  memory-safety: no over-release, no leak, no type confusion at the C boundary.
- `--leak-check` with the **ASan-instrumented runtime archive** is the gate
  (plain archives only intercept malloc/free — the epic requires the instrumented
  runtime so UAF *reads inside* runtime code are trapped).
- The reviewer records the security pass explicitly.

## 5. What U2 does NOT do (scope discipline)

- Does **not** retire any promotion (`buffer`/`collections`/`cli` stay promoted
  through U2) — that is **U3**, and only *after* this lands with proof.
- Does **not** touch the prelude manifest, type tiers, `addType` deletions (U3),
  the module graph (U4), or `.bmod` format (U5).
- Does **not** change ARC/ownership semantics or concurrency — repairs one
  double-free only.
- If the root cause proves to be mangling-identity-deep (H3), **stop and raise**;
  do not absorb U1's mangling work.

## 6. Done condition & evidence

- A `test_codegen`/`test_build` fixture exercising a namespaced module's internal
  string-returning path is **`--leak-check` clean** (double-freed before the fix,
  proven by the Step-1 red run).
- **MANDATORY (not optional): the `build-asan/` instrumented-runtime archive
  provisioning must FAIL, not SKIP, when absent** in the local gate and the CI
  `sanitizers`/`build-system` legs (KI-5 action 3 precedent). A leak-check leg that
  silently skips would report green over an unrun check — that is a done-condition
  failure for U2, not a warning.
- Spike write-up (repro + ASan before/after) committed under `specs/035-…`.
- No regression; full gates green in both build modes; `test_lsp.sh` green.

## 8. SPIKE FINDINGS (2026-08-09) — the double-free does NOT reproduce on current master

**Result: the string-ARC double-free that forced the `buffer`/`collections`/`cli`
promotions no longer reproduces.** The demotion codegen path (a namespaced,
module-prefixed module making internal string-returning calls) is `--leak-check`
clean on current `master`. This inverts U2's premise and is raised to the manager
for a ruling before any code lands.

### Method (faithful reproduction of the demotion path)

`cli` is currently *promoted* (parsed into `combineScope`, **no** module prefix),
so it never exercises the buggy config. To exercise exactly what **U3's demotion**
would produce, I copied `stdlib/cli.b` verbatim to `cliish.b` (a name **not** in
`isGlobalTypeLib`), so `qcc --combine cliish.b main.b` puts `cliish` in a
**namespace scope with prefix `cliish`** — confirmed by emitted IR
(`@cliish__flag_name_of`, `@cliish__flag_value`). Linked against the **ASan+UBSan+LSan
instrumented `build-asan/` runtime** (`-fsanitize=address,undefined,leak`,
`ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS=exitcode=23`).

### Cases exercised — all clean (exit 0, no ASan report, no leak)

1. `cliish.has_flag → flag_name_of → substring` (the **exact** pair the rationale
   names), in a 50-iteration loop. Clean.
2. The **full `codegen_cli.b` surface** (`has_flag`/`flag_value`/`bool_flag`/
   `positionals`), qualified through the prefix — output matches the committed
   golden byte-for-byte. Clean.
3. Namespaced fn returning an owned `string` **across the prefix boundary**, bound
   in user code and dropped in a loop. Clean.
4. Non-ASan build under `MALLOC_PERTURB_=42 MALLOC_CHECK_=3` (a double-free aborts
   loudly). exit 0.
5. Aggressive: a namespaced fn returning a **concatenated** (fresh-temp) string
   built via an internal string-returning call (`label_of`), bound & dropped in a
   loop plus a final return. ASan+leak clean.

### Interpretation

The rationale comment dates to `2619191` (2026-07-20). Multiple ARC return/borrow
fixes landed **after** it — candidates: `cfb92d8` (UAF on borrowed struct returns
from generic methods), `ca1f7c7` (sync-struct init double-free / string-subject
match UAF hardening), and the return-retain borrow-rule series. One of these
almost certainly fixed the root cause; the promotion was simply never retired, and
the committed `codegen_cli.b` test only ever exercised the **promoted** (prefix-
free) path — so **no test has covered the demoted path since the fix**. This spike
is the first.

**Not yet done:** a git-bisect to name the exact fixing commit (needs rebuilding
`qcc` at historical commits — offered to the manager, not run yet). Current
evidence (5 patterns, two allocator regimes, byte-exact golden) is strong that the
path is genuinely clean, not luck-masked.

### Proposed revised U2 shape (for manager/@auditor ruling)

U2 cannot "land a fix" for a bug that no longer reproduces. Proposed: **re-scope U2
from bug-fix to characterization + regression-lock**, still satisfying REQ-005's
intent (the demotion path is proven clean) and unblocking U3 under F3:

- Commit a **permanent regression fixture** = a namespaced module with internal
  string-returning calls (the `cliish`/`extra` shapes above), added to the codegen
  matrix with a committed golden and run under `--leak-check`, so a future
  regression that reintroduces the double-free goes red.
- Keep the **mandatory build-asan fail-not-skip** provisioning (§6) — without it
  the regression-lock is toothless.
- Optionally bisect to name the fixing commit for the record.
- **F3 is still honored**: U3 may demote `cli` only *after* U2's fixture proves the
  path clean and merges — the sequencing gate holds, its content changes from
  "fix" to "prove + lock."

This is a **STOP-AND-RAISE** per the manager's directive (the finding changes the
unit's premise), not a scope change I take unilaterally. Held for ruling.

## 7. Open items for the auditor to rule on

1. **Fixture placement**: `test_codegen` single-file combine vs a `test_build`
   lib+bin pair. Recommendation: a `test_codegen` combine fixture for the tight
   repro (fast, ASan-instrumented) **plus** the eventual U3/U6 `test_build`
   coverage — U2 owns the combine repro.
2. **H3 escalation trigger**: agree the precise line — "if the fix requires
   changing `mangleGenericName` or the symbol→FunctionDefinition mapping, it is
   U1-adjacent; stop and raise." Confirm this is the right stop-condition.
3. Confirm the `build-asan/` archive provisioning is present in the local gate and
   CI `sanitizers`/`build-system` legs so the leak-check leg **fails rather than
   skips** when archives are absent (KI-5 action 3 precedent).
