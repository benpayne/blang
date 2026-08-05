#include "BmodEmitter.h"
#include "Expression.h"

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

using namespace QLang;
using namespace std;

// Cache of source files read for definition slicing (one read per file).
static map<string, vector<string>> gSourceLineCache;

static const vector<string> *sourceLines( const string &path )
{
	auto it = gSourceLineCache.find( path );
	if ( it != gSourceLineCache.end() )
		return &it->second;

	ifstream in( path );
	if ( !in.is_open() )
		return nullptr;

	vector<string> lines;
	string line;
	while ( getline( in, line ) )
		lines.push_back( line );
	auto res = gSourceLineCache.emplace( path, std::move( lines ) );
	return &res.first->second;
}

string BmodEmitter::sliceDefinitionSource( const SourceLocation &loc )
{
	if ( loc.file.empty() || loc.line == 0 )
		return "";
	const vector<string> *lines = sourceLines( loc.file );
	if ( lines == nullptr || (size_t)loc.line > lines->size() )
		return "";

	// Scan from the start of the definition's line, brace-matching to the
	// definition's closing '}' while skipping strings, chars, and comments.
	ostringstream out;
	int depth = 0;
	bool sawOpen = false;
	bool inLineComment = false, inBlockComment = false;
	bool inString = false, inChar = false;

	for ( size_t li = (size_t)loc.line - 1; li < lines->size(); li++ )
	{
		const string &l = ( *lines )[li];
		inLineComment = false;
		for ( size_t ci = 0; ci < l.size(); ci++ )
		{
			char c = l[ci];
			char next = ( ci + 1 < l.size() ) ? l[ci + 1] : '\0';
			if ( inLineComment )
				continue;
			if ( inBlockComment )
			{
				if ( c == '*' && next == '/' )
				{
					inBlockComment = false;
					ci++;
				}
				continue;
			}
			if ( inString )
			{
				if ( c == '\\' )
					ci++;
				else if ( c == '"' )
					inString = false;
				continue;
			}
			if ( inChar )
			{
				if ( c == '\\' )
					ci++;
				else if ( c == '\'' )
					inChar = false;
				continue;
			}
			if ( c == '/' && next == '/' ) { inLineComment = true; continue; }
			if ( c == '/' && next == '*' ) { inBlockComment = true; ci++; continue; }
			if ( c == '"' ) { inString = true; continue; }
			if ( c == '\'' ) { inChar = true; continue; }
			if ( c == '{' ) { depth++; sawOpen = true; }
			else if ( c == '}' )
			{
				depth--;
				if ( sawOpen && depth == 0 )
				{
					// Emit through this closing brace and stop.
					out << l.substr( 0, ci + 1 ) << "\n";
					return out.str();
				}
			}
		}
		out << l << "\n";
	}
	return ""; // ran off the end without closing — malformed, fall back
}

// Helper to get a non-const Type* from various const SmartPtr contexts.
// The BmodEmitter only reads from types, never modifies them.
static Type *nc( const Type *t ) { return const_cast<Type*>( t ); }

void BmodEmitter::emitType( Type *type, ostream &out )
{
	out << type->getName();
	if ( type->getNumTypeParams() > 0 )
	{
		out << "<";
		for ( int i = 0; i < type->getNumTypeParams(); i++ )
		{
			if ( i > 0 )
				out << ", ";
			emitType( type->getTypeParam( i ), out );
		}
		out << ">";
	}
}

void BmodEmitter::emitAnnotations( const vector<AnnotationNode> &annotations, ostream &out )
{
	for ( const auto &ann : annotations )
	{
		out << "@" << ann.mName;
		if ( !ann.mArgs.empty() )
		{
			out << "(";
			for ( size_t i = 0; i < ann.mArgs.size(); i++ )
			{
				if ( i > 0 )
					out << ", ";
				out << "\"" << ann.mArgs[i] << "\"";
			}
			out << ")";
		}
		out << endl;
	}
}

void BmodEmitter::emitGenericParams( const vector<GenericParam> &params, ostream &out )
{
	if ( params.empty() )
		return;

	out << "<";
	for ( size_t i = 0; i < params.size(); i++ )
	{
		if ( i > 0 )
			out << ", ";
		out << params[i].mName;
		if ( !params[i].mConstraint.empty() )
			out << ": " << params[i].mConstraint;
	}
	out << ">";
}

