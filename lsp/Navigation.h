#ifndef BLANG_LSP_NAVIGATION_H_
#define BLANG_LSP_NAVIGATION_H_

// modules-v2-lsp U4: cross-module go-to-definition and hover (spec
// specs/044-lsp-xmodule-navigation; contract contracts/navigation.md).
//
// blangd owns NO resolution, visibility or export rule here. Every input is
// produced by the shared project-compile entry point (ProjectCompile::run) for the
// document and for each dependency (U3 Runs):
//   - declarations, their SourceLocations and the module records (path, name,
//     role) of the document's compile;
//   - the dependency SOURCE compiles (InterfaceSourceMap) — interface -> source;
//   - CompileContext::useCapabilityTypes + StructDefinition::findMethod +
//     Sema::receiverType — the compiler's own use-capability member rule (R3);
//   - ModuleRecord::typeReferences — the parser's own type-name bindings (R4);
//   - Module::getImports() — the viewer's qualifier bindings (FR-008).
// This file only looks things up in those structures.
//
// Same-module targets are answered by the UNCHANGED pre-U4 code
// (AstLocator::definitionLocation / AstLocator::hoverText), so same-file
// behavior is byte-identical (FR-007).

#include <cstdint>
#include <string>

#include "../SourceLocation.h"
#include "Compile.h"

namespace lsp
{

// The answer to one navigation request.
struct NavigationAnswer
{
	bool found = false;       // false => the response is null
	SourceLocation location;  // definition: the (source) declaration site
	std::string hover;        // hover: the one-line rendering
};

// textDocument/definition at a 1-based (line, col) of the document `docPath`.
NavigationAnswer definitionAt( const CompileResult &result, const std::string &docPath,
	uint32_t line, uint32_t col );

// textDocument/hover at a 1-based (line, col) of the document `docPath`.
NavigationAnswer hoverAt( const CompileResult &result, const std::string &docPath,
	uint32_t line, uint32_t col );

} // namespace lsp

#endif // BLANG_LSP_NAVIGATION_H_
