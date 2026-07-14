# Feature Specification: Sanitizer build (ASan/UBSan)

**Feature Branch**: `epic/test-validation/u3-sanitizers`

**Created**: 2026-07-14

**Status**: Draft

**Epic**: test-validation · **Unit**: U3 (this run's ordering) · **Covers**: REQ-004 (build portion)

**Input**: Add a `BLANG_SANITIZE` CMake option that builds the compiler and C
runtime with `-fsanitize=<list>` (address, undefined), and make
`BUILD_DIR=build-asan ./run_tests.sh` pass clean. Leak-check teeth + the
injected-leak fixture are U4's scope; CI wiring is U7's.

## Overview & Problem

REQ-004 requires a sanitizer (ASan+UBSan) build of the compiler that passes the
parse suite clean, plus a leak leg. Today there is **no** sanitizer build option
at all. This unit adds the opt-in `BLANG_SANITIZE` build option and proves the
ASan/UBSan build is clean across the parse+codegen suites — the foundation the
U4 leak-check leg and U7 CI job build on. It deliberately does **not** change
`--leak-check` teeth (U4) or add CI jobs (U7).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Opt-in sanitizer build of the compiler + runtime (Priority: P1)

**Why this priority**: REQ-004's foundation — without a sanitizer build there is
no memory-safety verification of the compiler/runtime.

**Independent Test**: `cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined
&& cmake --build build-asan` builds with no errors.

**Acceptance Scenarios**:

1. **Given** a clean tree, **When** `cmake -S . -B build-asan
   -DBLANG_SANITIZE=address,undefined && cmake --build build-asan -j"$(nproc)"`
   runs, **Then** it configures and builds with no build errors, and the
   sanitizer flags are present on both compile and link lines for the compiler
   and the C runtime libraries.
2. **Given** `BLANG_SANITIZE` is empty/unset (default), **When** the normal build
   configures, **Then** no `-fsanitize` flag is added — the default build and its
   artifacts are unchanged.

---

### User Story 2 - The sanitizer build runs the suites clean (Priority: P1)

**Why this priority**: A sanitizer build that isn't exercised proves nothing.

**Independent Test**: `BUILD_DIR=build-asan ./run_tests.sh` exits 0.

**Acceptance Scenarios**:

1. **Given** `build-asan` built with address,undefined, **When**
   `BUILD_DIR=build-asan ./run_tests.sh` runs, **Then** it exits 0 with no ASan
   memory-error or UBSan diagnostic causing a failure (parse+sema+cgfail suite).
2. **Given** a real memory error or UB is present in compiler/runtime code,
   **When** the sanitizer build runs the suite, **Then** it is caught (non-zero) —
   i.e. the instrumentation has teeth (proven at audit by a temporary injected
   fault). Any real fault surfaced is fixed in scope or raised as an Open Question.

---

### Edge Cases

- **LLVM allocations / LeakSanitizer noise**: `run_tests.sh` runs `qcc`, which
  links LLVM; LLVM (and the RefCount AST) do not free everything at exit, so
  LeakSanitizer would report many "leaks" unrelated to correctness. Since
  `run_tests.sh` is a **parse/sema correctness** gate (leak-checking is
  `test_codegen.sh --leak-check`, U4), the sanitizer run disables leak detection
  here (`ASAN_OPTIONS=detect_leaks=0`) while keeping memory-error + UB detection
  fatal. This is scoping, not masking — leaks are U4's dedicated gate.
- **Partially-instrumented LLVM static libs**: linking an ASan/UBSan executable
  against non-instrumented LLVM archives is supported; the ASan runtime
  intercepts globally. If ODR false positives appear, `detect_odr_violation=0` is
  the documented mitigation.
- **UBSan default is non-fatal**: plain UBSan prints and continues (exit 0), which
  would give no teeth. The build makes UBSan fatal (`-fno-sanitize-recover` and/or
  `UBSAN_OPTIONS=halt_on_error=1`) so injected UB actually fails the run.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: A `BLANG_SANITIZE` CMake **cache STRING** option MUST exist,
  defaulting to empty (off). When non-empty (e.g. `address,undefined`), it MUST
  add `-fsanitize=<list>` to **both** compile and link flags for the compiler
  (`qcc`, and `bcc`/lexer targets built in the same tree) and the C runtime
  libraries.
- **FR-002**: With `BLANG_SANITIZE` empty/unset, the build MUST be byte-for-byte
  equivalent to today's (no `-fsanitize`), so the default build and its artifacts
  are unchanged.
- **FR-003**: `cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined &&
  cmake --build build-asan` MUST build **clean** (no build errors).
- **FR-004**: `BUILD_DIR=build-asan ./run_tests.sh` MUST exit 0.
- **FR-005**: The instrumentation MUST have teeth: a deliberately-injected memory
  error or UB MUST cause the sanitizer run to fail (non-zero) — demonstrated at
  audit and reverted. UBSan MUST be configured fatal (not the non-aborting
  default).
- **FR-006**: `run_tests.sh` MUST set safe default sanitizer options (leak
  detection off, memory-error/UB fatal) in a way that is a **no-op for
  non-sanitizer builds** (env vars ignored by uninstrumented binaries). It MUST
  NOT change `run_tests.sh`'s behavior or output for the normal/parse-only builds.
- **FR-007**: Out of scope (do NOT implement here): `--leak-check` teeth and the
  injected-leak fixture (U4); CI jobs (U7). If a real leak is observed it is
  noted for U4, not fixed by weakening anything here.
- **FR-008**: If ASan/UBSan surfaces a **real** bug in compiler/runtime code, it
  is fixed in scope; a large/risky fix is raised as an Open Question rather than
  guessed.

### Key Entities

- **`BLANG_SANITIZE`**: CMake cache STRING; comma list of sanitizers; empty = off.
- **`build-asan`**: an out-of-source build dir configured with the option on.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: `cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined &&
  cmake --build build-asan -j"$(nproc)"` completes with exit 0 (clean build).
- **SC-002**: `BUILD_DIR=build-asan ./run_tests.sh` exits 0.
- **SC-003**: The default build (`build/`) and `./test_codegen.sh` remain green
  at the unit boundary (186 LLVM, 181 parse-only, 63/63 codegen); default build
  artifacts unchanged (no `-fsanitize` in the default build's flags).
- **SC-004**: Teeth — a temporary injected heap error or UB in an instrumented
  source makes `BUILD_DIR=build-asan ./run_tests.sh` (or a targeted sanitizer
  run) fail non-zero; reverting restores green. Verified at audit.
- **SC-005**: `-fsanitize=address,undefined` is present on the compile and link
  lines of `build-asan` (grep of the CMake build commands / `flags.make`).

## Assumptions

- Clang or GCC with ASan/UBSan is available (the repo already uses clang-18/gcc;
  `install_deps` provides them). The sanitizer build targets Linux (per Non-goals:
  Windows excluded).
- Disabling LeakSanitizer for `run_tests.sh` is correct scoping: `run_tests.sh`
  validates parse/sema correctness; memory-leak verification is the dedicated
  `test_codegen.sh --leak-check` gate (U4) on the deterministic codegen suite.
- The option plumbs flags globally via `add_compile_options`/`add_link_options`
  placed before target definitions, so all targets (compiler + runtime) are
  instrumented uniformly.
- Scope: build option + green sanitizer run only; leak teeth (U4) and CI (U7)
  are separate units.
