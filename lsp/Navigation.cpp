#include "Navigation.h"

#include <set>
#include <sstream>

#include "../Expression.h"
#include "../ProjectCompile.h"
#include "../Sema.h"
#include "../Type.h"
#include "AstLocator.h"
#include "Run.h"

namespace lsp
{

using namespace QLang;

namespace
{

bool endsWith( const std::string &s, const std::string &suffix )
{
	return s.size() >= suffix.size() &&
		s.compare( s.size() - suffix.size(), suffix.size(), suffix ) == 0;
}

// G1 (contract): a definition is always a real source location — never a .bmod,
// never a location with no file.
bool isNavigableSource( const SourceLocation &loc )
{
	return loc.isSet() && !loc.file.empty() && !endsWith( loc.file, ".bmod" );
}

// The per-request view of one document compile (nothing outlives the request).
class Nav
{
public:
	Nav( const CompileResult &r, const std::string &docPath ) : mR( r ), mDocPath( docPath )
	{
		if ( mR.context == nullptr )
			return;
		const auto &recs = mR.context->records();
		for ( std::size_t ri : mR.context->moduleIndices() )
			if ( recs[ri].path == docPath )
				mViewing = ri;
	}

	bool usable() const { return mR.context != nullptr && mR.module != nullptr; }

	// The RECORDED module record (index) of the document compile whose input
	// path is `file`; -1 when none (core/builtin declarations, unset files).
	long recordForFile( const std::string &file ) const
	{
		if ( file.empty() || mR.context == nullptr )
			return -1;
		const auto &recs = mR.context->records();
		for ( std::size_t ri : mR.context->moduleIndices() )
			if ( recs[ri].path == file )
				return (long)ri;
		return -1;
	}

	const ModuleRecord *record( long ri ) const
	{
		return ri < 0 ? nullptr : &mR.context->records()[ (std::size_t)ri ];
	}

	bool isViewing( long ri ) const { return ri >= 0 && (std::size_t)ri == mViewing; }

	// ---- the type-reference arm (contract step 1, research R4) -------------
	const Symbol *typeReferenceAt( uint32_t line, uint32_t col ) const
	{
		if ( mViewing == kNone )
			return nullptr;
		for ( const auto &ref : mR.context->records()[ mViewing ].typeReferences )
		{
			if ( ref.location.line == line && col >= ref.location.col &&
				 col < ref.location.col + ref.length && ref.location.file == mDocPath )
				return ref.target;
		}
		return nullptr;
	}

	// ---- the use-capability member fallback (contract step 3, research R3) ---
	const FunctionDefinition *useCapabilityMethod( const MethodCallExpression *mc ) const
	{
		Type *recv = Sema::receiverType( mc );
		if ( recv == nullptr )
			return nullptr;
		const StructDefinition *owner = uniqueUseCapabilityStruct( recv->getName() );
		if ( owner == nullptr )
			return nullptr;
		return owner->findMethod( const_cast<MethodCallExpression *>( mc )->getMethodName() );
	}

	// The unique struct of that name in the compiler's use-capability set (the set
	// qcc hands CodeGen::registerExternalTypes); null when absent or ambiguous.
	const StructDefinition *uniqueUseCapabilityStruct( const std::string &name ) const
	{
		loadUseCapability();
		const StructDefinition *found = nullptr;
		int n = 0;
		for ( const auto &s : mStructs )
			if ( s->getName() == name )
			{
				found = s;
				n++;
			}
		return n == 1 ? found : nullptr;
	}

	const Symbol *uniqueUseCapabilityType( const std::string &name ) const
	{
		if ( const StructDefinition *s = uniqueUseCapabilityStruct( name ) )
			return s;
		const EnumDefinition *found = nullptr;
		int n = 0;
		for ( const auto &e : mEnums )
			if ( e->getName() == name )
			{
				found = e;
				n++;
			}
		return n == 1 ? found : nullptr;
	}

	// The struct (in the record's module) that declares method/init `fn`.
	const StructDefinition *ownerOf( const FunctionDefinition *fn, long ri ) const
	{
		if ( fn == nullptr )
			return nullptr;
		auto search = [fn]( const Module *m ) -> const StructDefinition * {
			if ( m == nullptr )
				return nullptr;
			for ( const auto &sp : m->getStructList() )
			{
				StructDefinition *s = const_cast<StructDefinition *>( (const StructDefinition *)sp );
				if ( s->getInitMethod() == fn )
					return s;
				for ( const auto &meth : s->getMethods() )
					if ( (const FunctionDefinition *)meth == fn )
						return s;
			}
			return nullptr;
		};
		if ( const ModuleRecord *rec = record( ri ) )
			if ( const StructDefinition *s = search( rec->module ) )
				return s;
		for ( std::size_t i : mR.context->moduleIndices() )
			if ( const StructDefinition *s = search( mR.context->records()[i].module ) )
				return s;
		return nullptr;
	}

