#ifndef BLANG_BMOD_EMITTER_H_
#define BLANG_BMOD_EMITTER_H_

#include <string>
#include <vector>
#include <ostream>

#include "Type.h"

namespace QLang
{

class BmodEmitter
{
public:
	// Emit a .bmod interface file from one or more parsed modules.
	// Only pub symbols are emitted.
	static void emit( const std::vector<Module*> &modules, std::ostream &out );

private:
	static void emitFunction( FunctionDefinition *func, std::ostream &out );
	static void emitStruct( StructDefinition *structDef, std::ostream &out );
	static void emitEnum( EnumDefinition *enumDef, std::ostream &out );
	static void emitProtocol( ProtocolDefinition *protoDef, std::ostream &out );
	static void emitAnnotations( const std::vector<AnnotationNode> &annotations, std::ostream &out );
	static void emitGenericParams( const std::vector<GenericParam> &params, std::ostream &out );
	static void emitType( Type *type, std::ostream &out );

	// Cross-module generics: a generic definition's BODY is shipped in the
	// .bmod (verbatim source slice) so consumers can monomorphize it — a
	// signature alone would leave every downstream instantiation a linker
	// error. Returns the source text from the start of the definition's line
	// through its brace-matched closing '}', or empty on failure (caller falls
	// back to signature-only emission).
	static std::string sliceDefinitionSource( const SourceLocation &loc );
};

} // namespace QLang

#endif // BLANG_BMOD_EMITTER_H_
