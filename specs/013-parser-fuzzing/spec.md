# Feature Specification: Bounded parser fuzzing (libFuzzer)

**Feature Branch**: `epic/test-validation/u5-parser-fuzzing`

**Created**: 2026-07-14

**Status**: Draft

**Epic**: test-validation · **Unit**: U5 (this run) · **Covers**: REQ-006

**Input**: Add a libFuzzer target `fuzz_parse` driving the lexer+parser on
arbitrary bytes, a seed corpus (≥20) from pass/fail inputs, a crash-free corpus
replay, and a poison-input teeth proof that the target provably reaches the
parser. CI runs a fixed-budget campaign (U7 wires it). Bounded — no open-ended
bug hunt.

## Overview & Problem

The hand-written recursive-descent lexer/parser has **no fuzzing**. This unit
adds a bounded libFuzzer harness that feeds arbitrary bytes through the exact
`LexerReader → Lexer → Module::Parse` path `qcc` uses, seeded from existing
pass/fail programs, with a committed corpus that replays crash-free and a
poison-input proof that the harness actually reaches the parser (so a stub target
can't pass). Any crash it surfaces is fixed in scope or escalated (Open Question)
— the campaign is fixed-budget (`-max_total_time=60`), not open-ended.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A libFuzzer target exercises the parser (Priority: P1)

**Why**: REQ-006 — the parser must be fuzzed for crashes/UB on arbitrary input.

**Independent Test**: `fuzz_parse` builds; running it on the seed corpus with
`-runs=0` replays every input crash-free (exit 0).

**Acceptance Scenarios**:

1. **Given** a clang build with the fuzz option on, **When** `fuzz_parse` is
   built, **Then** it links a standard libFuzzer entry (`LLVMFuzzerTestOneInput`)
   that drives `Module::Parse` on the input bytes.
2. **Given** the committed corpus (≥20), **When** `fuzz_parse
   test_files/fuzz/corpus/ -runs=0` runs, **Then** it exits 0 (no crash on any
   corpus input).

---

### User Story 2 - The harness provably reaches the parser (Priority: P1)

**Why**: A fuzz target that doesn't actually parse the input is a vacuous gate.

**Independent Test**: With a deliberately-broken parser, a committed poison input
crashes `fuzz_parse`; with the real parser it does not.

**Acceptance Scenarios**:

1. **Given** a temporary deliberate break in a parser code path (e.g. `abort()`
   when the function name equals a canary the poison input defines), **When**
   `fuzz_parse <poison>` runs, **Then** it crashes (libFuzzer reports the fault) —
   proving the poison bytes flowed lexer→parser. Reverting the break makes the
   poison parse cleanly. Demonstrated in the PR.

---

### User Story 3 - Bounded campaign + crash handling (Priority: P2)

**Why**: The done condition needs a finite target; crashes must be triaged.

**Independent Test**: A `-max_total_time=60` run completes; any crash found is
committed as a regression input and fixed or escalated.

**Acceptance Scenarios**:

1. **Given** the fuzz target, **When** a `-max_total_time=60` campaign runs (U7
   CI leg), **Then** it completes within budget.
2. **Given** the campaign surfaces a crash, **When** triaged, **Then** the
   crashing input is committed under `test_files/fuzz/corpus/` as a regression
   case and the bug is fixed in scope (or escalated as an Open Question if
   large/risky); the corpus then replays crash-free.

---

### Edge Cases

- **Compiler**: libFuzzer requires clang; the target is built only when
  `BLANG_FUZZ=ON` and the compiler is clang. gcc builds simply don't define it.
- **LLVM**: the parser needs no LLVM; the fuzz build is configured
  `BLANG_ENABLE_LLVM=OFF` so `fuzz_parse` links only parse-only sources (no
  CodeGen), keeping it lean and clang-buildable.
- **Per-iteration leaks**: `Module`/`Scope` are RefCount-managed; the harness
  holds them in `SmartPtr` so each parse frees its AST — corpus replay under
  libFuzzer's leak detection stays clean.
- **Controlled parse errors are not crashes**: `Module::Parse` reports located
  errors and returns null on bad input; only a signal/ASan/UBSan fault is a
  libFuzzer crash. Diagnostics are silenced (`stderr`→/dev/null) for speed.
- **Build dir path**: libFuzzer needs clang, so `fuzz_parse` cannot live in the
  gcc default `build/`. Its canonical home is a dedicated clang build dir
  `build-fuzz` (mirroring `build-asan`). The done-condition's `build/fuzz_parse`
  denotes this clang fuzz build; the runnable acceptance path is aligned to
  `build-fuzz/fuzz_parse` (documented, teeth-preserving).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Add a `BLANG_FUZZ` CMake option (OFF by default). When ON, require
  clang (fail configure otherwise) and define a `fuzz_parse` executable built
  from the parse-only compiler sources (`qcc.cpp` with `main` compiled out via a
  `BLANG_FUZZ_HARNESS` define, plus the `Q*.cpp`/lexer/Sema/diagnostics sources)
  and `fuzz/fuzz_parse.cpp`, compiled+linked with `-fsanitize=fuzzer,address`.
- **FR-002**: `fuzz/fuzz_parse.cpp` provides `LLVMFuzzerTestOneInput`, which
  initializes `gScope` (builtin types) + `gDiag` once, writes the input bytes to
  a temp file, and drives `LexerReader → Lexer → Module::Parse` — the same path
  `qcc` uses. It holds the `Module`/`Scope` in `SmartPtr` (leak-clean) and
  swallows only controlled `CompileError` (parse errors), never masking faults.
- **FR-003**: Guard `qcc.cpp`'s `int main` in `#ifndef BLANG_FUZZ_HARNESS` so the
  fuzz target reuses `Module::Parse` etc. without a duplicate `main`. Normal
  `qcc`/`bcc` builds are byte-for-byte unchanged (the guard is a no-op when the
  define is absent).
- **FR-004**: Commit a seed corpus at `test_files/fuzz/corpus/` derived from
  `test_files/pass` + `test_files/fail`, with `ls test_files/fuzz/corpus | wc -l`
  ≥ 20.
- **FR-005**: `build-fuzz/fuzz_parse test_files/fuzz/corpus/ -runs=0` exits 0
  (crash-free replay of the whole corpus).
- **FR-006**: TEETH — a committed poison input (`test_files/fuzz/poison.b`)
  crashes a deliberately-broken parser and parses cleanly with the real parser;
  the break→crash→revert→clean cycle is demonstrated in the PR (proves the
  harness reaches the parser).
- **FR-007**: The target supports a fixed-budget campaign
  (`fuzz_parse ... -max_total_time=60`) for U7's CI leg. Any crash surfaced is
  committed as a regression corpus input and fixed in scope, or escalated as an
  Open Question if large/risky.
- **FR-008**: The default build and existing suites stay green at the unit
  boundary; the fuzz target is opt-in and does not perturb `build`/`build-asan`.
- **FR-009**: `build-fuzz/` is git-ignored. The runnable fuzz acceptance path in
  `evaluation.md` is aligned to the clang fuzz build (documented correction,
  teeth intact — corpus replay + poison proof unchanged).

### Key Entities

- **`fuzz_parse`**: libFuzzer executable over the lexer+parser.
- **`test_files/fuzz/corpus/`**: ≥20 seed inputs (from pass/fail).
- **`test_files/fuzz/poison.b`**: teeth artifact (crashes a broken parser).
- **`build-fuzz`**: clang build dir (`BLANG_FUZZ=ON`, `BLANG_ENABLE_LLVM=OFF`).

## Success Criteria *(mandatory)*

- **SC-001**: `fuzz_parse` builds (clang, `BLANG_FUZZ=ON`).
- **SC-002**: `ls test_files/fuzz/corpus | wc -l` ≥ 20.
- **SC-003**: `build-fuzz/fuzz_parse test_files/fuzz/corpus/ -runs=0` exits 0.
- **SC-004**: Poison-input teeth: break parser → `fuzz_parse <poison>` crashes;
  revert → poison parses cleanly (shown in PR).
- **SC-005**: A `-max_total_time=60` campaign completes (bounded); crashes (if
  any) committed + fixed/escalated.
- **SC-006**: Unit boundary green — `./run_tests.sh` 186, `BUILD_DIR=build-parse
  ./run_tests.sh` 181, `./test_codegen.sh` 63/63, `BUILD_DIR=build-asan
  ./run_tests.sh` 186; default build unchanged.

## Assumptions

- clang + libFuzzer available (verified: clang 14 links `-fsanitize=fuzzer`).
- The parse-only source set (no CodeGen) is sufficient to fuzz the parser; Sema
  is out of the fuzz path (target is the parser, per REQ-006).
- The done-condition's `build/fuzz_parse` denotes the clang fuzz build; since a
  libFuzzer target cannot exist in the gcc default `build/`, `build-fuzz` is the
  canonical dir and the runnable acceptance is aligned to it — the same
  established pattern as `build-asan`.
- Bounded per Non-goals: build + fixed campaign + fix-or-escalate; no open-ended
  latent-bug mandate.