	// ---- source mapping (contract step 4, research R2) ----------------------
	// The source location of `decl` (declared in record `ri` of the document
	// compile). An interface declaration maps to its dependency source declaration;
	// an unmappable one yields an unset location (=> null).
	SourceLocation sourceLocation( const Symbol *decl, long ri ) const
	{
		const SourceLocation &loc = decl->getLocation();
		if ( !mR.interfaces.isInterfacePath( loc.file ) && !endsWith( loc.file, ".bmod" ) )
			return isNavigableSource( loc ) ? loc : SourceLocation();
		std::string owner;
		if ( const auto *fn = dynamic_cast<const FunctionDefinition *>( decl ) )
			if ( const StructDefinition *s = ownerOf( fn, ri ) )
				owner = s->getName();
		const Symbol *src = mR.interfaces.toSource( decl, loc.file, owner );
		if ( src == nullptr || !isNavigableSource( src->getLocation() ) )
			return SourceLocation();
		return src->getLocation();
	}

	// ---- display names (FR-008; design.md D4 + addendum) --------------------
	std::string display( const Symbol *def, long ri ) const
	{
		const std::string &name = def->getName();
		const ModuleRecord *rec = record( ri );
		if ( rec == nullptr || isViewing( ri ) || mR.context->isPreludeProvider( (std::size_t)ri ) )
			return name;   // core / the viewer's own module / prelude: bare
		// Bound (F1): the viewer's import bindings of the defining record IN THIS
		// compile (an interface record's name comes from its <name>.bmod path).
		std::string best;
		if ( mViewing != kNone && mR.context->records()[ mViewing ].module != nullptr )
		{
			for ( const auto &impSp : mR.context->records()[ mViewing ].module->getImports() )
			{
				const ImportStatement *imp = (const ImportStatement *)impSp;
				if ( imp == nullptr || imp->getModuleName() != rec->moduleName )
					continue;
				const std::string &q = imp->getLocalQualifier();
				if ( best.empty() || q < best )
					best = q;   // ties: the lexicographically smallest qualifier
			}
		}
		if ( !best.empty() )
			return best + "." + name;
		// Unbound (use-capability only; F2): the defining module's HUMAN name — the
		// struct's getDefiningFile(), exactly what a .bmod `// foreign-type:` header
		// prints (BmodEmitter.cpp) — never the record's moduleName for a struct.
		std::string human;
		if ( const auto *sd = dynamic_cast<const StructDefinition *>( def ) )
		{
			human = sd->getDefiningFile();
			if ( human.empty() )
				human = "?";   // as the header renders an unstamped struct
		}
		else
			human = rec->moduleName;   // non-struct: no stamped human name exists
		return human + "." + name;
	}

	// A type named inside a foreign signature, rendered relative to the viewer
	// (FR-008 rule 6). `declRi` is the record that declared the signature.
	std::string typeText( Type *t, const std::set<std::string> &generics, long declRi ) const
	{
		if ( t == nullptr )
			return "void";
		if ( t->isFunctionType() )
		{
			std::ostringstream out;
			out << *t;   // fn-typed parameters: the declared spelling
			return out.str();
		}
		std::string out = t->getName();
		if ( !generics.count( out ) )
		{
			const Symbol *def = nullptr;
			if ( const ModuleRecord *rec = record( declRi ) )
				if ( rec->scope != nullptr )
				{
					Symbol *sym = rec->scope->findSymbol( out );
					if ( dynamic_cast<StructDefinition *>( sym ) || dynamic_cast<EnumDefinition *>( sym ) ||
						 dynamic_cast<ProtocolDefinition *>( sym ) )
						def = sym;
				}
			if ( def == nullptr )
				def = uniqueUseCapabilityType( out );
			if ( def != nullptr )
				out = display( def, recordForFile( def->getLocation().file ) );
		}
		if ( t->getNumTypeParams() > 0 )
		{
			out += "<";
			for ( int i = 0; i < t->getNumTypeParams(); i++ )
			{
				if ( i > 0 )
					out += ", ";
				out += typeText( t->getTypeParam( i ), generics, declRi );
			}
			out += ">";
		}
		return out;
	}

