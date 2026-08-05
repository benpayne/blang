#ifndef BLANG_BMOD_EMITTER_H_
#define BLANG_BMOD_EMITTER_H_

#include "BmodFormat.h"
#include <set>
#include <string>
#include <vector>
#include <ostream>

#include "Type.h"

namespace QLang
{

class BmodEmitter
{
public:
	// The .bmod interface format version (see BmodFormat.h for the history
	// and the bump rule). Aliased here so emitter code reads naturally.
	static const int kFormatVersion = BlangBmod::kFormatVersion;

	// Emit a .bmod interface file from one or more parsed modules.
	// Only pub symbols are emitted.
	static void emit( const std::vector<Module*> &modules, std::ostream &out );

private:
	static void emitFunction( FunctionDefinition *func, std::ostream &out );
	static void emitStruct( StructDefinition *structDef, std::ostream &out,
		const std::set<std::string> &exportedProtocols );

	// Non-generic structs ship an `impl` block of bodyless init/method
	// SIGNATURES — the interface that makes an imported type constructible and
	// callable (P8). The init signature doubles as the struct's factory record:
	// its presence tells a consumer to construct through the library-emitted
	// factory instead of allocating locally.
	static void emitStructInterface( StructDefinition *structDef, std::ostream &out );

	// Protocol conformance records (`impl Protocol for Type { }`, D16), emitted
	// after the interface block so the conformance check sees the methods.
	//
	// `exportedProtocols` is the set of protocol names a consumer will be able to
	// resolve from this file: the protocols this .bmod itself declares, plus the
	// always-in-scope builtins. A record naming anything else would be a dangling
	// reference that makes the whole interface unparseable, so it is skipped.
	static void emitConformances( StructDefinition *structDef, std::ostream &out,
		const std::set<std::string> &exportedProtocols );
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
