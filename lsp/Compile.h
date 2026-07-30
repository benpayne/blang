#ifndef BLANG_LSP_COMPILE_H_
#define BLANG_LSP_COMPILE_H_

// The LSP reparse pipeline: compile one in-memory document through the shared
// frontend (lexer -> parser -> Sema) and return the buffered diagnostics.
// Single-file semantics, same as `qcc file.b` (cross-file import resolution
// is bcc's --combine mode and out of scope for v1).

#include <string>
#include <vector>

#include "../DiagnosticEngine.h"

namespace lsp
{

// Compile `text` as the document at `path` (used in diagnostic locations).
// Never prints; never throws for source-level errors. Each call uses a fresh
// DiagnosticEngine and a fresh file scope parented to a process-lifetime
// global builtin scope (pinned via SmartPtr — see Frontend.h).
std::vector<QLang::Diagnostic> compileDocument( const std::string &path,
                                                const std::string &text );

} // namespace lsp

#endif // BLANG_LSP_COMPILE_H_