void BmodEmitter::emitFunction( FunctionDefinition *func, ostream &out )
{
	if ( !func->isPublic() )
		return;

	emitAnnotations( func->getAnnotations(), out );

	// A GENERIC function ships its full definition (verbatim source): the
	// consumer must monomorphize the body per instantiation — a signature
	// alone would leave every downstream use a linker error. Monomorphized
	// instances are emitted linkonce_odr, so a lib and its consumers
	// instantiating the same specialization dedup at link time.
	if ( func->isGeneric() )
	{
		string src = sliceDefinitionSource( func->getLocation() );
		if ( !src.empty() )
		{
			out << src;
			return;
		}
		// fall through to signature-only if the source is unavailable
	}

	out << "pub fn " << func->getName();
	emitGenericParams( func->getGenericParams(), out );
	out << "(";

	for ( int i = 0; i < func->getNumberParams(); i++ )
	{
		if ( i > 0 )
			out << ", ";
		VariableDefinition *param = func->getParam( i );
		emitType( nc( param->getVariableType() ), out );
		out << " " << param->getName();
	}

	if ( func->isVariadic() )
	{
		if ( func->getNumberParams() > 0 )
			out << ", ";
		out << "...";
	}

	out << ")";

	if ( func->getReturnType() != nullptr )
	{
		out << " -> ";
		emitType( func->getReturnType(), out );
	}

	out << ";" << endl;
}

void BmodEmitter::emitStruct( StructDefinition *structDef, ostream &out,
	const std::set<std::string> &exportedProtocols )
{
	if ( !structDef->isPublic() )
		return;

	emitAnnotations( structDef->getAnnotations(), out );

	// Source order is `pub table struct Name` — the visibility modifier first.
	// This emitted the inverse (`table pub struct`) until U2; it round-tripped
	// only through parser leniency, and U5's D15 metadata work makes table
	// structs load-bearing across the boundary, so it has to be right first.
	out << "pub ";
	if ( structDef->isTable() )
		out << "table ";
	out << "struct " << structDef->getName();
	emitGenericParams( structDef->getGenericParams(), out );
	out << " {" << endl;

	for ( const auto &field : structDef->getFields() )
	{
		out << "\t";
		emitType( nc( field->getVariableType() ), out );
		out << " " << field->getName() << ";" << endl;
	}

	out << "}" << endl;

	// A GENERIC struct also ships its method bodies (an impl block of verbatim
	// source slices): instantiating Box<int> in a consumer monomorphizes the
	// methods, which requires bodies. Non-generic struct methods stay out of
	// the .bmod — they are ordinary symbols linked from the library archive.
	if ( !structDef->getGenericParams().empty() &&
		 !structDef->getMethods().empty() )
	{
		ostringstream methods;
		bool allSliced = true;
		for ( const auto &msp : structDef->getMethods() )
		{
			FunctionDefinition *m = const_cast<FunctionDefinition*>(
				(const FunctionDefinition*)msp );
			string src = sliceDefinitionSource( m->getLocation() );
			if ( src.empty() )
			{
				allSliced = false;
				break;
			}
			methods << src;
		}
		if ( allSliced )
		{
			out << "impl " << structDef->getName() << " {" << endl;
			out << methods.str();
			out << "}" << endl;
		}

		// Conformance records apply to generic structs too: D16 names generic
		// CONSTRAINT checking (`sort<T: Comparable>` with a foreign T) as one of
		// the things that needs them, so returning early here would leave exactly
		// that case unserved.
		emitConformances( structDef, out, exportedProtocols );
		return;
	}

	// A NON-GENERIC struct ships its init and method SIGNATURES (no bodies) —
	// this is what makes an imported type constructible and callable at all
	// (design record P8). The bodies stay in the library archive and link from
	// the .a; codegen turns each bodyless signature into an LLVM `declare`.
	//
	// The `init` signature is also the struct's factory record: a consumer that
	// sees it constructs through the library-emitted factory symbol (derived
	// from the struct name by mangleStructFactoryName) rather than allocating
	// locally. The factory is deliberately NOT emitted as a free `pub fn` — it
	// must not be nameable from source, or `Counter(5)` would gain a second
	// spelling and the one-external-form rule (D9) would be broken.
	//
	// Interim semantics: every method ships until `pub` exists on impl members;
	// the unit that adds `pub` flips this to pub-only.
	if ( !structDef->getMethods().empty() )
		emitStructInterface( structDef, out );

	emitConformances( structDef, out, exportedProtocols );
}

