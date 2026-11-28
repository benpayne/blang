#ifndef BLANG_LSP_COMPILE_H_
#define BLANG_LSP_COMPILE_H_

// The LSP reparse pipeline: compile one in-memory document through the shared
// project-compile entry point (ProjectCompile::run — the same call qcc makes)
// and return the buffered diagnostics plus the parsed module (retained for
// navigation requests: definition, hover, documentSymbol). Still single-file
// semantics in modules-v2-lsp U1, same as `qcc file.b`; U2/U3 add the stdlib
// and project context through the same entry point.

#include <memory>
#include <string>
#include <vector>

#include "../DiagnosticEngine.h"
#include "../Type.h"
#include "../Expression.h"   // completes the AST types SmartPtr instantiates
#include "../ProjectCompile.h"

namespace lsp
{

struct CompileResult
{
	// The parsed AST (non-null even for documents with errors — the parser
	// recovers per declaration) and the scope it was parsed in. The scope
	// keeps the definitions the AST's resolution pointers reference alive.
	SmartPtr<QLang::Module> module;
	SmartPtr<QLang::Scope> fileScope;
	std::vector<QLang::Diagnostic> diagnostics;
	// The shared compile's context (owns every scope and AST the compile built;
	// KG-7). shared_ptr because the Server stores results by value.
	std::shared_ptr<QLang::CompileContext> context;
};

// Compile `text` as the document at `path` (used in diagnostic locations).
// Never prints; never throws for source-level errors. Each call uses a fresh
// DiagnosticEngine and a fresh file scope parented to a process-lifetime
// global builtin scope (pinned via SmartPtr — see Frontend.h).
CompileResult compileDocument( const std::string &path, const std::string &text );

} // namespace lsp

#endif // BLANG_LSP_COMPILE_H_
