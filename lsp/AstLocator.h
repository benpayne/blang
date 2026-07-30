#ifndef BLANG_LSP_AST_LOCATOR_H_
#define BLANG_LSP_AST_LOCATOR_H_

// Cursor-to-AST-node resolution for the LSP server. Walks a parsed Module
// with the same traversal shape as LocationDumper (friend of the AST
// classes) and finds the node "under" a 1-based line/column cursor: among
// nodes located on the cursor's line at or before the cursor column, the one
// with the greatest column (i.e. the innermost construct that starts at or
// left of the cursor). SourceLocation has no end position, so this is the
// documented v1 approximation — behavior is pinned by the definition/hover
// transcript goldens.
//
// Lives in namespace QLang so the existing `friend class AstLocator`
// declarations beside every `friend class LocationDumper` apply; compiled
// only into blangd.

#include "../Type.h"
#include "../Expression.h"

namespace QLang
{

class AstLocator
{
public:
	// Innermost statement/expression at (line, col), or null when nothing on
	// that line starts at or before the cursor.
	static const Statement *locate( const Module *mod, uint32_t line, uint32_t col );

	// The definition site a node navigates to (go-to-definition), for every
	// AST kind that carries a resolved definition pointer: variable
	// references, calls, constructor calls, enum variant construction
	// (variant location when stamped, else the enum), named function
	// references, indirect calls (the fn-typed variable), and — via the Sema
	// stamps — field accesses and method calls. Returns false when the node
	// kind has no definition pointer, the pointer is unresolved, or the
	// definition has no source location (compiler builtins).
	static bool definitionLocation( const Statement *node, SourceLocation &out );

private:
	AstLocator( uint32_t line, uint32_t col ) : mLine( line ), mCol( col ) {}

	void visitModule( const Module *mod );
	void visitFunction( const FunctionDefinition *func );
	void visitStatement( const Statement *stmt );
	void consider( const Statement *stmt );

	uint32_t mLine;
	uint32_t mCol;
	const Statement *mBest = nullptr;
};

} // namespace QLang

#endif // BLANG_LSP_AST_LOCATOR_H_