	static std::string genericList( const std::vector<GenericParam> &gps )
	{
		if ( gps.empty() )
			return "";
		std::string out = "<";
		for ( size_t i = 0; i < gps.size(); i++ )
			out += ( i > 0 ? ", " : "" ) + gps[i].mName;
		return out + ">";
	}

	// The foreign hover renderer (research R6). `decl` is the declaration the
	// document compile resolved (its signature text is the interface's verbatim
	// copy of the source's); `ri` its record.
	std::string render( const Symbol *decl, long ri ) const
	{
		if ( const auto *sd = dynamic_cast<const StructDefinition *>( decl ) )
			return "struct " + display( sd, ri ) + genericList( sd->getGenericParams() );
		if ( const auto *ed = dynamic_cast<const EnumDefinition *>( decl ) )
			return "enum " + display( ed, ri ) + genericList( ed->getGenericParams() );
		if ( const auto *pd = dynamic_cast<const ProtocolDefinition *>( decl ) )
			return "protocol " + display( pd, ri );
		const auto *cfn = dynamic_cast<const FunctionDefinition *>( decl );
		if ( cfn == nullptr )
			return "";
		FunctionDefinition *fn = const_cast<FunctionDefinition *>( cfn );
		std::set<std::string> generics;
		for ( const auto &g : fn->getGenericParams() )
			generics.insert( g.mName );
		std::string name;
		if ( const StructDefinition *owner = ownerOf( fn, ri ) )
		{
			for ( const auto &g : owner->getGenericParams() )
				generics.insert( g.mName );
			name = display( owner, ri ) + "." + fn->getName();
		}
		else
			name = display( fn, ri );
		std::string out = "fn " + name + genericList( fn->getGenericParams() ) + "(";
		for ( int i = 0; i < fn->getNumberParams(); i++ )
		{
			if ( i > 0 )
				out += ", ";
			VariableDefinition *param = fn->getParam( i );
			if ( param == nullptr )
				continue;
			Type *pt = param->getVariableType();
			if ( pt != nullptr && pt->getName() == "self" )
				out += "self";
			else
				out += typeText( pt, generics, ri ) + " " + param->getName();
		}
		out += ")";
		if ( fn->getReturnType() != nullptr )
			out += " -> " + typeText( fn->getReturnType(), generics, ri );
		return out;
	}

	const CompileResult &mR;

private:
	void loadUseCapability() const
	{
		if ( mLoaded )
			return;
		mLoaded = true;
		mR.context->useCapabilityTypes( mStructs, mEnums );
	}

