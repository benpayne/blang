# Contract: Codegen Hardening (U4, REQ-012)
- Return-fabrication (`getNullValue`/`CreateIntToPtr`) in CGStatements.cpp: REMOVED.
- Dropped-initializer (`initVal = nullptr` then skipped store): REMOVED.
- CodeGen.cpp expression-dispatch fallback: emits a loud ICE ("internal compiler
  error: unhandled expression node — please report") instead of silent nullptr.
- After sema, no valid user program reaches these; an ICE indicates a compiler bug.
