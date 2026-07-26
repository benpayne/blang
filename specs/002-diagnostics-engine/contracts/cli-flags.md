# Contract: CLI Flags (`-v`, `--debug-compiler`)

Two new flags on `qcc`. Neither changes which programs compile (FR-016).

## `-v` / `--verbose`

- **Default (absent)**: developer-facing output is suppressed. A clean compile
  is byte-silent on stdout **and** stderr (FR-007). Specifically suppressed:
  - the `Completed parse` line (`qcc.cpp:717`),
  - the lexer per-token `Symbol …` trace (`FileLexer.cpp:315-321`, via
    `setTraceEnabled(false)`),
  - any AST/token dump.
- **Present**: the above output is emitted (FR-008). Exit code is unaffected.
- Does **not** gate `--dump-locations` output (that mode prints its node dump
  regardless — FR-009) and does not gate error diagnostics (errors always
  print).

## `--debug-compiler`

- **Default (absent)**: no compiler-internal detail in any output (FR-003).
- **Present**: adds the C++ throw-site `file:line` for a `CompileError` and,
  on the LLVM verifier path, the raw verifier text (see
  `diagnostic-format.md`). Independent of `-v`.

## Interaction / precedence

- `-v` and `--debug-compiler` are independent booleans; either, both, or
  neither may be set.
- Error diagnostics print in all modes; `-v`/`--debug-compiler` only add
  extra output, never remove the canonical error line.
- `--dump-locations` (U1) is unchanged and orthogonal.
- `bcc` invokes `qcc` without `-v`/`--debug-compiler`, so the pipeline stays
  quiet (its stderr grep-filter becomes unnecessary; its removal is optional
  in U2).

## Help text

`qcc --help` lists both new flags with one-line descriptions. `CLAUDE.md` CLI
features list is updated to mention them and the new error format.

## Verification hooks

- Gate D: `out=$(build/qcc --parse-only test_files/pass/func_simple.b 2>&1);
  test -z "$out"` (silence by default).
- `-v` on the same file emits `Completed parse` (and trace) and still exits 0.
