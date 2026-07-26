#ifndef BLANG_LOCATION_DUMPER_H_
#define BLANG_LOCATION_DUMPER_H_

#include <ostream>

#include "Type.h"
#include "Expression.h"

namespace QLang
{

// Walks a parsed Module in deterministic pre-order (parent before children,
// children in source order) and prints one line per AST node:
//   <file>:<line>:<col> <NodeKind>
// Used by qcc --dump-locations and as a golden-file regression lock for
// source-location coverage (spec REQ-001). Standalone walker in the
// BmodEmitter tradition; a friend of the AST classes so it can traverse
// their private children.
class LocationDumper
{
public:
	static void dump( const Module *mod, std::ostream &out );

private:
	explicit LocationDumper( std::ostream &out ) : mOut( out ) {}

	void visitModule( const Module *mod );
	void visitImport( const ImportStatement *imp );
	void visitFunction( const FunctionDefinition *func );
	void visitVariableDef( const VariableDefinition *var );
	void visitStruct( const StructDefinition *structDef );
	void visitEnum( const EnumDefinition *enumDef );
	void visitProtocol( const ProtocolDefinition *protoDef );
	void visitTestBlock( const TestBlock *test );

	// Statements and expressions (Expression derives from Statement).
	void visitStatement( const Statement *stmt );

	void emit( const SourceLocation &loc, const char *kind );

	std::ostream &mOut;
};

} // namespace QLang

#endif // BLANG_LOCATION_DUMPER_H_
