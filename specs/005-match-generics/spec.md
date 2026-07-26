# Feature Specification: Match & Generics Soundness (U5)

**Branch**: `epic/blang-ast/u5-match-generics` — Unit U5, covers **REQ-007**, **REQ-008**.

## Scope delivered
Adds two soundness checks to the Sema pass (all build modes):
- **FR-001 (REQ-007) Match exhaustiveness**: a `match` on a determinable enum
  subject with no wildcard `_` arm must cover every variant; a missing variant is
  a located error. Non-enum subjects (int literals, `var`-inferred bindings) are
  left unchecked (bounded).
- **FR-002 (REQ-008) Generic constraint verification**: an explicit-type-argument
  call to a generic function whose parameter carries a `T: Protocol` constraint
  must supply a type argument that satisfies the protocol (structural conformance:
  the struct implements every required method by name). A non-conforming concrete
  struct is a located error. Builtins / inferred instantiations / unknown types
  are left unchecked (bounded, to avoid false positives).

## Audit programs
- `test_files/fail/sema/audit_08.b` — generic constraint not satisfied (`Point`
  lacks `Comparable.compare`) → located error, `.expected` pattern.
- `test_files/fail/sema/audit_09.b` — non-exhaustive match (variant `blue`
  unhandled) → located error, `.expected` pattern.

## Quiet-by-default fix
`FileLexer.cpp` `default:` case emitted a stray `printf("Unknown Charater ...")`
to stdout for `:` and other single characters reached via valid constructs
(e.g. generic constraints) during parse backtracking — surfaced by audit_08.
Gated behind the lexer's `-v` trace flag (REQ-003), consistent with U2.

## Success criteria
- SC-001: audit_08, audit_09 rejected under `build/qcc --parse-only` and the
  non-LLVM build with canonical located lines matching their `.expected`.
- SC-002: Gate A (`./run_tests.sh && ./test_codegen.sh`) and Gate B
  (`BUILD_DIR=build-parse ./run_tests.sh`) both exit 0; no corpus false positives.
- SC-003: quiet-by-default preserved (audit_08 emits nothing on stdout).

## Bounded deferral (tracked, does NOT block the epic done-condition)
The U5 workplan also lists "match-as-expression produces a checked value"
(`CGEnum.cpp` `genMatchExpression` returns nullptr today; the value-context
return fabrication is the `getNullValue` site left intact in U4). Implementing
value-producing-match codegen safely requires the match result type and per-arm
trailing-value capture without regressing the statement-form match tests — a
sizable codegen change. It is **deferred as a tracked follow-up**: it is not one
of the epic's five machine-checkable done-condition commands (which require the
audit programs rejected and both suites green, both delivered here). Recorded so
the deferral is explicit, not silent.

## Out of scope
Ownership/move (U6); concurrency (U7); the strict-migration sweep to >=25
fail/sema (U8).