void BmodEmitter::emitConformances( StructDefinition *structDef, ostream &out,
	const std::set<std::string> &exportedProtocols )
{
	// Protocol conformance records (design record D16). Emitted as EMPTY impl
	// blocks: the method signatures are already in the struct's interface block
	// above, and repeating them here would give the struct two copies of every
	// conforming method. An empty body still satisfies the conformance check,
	// which validates against the struct's accumulated methods rather than the
	// impl block's own members (QImplBlock.cpp:156-157) — so this must be
	// emitted AFTER the interface block, never before.
	//
	// Without these records a consumer cannot dispatch `print("{}", x)` through
	// Printable on an imported type, and a foreign type cannot satisfy a generic
	// constraint.
	for ( const auto &proto : structDef->getConformedProtocols() )
	{
		// Only emit a record a consumer can actually resolve. A non-`pub`
		// protocol is not emitted into this .bmod, so a record naming it would
		// dangle and take the whole interface down with it. (Rejecting that
		// combination at the LIBRARY build is P9 enforcement, which U3 owns —
		// until then U2's job is simply not to emit an unreadable file.)
		if ( exportedProtocols.count( proto ) == 0 )
			continue;
		out << "impl " << proto << " for " << structDef->getName() << " {" << endl
		    << "}" << endl;
	}
}

void BmodEmitter::emitStructInterface( StructDefinition *structDef, ostream &out )
{
	out << "impl " << structDef->getName() << " {" << endl;

	// A struct accumulates methods from every impl block that targets it — its
	// own, plus one per `impl Protocol for Struct`. Emitting the list verbatim
	// would repeat any method that satisfies a protocol, giving the re-parsed
	// struct two copies of it. BLang has no overloading, so the name alone is
	// the identity.
	std::vector<std::string> emitted;

	for ( const auto &msp : structDef->getMethods() )
	{
		FunctionDefinition *method = const_cast<FunctionDefinition*>(
			(const FunctionDefinition*)msp );

		bool already = false;
		for ( const auto &n : emitted )
			if ( n == method->getName() )
				already = true;
		if ( already )
			continue;
		emitted.push_back( method->getName() );

		// Generic methods on a non-generic struct would need their body shipped
		// to be monomorphized; that is out of this unit's scope, so skip them
		// rather than emit an unusable signature.
		if ( method->isGeneric() )
			continue;

		if ( method->isInit() )
			out << "\tinit(";
		else
			out << "\tfn " << method->getName() << "(";

		bool first = true;
		for ( int i = 0; i < method->getNumberParams(); i++ )
		{
			VariableDefinition *param = method->getParam( i );
			bool isSelf = ( param->getVariableType() != nullptr &&
				param->getVariableType()->getName() == "self" );

			// `init` carries an implicit self that is re-created on parse, so
			// it must not appear in the emitted signature.
			if ( isSelf && method->isInit() )
				continue;

			if ( !first )
				out << ", ";
			first = false;

			if ( isSelf )
				out << "self";
			else
			{
				emitType( nc( param->getVariableType() ), out );
				out << " " << param->getName();
			}
		}

		out << ")";

		if ( !method->isInit() && method->getReturnType() != nullptr )
		{
			out << " -> ";
			emitType( method->getReturnType(), out );
		}

		out << ";" << endl;
	}

	out << "}" << endl;
}

void BmodEmitter::emitEnum( EnumDefinition *enumDef, ostream &out )
{
	if ( !enumDef->isPublic() )
		return;

	emitAnnotations( enumDef->getAnnotations(), out );

	out << "pub enum " << enumDef->getName();
	emitGenericParams( enumDef->getGenericParams(), out );
	out << " {" << endl;

	const auto &variants = enumDef->getVariants();
	for ( size_t i = 0; i < variants.size(); i++ )
	{
		out << "\t" << variants[i].mName;
		if ( !variants[i].mAssociatedTypes.empty() )
		{
			out << "(";
			for ( size_t j = 0; j < variants[i].mAssociatedTypes.size(); j++ )
			{
				if ( j > 0 )
					out << ", ";
				emitType( nc( (const Type*)variants[i].mAssociatedTypes[j] ), out );
			}
			out << ")";
		}
		if ( i + 1 < variants.size() )
			out << ",";
		out << endl;
	}

	out << "}" << endl;
}

