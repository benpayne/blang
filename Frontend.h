#ifndef BLANG_FRONTEND_H_
#define BLANG_FRONTEND_H_

// Shared compiler-frontend entry points (defined in QModule.cpp), used by the
// qcc driver, the LSP server, and the fuzz harness.

#include "Type.h"

namespace QLang { class DiagnosticEngine; }

// Global scope shared by all per-file module scopes in one compile. Set by the
// driver from createGlobalScope(); the DRIVER owns it (hold it in a
// SmartPtr) — the global is a non-owning alias for the parser's lookups.
extern QLang::Scope *gScope;

// The single diagnostic reporting path. Null until a driver installs one; the
// top-level parse-catch falls back to a local engine when unset.
extern QLang::DiagnosticEngine *gDiag;

// Build the global scope with every compiler builtin: primitive types,
// print/println/to_json, the Printable protocol, and Option/Result. Returned
// at refcount 0 like any freshly built node — the caller must take ownership
// (store it in a SmartPtr) before parenting file scopes to it, or the first
// file scope's release will free it.
QLang::Scope *createGlobalScope();

#endif // BLANG_FRONTEND_H_
