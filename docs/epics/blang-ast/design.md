# Design spec: blang-ast

Product-level design. Engineering detail (exact class shapes, visitor vs
dispatch, data-flow representation) is decided by the hires in their per-unit
speckit plans — this document fixes the target architecture, the seams, and
the constraints they must respect.

## Context: current state (from the 2026-07-13 audit)

Pipeline today: `FileLexer` → recursive-descent parser (`Q*.cpp`, factory
`Parse(Lexer&, Scope*)` methods building a `RefCount`/`SmartPtr` AST) →
`CodeGen` (`CodeGen.cpp` + `CG*.cpp`, `dynamic_cast` dispatch, LLVM 18).

Load-bearing facts for this epic:

- **No locations.** AST nodes (`Statement` base in `Type.h`, expressions in
  `Expression.h`) store no source position. The lexer tracks line only;
  `charPos` is dead (`FileLexer.cpp:11`, never incremented); the filename is
  discarded after open. `CompileError` (`CompilerHelpers.h`) formats the
  compiler's own C++ `__FILE__:__LINE__` first (`qcc.cpp:27-33`).
- **No semantic pass.** All checks live in `CG*.cpp` behind
  `BLANG_HAS_LLVM`; parse-only builds validate syntax only. Errors are
  ad-hoc `cerr` + `mHasError` (`CodeGen.h:453`); many failure paths return
  `nullptr` silently (`CGStruct.cpp:968,994`; `CodeGen.cpp:1118`).
- **Silent coercions.** Return mismatch → `inttoptr`/zeroed struct
  (`CGStatements.cpp:521-541`); bad initializer → store skipped
  (`CGStatements.cpp:285`); `match` as expression → always `nullptr`
  (`CGEnum.cpp:331`); generic constraints unchecked (`CGTypes.cpp:397,212`).
- **Safety gaps.** `shared` field mutation allowed (`CGStruct.cpp:1035`);
  `sync` field access unlocked; spawn captures non-`own` heap values by raw
  pointer copy (`CGLambda.cpp:33-39,123-135`); move tracking is
  codegen-resident and shallow (`CGExpressions.cpp:69-75`,
  `CGStatements.cpp:606-630`).
- **Noise.** Unconditional token/trace/AST dumps on every compile
  (`FileLexer.cpp:321`, `qcc.cpp:282,681`); `bcc` grep-filters some stderr
  but not stdout (`bcc.cpp:948-981`).

## Target architecture

```
source.b ─→ Lexer(+file,line,col) ─→ Parser (Q*.cpp, unchanged shape,
                                         stamps SourceLocation on every node)
                 ─→ Sema (NEW: always compiled, no LLVM dependency)
                       • name/member resolution → typed AST
                       • type checking (REQ-005..008)
                       • ownership/move analysis (REQ-009)
                       • concurrency rules (REQ-010)
                       • all errors via DiagnosticEngine
                 ─→ CodeGen (consumes typed AST; trusts it; any surprise = ICE)
```

Key structural decisions (fixed):

1. **Sema is a separate pass, not parser- or codegen-resident.** It runs in
   every build mode; `--parse-only` becomes "parse + sema". Codegen may keep
   its LLVM-specific lowering logic but loses its role as the checker.
2. **One diagnostic path.** A `DiagnosticEngine` owned by the driver formats
   `<file>:<line>:<col>: error: <message>`; parser and sema both report
   through it. `CompileError` survives as the parser's control-flow
   mechanism but carries a `SourceLocation`. C++ internals visible only
   under `--debug-compiler`.
3. **Typed AST.** Sema records the resolved type of every expression on the
   node (the `Expression`/`Type` classes already exist; sema fills them in
   authoritatively). Codegen reads types from the AST instead of re-deriving
   them; divergence is a bug in exactly one place.
4. **Codegen trusts, loudly.** After sema, ill-typed input reaching codegen
   is an internal compiler error (assert + "please report" message), never a
   silent `return nullptr`. The existing silent-coercion sites are deleted,
   not bypassed.
5. **Diagnostics are single-error, located, clean.** Multi-error recovery,
   caret snippets, warnings, and `--json` are explicitly deferred; the
   DiagnosticEngine API should not preclude them (severity + location + note
   list), but no work is spent on them here.
6. **Implicit conversions are a closed set:** integer width promotion only
   (existing documented behavior). Everything else is an error.

## Seams and what must not break

- **Parser shape stays.** `Parse(Lexer&, Scope*)` factories, `SmartPtr`
  ownership, and the `QLang` namespace remain; U1 adds location capture but
  does not restructure parsing.
- **The 109 `pass/` tests, 47+ codegen E2E tests, and 14 demos** define
  current correct behavior. Any newly rejected program among them is either
  fixed in the same PR (small) or documented and fixed in U8 (bulk) — but
  suites must be green at every unit boundary (constitution, Principle II).
- **`--combine` / multi-module builds** (stdlib is compiled with user code
  into one scope) must keep working; sema must handle the combined-scope
  model and `.bmod`-imported symbols.
- **Runtime ABI unchanged** except U7's added lock calls around `sync`
  field access; `blang_runtime`/`blang_array`/`blang_string` interfaces are
  otherwise stable in this epic.
- **`bcc` pipeline** (qcc → llc → cc) unchanged; its stderr grep-filter
  hack becomes unnecessary once REQ-003 lands but is removed only after.

## Diagnostics catalogue (minimum set introduced by this epic)

Undefined variable / function / field / method; wrong argument count; wrong
argument type; incompatible assignment / initialization; invalid operands to
binary/unary operator; return type mismatch; `return;` in non-void function;
missing return on a path; non-exhaustive match; match arms with inconsistent
value types; generic constraint not satisfied; use after move (with move-site
note); move in loop (real ones only); assignment to `shared` value or its
fields; unguarded capture of heap value in `spawn`. Exact wording is decided
in U2's spec and then locked — every message gets an expected-error test.

## Risks

- **Biggest risk: strict mode breaks the world.** The stdlib and older tests
  were written against a coercing compiler. Mitigation: suites green at
  every unit boundary; U8 is an explicit budgeted sweep, not an afterthought.
- **Typed-AST/codegen divergence** during the transition (codegen re-deriving
  types differently than sema). Mitigation: decision 3 — codegen reads from
  the AST; hires remove codegen-local type inference as they touch each area.
- **Move-analysis false positives** could make the language annoying.
  Mitigation: U6's done condition includes accepted-program tests
  (reassign-after-move, branch-local moves), not just rejections.
- **Monomorphization timing**: generic bodies are instantiated in codegen
  today; constraint checks must fire in sema (parse-only mode) without LLVM.
  U5 must place instantiation-time checking in sema even if body stamping
  stays in codegen.