void BmodEmitter::emitProtocol( ProtocolDefinition *protoDef, ostream &out )
{
	if ( !protoDef->isPublic() )
		return;

	out << "pub protocol " << protoDef->getName();
	emitGenericParams( protoDef->getGenericParams(), out );
	out << " {" << endl;

	for ( const auto &sp : protoDef->getRequiredMethods() )
	{
		FunctionDefinition *method = const_cast<FunctionDefinition*>( (const FunctionDefinition*)sp );
		out << "\tfn " << method->getName() << "(";

		for ( int i = 0; i < method->getNumberParams(); i++ )
		{
			if ( i > 0 )
				out << ", ";
			VariableDefinition *param = method->getParam( i );
			if ( param->getVariableType()->getName() == "self" )
				out << "self";
			else
			{
				emitType( nc( param->getVariableType() ), out );
				out << " " << param->getName();
			}
		}

		out << ")";

		if ( method->getReturnType() != nullptr )
		{
			out << " -> ";
			emitType( method->getReturnType(), out );
		}

		out << ";" << endl;
	}

	out << "}" << endl;
}

void BmodEmitter::emit( const vector<Module*> &modules, ostream &out )
{
	out << "// auto-generated .bmod interface file — do not edit" << endl;
	// Format version, deliberately a COMMENT: a compiler that predates the
	// marker still parses the file. A version mismatch should cost a cache miss
	// and a rebuild, never a syntax error inside a generated file.
	out << "// blang-bmod-format: " << kFormatVersion << endl;
	out << endl;

	// Names a consumer of this file will be able to resolve: the protocols this
	// .bmod declares, plus the builtins that are in scope everywhere.
	std::set<std::string> exportedProtocols;
	// KEEP IN SYNC with the builtin protocol registration in
	// QModule.cpp (createGlobalScope, "Register Printable as a builtin
	// protocol"). A builtin is resolvable in every scope without being declared
	// in any .bmod, so it must be listed here or every conformance record naming
	// it would be silently dropped (known-issues KI-15 is the same failure for
	// protocols arriving via a dependency).
	exportedProtocols.insert( "Printable" );
	for ( auto *mod : modules )
		for ( const auto &sp : mod->getProtocolList() )
		{
			ProtocolDefinition *p = const_cast<ProtocolDefinition*>( (const ProtocolDefinition*)sp );
			if ( p->isPublic() )
				exportedProtocols.insert( p->getName() );
		}

	for ( auto *mod : modules )
	{
		// PROTOCOLS FIRST. A struct's conformance record (`impl P for S { }`)
		// names a protocol, and the impl-block parser resolves that name at the
		// point of use — so emitting protocols after structs makes every record a
		// forward reference and the whole interface unparseable:
		//
		//     t.bmod:11:23: error: Unknown protocol 'Sizeable' in impl block
		//
		// It went unnoticed at first because the only conformance in the corpus
		// was to `Printable`, the one builtin protocol pre-registered in every
		// scope. Any user-defined `pub protocol` broke its library's interface.
		//
		// The "a record must follow its struct's interface block" constraint
		// (conformance checking reads the struct's accumulated methods) concerns
		// the STRUCT, not the protocol, so both orderings satisfy it.
		for ( const auto &sp : mod->getProtocolList() )
		{
			ProtocolDefinition *protoDef = const_cast<ProtocolDefinition*>( (const ProtocolDefinition*)sp );
			emitProtocol( protoDef, out );
			out << endl;
		}

		// Emit structs
		for ( const auto &sp : mod->getStructList() )
		{
			StructDefinition *structDef = const_cast<StructDefinition*>( (const StructDefinition*)sp );
			emitStruct( structDef, out, exportedProtocols );
			out << endl;
		}

		// Emit enums
		for ( const auto &sp : mod->getEnumList() )
		{
			EnumDefinition *enumDef = const_cast<EnumDefinition*>( (const EnumDefinition*)sp );
			emitEnum( enumDef, out );
			out << endl;
		}

		// Emit functions
		for ( const auto &sp : mod->getFunctionList() )
		{
			FunctionDefinition *func = const_cast<FunctionDefinition*>( (const FunctionDefinition*)sp );
			emitFunction( func, out );
		}
	}
}