	static constexpr std::size_t kNone = (std::size_t)-1;
	const std::string &mDocPath;
	std::size_t mViewing = kNone;
	mutable bool mLoaded = false;
	mutable std::vector<SmartPtr<StructDefinition>> mStructs;
	mutable std::vector<SmartPtr<EnumDefinition>> mEnums;
};

// The declaration a located node resolved to (definition-carrying kinds whose
// target can live in another module), else null.
const Symbol *declOf( const Statement *node )
{
	Statement *n = const_cast<Statement *>( node );
	if ( auto *e = dynamic_cast<CallExpression *>( n ) )
		return e->getFunction();
	if ( auto *e = dynamic_cast<FunctionRefExpression *>( n ) )
		return e->getFunction();
	if ( auto *e = dynamic_cast<ConstructExpression *>( n ) )
		return e->getStructDef();
	if ( auto *e = dynamic_cast<EnumConstructExpression *>( n ) )
		return e->getEnumDef();
	if ( auto *e = dynamic_cast<MethodCallExpression *>( n ) )
		return e->getResolvedMethod();
	return nullptr;
}

// A resolved foreign target, or nothing (=> the pre-U4 same-module path).
struct Target
{
	const Symbol *decl = nullptr;
	long record = -1;
	bool foreign = false;
};

// Contract steps 1-3: what the cursor names, when it names something declared
// outside the viewer's own file.
Target foreignTarget( const Nav &nav, const std::string &docPath, uint32_t line, uint32_t col,
	const Statement *&node )
{
	Target t;
	node = nullptr;
	// Step 1: a type name in type position — answered ONLY for a foreign target
	// (accepted decision: same-file type positions keep their pre-U4 null).
	if ( const Symbol *ref = nav.typeReferenceAt( line, col ) )
	{
		if ( ref->getLocation().file != docPath )
		{
			t.decl = ref;
			t.record = nav.recordForFile( ref->getLocation().file );
			t.foreign = true;
			return t;
		}
	}
	// Step 2: the existing locator.
	node = AstLocator::locate( (const Module *)nav.mR.module, line, col );
	const Symbol *decl = declOf( node );
	// Step 3: a method on a value held by use-capability only (Sema left it
	// unresolved because the viewer cannot name the receiver's type).
	if ( decl == nullptr )
		if ( const auto *mc = dynamic_cast<const MethodCallExpression *>( node ) )
			decl = nav.useCapabilityMethod( mc );
	if ( decl == nullptr || decl->getLocation().file.empty() || decl->getLocation().file == docPath )
		return t;   // nothing foreign: the pre-U4 path answers
	t.decl = decl;
	t.record = nav.recordForFile( decl->getLocation().file );
	t.foreign = !nav.isViewing( t.record );
	return t;
}

} // namespace

const Symbol *InterfaceSourceMap::toSource( const Symbol *decl, const std::string &file,
	const std::string &owner ) const
{
	if ( decl == nullptr )
		return nullptr;
	const std::string &name = decl->getName();
	const auto *fn = dynamic_cast<const FunctionDefinition *>( decl );
	bool isInit = fn != nullptr && fn->isInit();

	// Matches in one dependency source compile: same kind, same name, and — for a
	// method/init — the same owning struct. Only that run's SOURCE modules.
	auto collect = [&]( const Run &run, std::vector<const Symbol *> &out ) {
		if ( run.ctx == nullptr )
			return;
		for ( std::size_t ri : run.ctx->moduleIndices() )
		{
			const ModuleRecord &rec = run.ctx->record( ri );
			if ( rec.role == ModuleRole::kInterface || rec.module == nullptr )
				continue;
			const Module *m = rec.module;
			if ( fn != nullptr && owner.empty() )
			{
				for ( const auto &f : m->getFunctionList() )
					if ( f->getName() == name )
						out.push_back( f );
			}
			else if ( fn != nullptr )
			{
				for ( const auto &sp : m->getStructList() )
				{
					if ( sp->getName() != owner )
						continue;
					StructDefinition *s = const_cast<StructDefinition *>( (const StructDefinition *)sp );
					const FunctionDefinition *mm = isInit ? s->getInitMethod() : s->findMethod( name );
					if ( mm != nullptr )
						out.push_back( mm );
				}
			}
			else if ( dynamic_cast<const StructDefinition *>( decl ) )
			{
				for ( const auto &s : m->getStructList() )
					if ( s->getName() == name )
						out.push_back( s );
			}
			else if ( dynamic_cast<const EnumDefinition *>( decl ) )
			{
				for ( const auto &e : m->getEnumList() )
					if ( e->getName() == name )
						out.push_back( e );
			}
			else if ( dynamic_cast<const ProtocolDefinition *>( decl ) )
			{
				for ( const auto &p : m->getProtocolList() )
					if ( p->getName() == name )
						out.push_back( p );
			}
		}
	};

	std::vector<const Symbol *> matches;
	auto it = byInterfacePath.find( file );
	if ( it != byInterfacePath.end() && it->second != nullptr )
		collect( *it->second, matches );
	if ( matches.empty() )
	{
		// research R2 risk: a body attributed to another closure interface —
		// search every dependency compile, still requiring a unique match.
		for ( const auto &kv : byInterfacePath )
			if ( kv.second != nullptr )
				collect( *kv.second, matches );
	}
	return matches.size() == 1 ? matches.front() : nullptr;
}

NavigationAnswer definitionAt( const CompileResult &result, const std::string &docPath,
	uint32_t line, uint32_t col )
{
	NavigationAnswer a;
	Nav nav( result, docPath );
	if ( !nav.usable() )
		return a;
	const Statement *node = nullptr;
	Target t = foreignTarget( nav, docPath, line, col, node );
	SourceLocation loc;
	if ( t.decl != nullptr )
		loc = nav.sourceLocation( t.decl, t.record );   // unmappable => unset => null
	else if ( !AstLocator::definitionLocation( node, loc ) )
		return a;   // pre-U4 path: no definition pointer / builtin target
	if ( !isNavigableSource( loc ) )
		return a;   // G1: never a .bmod, never a fileless location
	a.found = true;
	a.location = loc;
	return a;
}

NavigationAnswer hoverAt( const CompileResult &result, const std::string &docPath,
	uint32_t line, uint32_t col )
{
	NavigationAnswer a;
	Nav nav( result, docPath );
	if ( !nav.usable() )
		return a;
	const Statement *node = nullptr;
	Target t = foreignTarget( nav, docPath, line, col, node );
	if ( t.decl != nullptr && t.foreign )
		a.hover = nav.render( t.decl, t.record );
	else
		a.hover = AstLocator::hoverText( node );   // pre-U4 renderer, unchanged
	a.found = !a.hover.empty();
	return a;
}

} // namespace lsp
