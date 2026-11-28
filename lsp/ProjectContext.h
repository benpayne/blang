#ifndef BLANG_LSP_PROJECT_CONTEXT_H_
#define BLANG_LSP_PROJECT_CONTEXT_H_

// modules-v2-lsp U3: a document's blang.toml project context (spec FR-001..FR-016;
// plan D-3..D-6; contract project-context.md).
//
// blangd owns NO project rule here. Discovery, source selection, dependency
// resolution and the transitive walk are BlangProject's (shared with bcc —
// audit R3/H1); interfaces come from BmodEmitter::emit (the one exporter — D2);
// every compile is ProjectCompile::run in an isolated Run (audit R2). This file
// only assembles plans, keeps the dependency source compiles, and turns setup
// failures into located diagnostics.

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../DiagnosticEngine.h"
#include "../ProjectCompile.h"
#include "../ProjectConfig.h"
#include "DocumentStore.h"
#include "Run.h"

namespace lsp
{

// One setup problem (missing path, unfetched/unpinned git dep, a dependency
// whose own compile failed, a cycle, a blang.toml line), published once.
struct SetupDiagnostic
{
	QLang::Severity severity = QLang::Severity::Error;
	std::string message;
	std::string importName;   // "" = no import site: published at 1:1
	std::string declaredAt;   // "<toml>:<line>" for the 1:1 form ("" = none)
};

// The project context of one document (rebuilt on every reparse; nothing kept).
struct ProjectContextResult
{
	bool isProject = false;   // a blang.toml was found AND the document is a member
	bool tomlUnreadable = false;
	std::string tomlPath;
	std::string root;          // canonical project root
	bool isLibrary = false;
	// The plan bcc build's final qcc invocation uses, minus the stdlib (the
	// caller adds it — U2's location): the project sources (in-memory; the
	// document's own input uses its URI path EXACTLY — analyze M6) and the
	// resolved closure interfaces (in-memory .bmod text, topological order).
	std::vector<QLang::CompileInput> sources;
	std::vector<QLang::CompileInput> interfaces;
	std::string moduleOrigin;  // realpath(root) — bcc's --module-origin
	// The dependency SOURCE compiles (kept for U4 navigation).
	std::vector<std::shared_ptr<Run>> depRuns;
	// U4 (research R2): each SUCCESSFULLY derived dependency's interface path
	// (exactly the CompileInput::path of its interface input) and its source Run.
	std::vector<std::pair<std::string, std::shared_ptr<Run>>> interfaceRuns;
	std::vector<SetupDiagnostic> setup;
	std::set<std::string> unresolvedNames;   // for the cascade suppression (D-5)
};

// Locate and assemble `docPath`'s project context. `docText` is the document's
// own (possibly unsaved) text.
ProjectContextResult buildProjectContext( const std::string &docPath, const std::string &docText,
	const Overlay &overlay );

// Read a file through the overlay first (canonical-path keyed), else from disk.
// Returns false when neither has it.
bool readThroughOverlay( const Overlay &overlay, const std::string &path, std::string &out );

} // namespace lsp

#endif // BLANG_LSP_PROJECT_CONTEXT_H_
