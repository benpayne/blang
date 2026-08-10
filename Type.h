
#ifndef BLANG_TYPE_H_
#define BLANG_TYPE_H_

#include <string>
#include <map>
#include <set>
#include <vector>

#include "RefCount.h"
#include "SourceLocation.h"

//#include "Expression.h"

class Lexer;

namespace QLang
{
	class Scope;
	class FunctionDefinition;
	class VariableDefinition;
	class Type;
	class Block;
	class Statement;
	class Expression;
	class CodeGen;
	class TestBlock;

	class Statement : virtual public RefCount
	{
	public:

		static Statement *Parse( Lexer &l, Scope *scope );

		// Source location of the construct's first token, stamped at parse
		// time. Every reachable AST node carries a set location (line/col
		// >= 1); see spec REQ-001 / FR-003.
		void setLocation( const SourceLocation &loc ) { mLocation = loc; }
		const SourceLocation &getLocation() const { return mLocation; }

	protected:
		Statement() {}
		SourceLocation mLocation;
		friend class CodeGen;
		friend class LocationDumper;
		friend class AstLocator;
		friend class Sema;
	};

	class Type : virtual public RefCount
	{
	public:
		Type( const std::string &name ) : mName( name ) {}

		static Type *Parse( Lexer &l, Scope *s, bool allow_void );

		const std::string &getName() const { return mName; }

		void addTypeParam( Type *param ) { mTypeParams.push_back( param ); }
		int getNumTypeParams() const { return mTypeParams.size(); }
		Type *getTypeParam( int i ) { return mTypeParams[ i ]; }

		virtual bool isFunctionType() const { return false; }

		void setLocation( const SourceLocation &loc ) { mLocation = loc; }
		const SourceLocation &getLocation() const { return mLocation; }

		friend std::ostream &operator<<(std::ostream &out, const Type &type);

	private:
		std::string mName;
		std::vector<SmartPtr<Type>> mTypeParams;
		SourceLocation mLocation;
		friend class LocationDumper;
		friend class AstLocator;
		friend class Sema;
	};

	class FunctionType : public Type
	{
	public:
		FunctionType() : Type( "fn" ) {}

		void setReturnType( Type *rt ) { mReturnType = rt; }
		Type *getReturnType() { return mReturnType; }

		void addParamType( Type *pt ) { mParamTypes.push_back( pt ); }
		int getNumParamTypes() const { return (int)mParamTypes.size(); }
		Type *getParamType( int i ) { return mParamTypes[i]; }

		bool isFunctionType() const override { return true; }

	private:
		SmartPtr<Type> mReturnType;  // nullptr = void
		std::vector<SmartPtr<Type>> mParamTypes;
		friend class CodeGen;
		friend class LocationDumper;
		friend class AstLocator;
		friend class Sema;
	};

	class Symbol : virtual public RefCount
	{
	public:
		Symbol( const std::string &name ) : mName( name ) {}

		static Symbol *Parse( Lexer &l, Scope *s );

		const std::string &getName() const { return mName; }

		enum SymbolType {
			TypeVariable,
			TypeFunction
		};

		virtual SymbolType getSymbolType() = 0;

		void setLocation( const SourceLocation &loc ) { mLocation = loc; }
		const SourceLocation &getLocation() const { return mLocation; }

	private:
		std::string mName;
		SourceLocation mLocation;
		friend class LocationDumper;
		friend class AstLocator;
		friend class Sema;
	};

	class Scope : virtual public RefCount
	{
	public:
		enum ScopeType {
			kScope_Global,
			kScope_Module,
			kScope_Namespace,
			kScope_Class,
			kScope_Function,
			kScope_Anonymous,
			kScope_IfElse,
			kScope_Loop,
		};

		Scope( ScopeType type = kScope_Anonymous ) : mType( type ) {}
		Scope( ScopeType type, const std::string &name ) : mType( type ) {}

		bool addSymbol( Symbol *sym )
		{
			if ( mSymbolList.find( sym->getName() ) != mSymbolList.end() )
				return false; // duplicate
			mSymbolList[ sym->getName() ] = sym;
			return true;
		}

		void addType( Type *type )
		{
			mTypeList[ type->getName() ] = type;
		}

		Symbol *findSymbol( const std::string &str )
		{
			SymbolListType::iterator i = mSymbolList.find( str );
			if ( i == mSymbolList.end() )
			{
				if ( mParent != nullptr )
					return mParent->findSymbol( str );
				else
					return nullptr;
			}
			else
				return (*i).second;
		}

		Type *findType( const std::string &str )
		{
			TypeListType::iterator i = mTypeList.find( str );
			if ( i == mTypeList.end() )
			{
				if ( mParent != nullptr )
					return mParent->findType( str );
				else
					return nullptr;
			}
			else
				return (*i).second;
		}

		void setParent( Scope *parent )
		{
			mParent = parent;
		}

		// Namespace support for module-qualified access (e.g. sys.args, net.Socket)
		void addNamespace( const std::string &name, Scope *ns )
		{
			mNamespaceMap[name] = ns;
		}

		Scope *findNamespace( const std::string &name )
		{
			auto it = mNamespaceMap.find( name );
			if ( it != mNamespaceMap.end() )
				return it->second;
			if ( mParent != nullptr )
				return mParent->findNamespace( name );
			return nullptr;
		}

		// modules-v2-graph U6b — marks a per-module namespace as a .bmod DEPENDENCY
		// export scope, whose exported TYPE names become namable (unqualified) in a
		// module that imports it (D7 name-capability). Combine-mode stdlib
		// namespaces (net/fs/timer) do NOT set this: their types are reached
		// QUALIFIED (`net.HttpServer`) and must not be dumped into the user scope.
		void setGrantsNameCapability( bool v ) { mGrantsNameCapability = v; }
		bool grantsNameCapability() const { return mGrantsNameCapability; }

		// modules-v2-graph U8 (DC10) — import aliasing. `import x as y;` binds the
		// local qualifier `y` to module `x`. addModuleAlias records y->x so codegen's
		// module-prefix fork (a combined-stdlib callee `y.foo` must emit `x__foo`,
		// not `y__foo`) can recover the real module. realModuleName returns the real
		// module for a qualifier (the qualifier itself when it is not an alias).
		void addModuleAlias( const std::string &alias, const std::string &real )
		{
			mModuleAliases[alias] = real;
		}
		std::string realModuleName( const std::string &qualifier )
		{
			auto it = mModuleAliases.find( qualifier );
			if ( it != mModuleAliases.end() )
				return it->second;
			if ( mParent != nullptr )
				return mParent->realModuleName( qualifier );
			return qualifier;
		}

		// Track which modules have been imported in this scope
		void addImportedModule( const std::string &name )
		{
			mImportedModules.insert( name );
		}

		bool isModuleImported( const std::string &name )
		{
			if ( mImportedModules.count( name ) > 0 )
				return true;
			if ( mParent != nullptr )
				return mParent->isModuleImported( name );
			return false;
		}

		// modules-v2-graph U6b-2 (DC8) — import-usage tracking for the unused-import
		// lint. A qualified access (`module.x`) or a bare imported-type reference
		// marks the module used; markModuleUsed walks up to the scope that actually
		// imported it (uses happen in nested block scopes).
		void markModuleUsed( const std::string &name )
		{
			if ( mImportedModules.count( name ) > 0 )
				mUsedModules.insert( name );
			else if ( mParent != nullptr )
				mParent->markModuleUsed( name );
		}
		bool wasModuleUsed( const std::string &name ) const
		{
			return mUsedModules.count( name ) > 0;
		}
		// Mark used the module that OWNS an imported bare type name (name-capability
		// use — e.g. `Pair<int>` after `import mathlib;`).
		void markTypeUsed( const std::string &typeName )
		{
			auto it = mImportedTypeOwner.find( typeName );
			if ( it != mImportedTypeOwner.end() )
				markModuleUsed( it->second );
			else if ( mParent != nullptr )
				mParent->markTypeUsed( typeName );
		}
		const std::set<std::string> &importedModules() const { return mImportedModules; }
		// Enumerate this scope's OWN registered namespaces (for the "did you mean
		// module.name?" suggestion — searching which module exports a given name).
		const std::map<std::string, SmartPtr<Scope>> &namespaceMap() const
		{
			return mNamespaceMap;
		}
		const std::map<std::string, SmartPtr<Symbol>> &ownSymbols() const
		{
			return mSymbolList;
		}

		// modules-v2-graph U6b — D7 name-capability. On `import module;` the driver
		// copies the module namespace's exported TYPE names (struct/enum) into the
		// importing scope so they resolve UNQUALIFIED (`Pair<int> p`, `Counter(5)`).
		// FUNCTIONS and PROTOCOLS are deliberately NOT copied: a dependency function
		// stays reachable only as `module.name` (the U6a import enforcement), so
		// bringing it unqualified would regress that. Only the namespace's OWN
		// entries move (its parent is gScope, already visible); an entry the
		// importing scope already defines is NOT overwritten (a local definition
		// shadows an import).
		// `moduleName` is the dependency's import qualifier. Ownership is recorded so
		// the unused-import lint can attribute a bare type use.
		//
		// DUPLICATE exported name across two imported modules (DC8, P2): two modules
		// may legitimately export the same name (D4 — qualifiers are per-module), and
		// importing both is fine *as long as the bare name is never used* (the U1
		// `boxapp` scenario: two libraries each export `Box`, the consumer uses both
		// via qualified functions and never names `Box`). So a collision is NOT an
		// import-time error — instead the name is UNBOUND (so it can't silently bind
		// to whichever module was imported first, a P10-class trap) and recorded as
		// ambiguous; a *bare use* of it is a located error (QType.cpp) that names the
		// exporting modules. A name matching a LOCAL definition is shadowing, not a
		// collision (mImportedTypeOwner tracks only imported names).
		void importTypeNamesFrom( Scope *src, const std::string &moduleName )
		{
			if ( src == nullptr )
				return;
			for ( auto &kv : src->mSymbolList )
			{
				if ( kv.second != nullptr &&
					 kv.second->getSymbolType() == Symbol::TypeFunction )
					continue; // functions + protocols stay qualified-only
				auto ambIt = mAmbiguousImports.find( kv.first );
				if ( ambIt != mAmbiguousImports.end() )
				{
					ambIt->second.insert( moduleName ); // already ambiguous, extend
					continue;
				}
				auto ownerIt = mImportedTypeOwner.find( kv.first );
				if ( ownerIt != mImportedTypeOwner.end() &&
					 ownerIt->second != moduleName )
				{
					// Collision: unbind and mark ambiguous (error only on bare use).
					std::set<std::string> mods{ ownerIt->second, moduleName };
					mAmbiguousImports[kv.first] = mods;
					mSymbolList.erase( kv.first );
					mTypeList.erase( kv.first );
					mImportedTypeOwner.erase( kv.first );
					continue;
				}
				if ( mSymbolList.find( kv.first ) == mSymbolList.end() )
				{
					mSymbolList[kv.first] = kv.second;
					mImportedTypeOwner[kv.first] = moduleName;
				}
			}
			for ( auto &kv : src->mTypeList )
			{
				if ( mAmbiguousImports.count( kv.first ) )
					continue;
				if ( mTypeList.find( kv.first ) == mTypeList.end() )
					mTypeList[kv.first] = kv.second;
			}
		}

		// modules-v2-graph U6b-2 (DC8, D3) — "did you mean module.name?" support.
		// Search all reachable module namespaces for one that exports `name` (as its
		// OWN symbol, so gScope builtins don't match), returning that module's
		// qualifier or "" if none. Used to sharpen a bare-name resolution failure
		// into a D3-rendered `module.name` suggestion.
		std::string moduleExporting( const std::string &name )
		{
			for ( auto &kv : mNamespaceMap )
			{
				Scope *ns = kv.second;
				if ( ns != nullptr &&
					 ns->mSymbolList.find( name ) != ns->mSymbolList.end() )
					return kv.first;
			}
			if ( mParent != nullptr )
				return mParent->moduleExporting( name );
			return "";
		}

		// The set of modules that export an ambiguous imported name (empty if the
		// name is not ambiguous). Walks up to the scope that holds the import table.
		std::set<std::string> ambiguousImportModules( const std::string &name )
		{
			auto it = mAmbiguousImports.find( name );
			if ( it != mAmbiguousImports.end() )
				return it->second;
			if ( mParent != nullptr )
				return mParent->ambiguousImportModules( name );
			return {};
		}

	private:
		typedef std::map<std::string, SmartPtr<Symbol> > SymbolListType;
		typedef std::map<std::string, SmartPtr<Type> > TypeListType;

		ScopeType	mType;
		SmartPtr<Scope> mParent;
		SymbolListType mSymbolList;
		TypeListType mTypeList;
		std::map<std::string, SmartPtr<Scope>> mNamespaceMap;
		std::set<std::string> mImportedModules;
		std::set<std::string> mUsedModules;
		std::map<std::string, std::string> mImportedTypeOwner;
		std::map<std::string, std::set<std::string>> mAmbiguousImports;
		std::map<std::string, std::string> mModuleAliases;
		bool mGrantsNameCapability = false;
	};

	struct GenericParam
	{
		std::string mName;
		std::string mConstraint; // protocol constraint, empty if unconstrained
	};

	struct AnnotationNode
	{
		std::string mName;                     // e.g., "json", "grpc", "db", "drop", "graphql"
		std::vector<std::string> mArgs;        // e.g., for @db("analytics") -> ["analytics"]
	};

	class ImportStatement : virtual public RefCount
	{
	public:
		ImportStatement( const std::string &moduleName ) : mModuleName( moduleName ) {}

		// The real module being imported (`x` in `import x as y;`).
		const std::string &getModuleName() const { return mModuleName; }

		// modules-v2-graph U8 (DC10) — import aliasing. The LOCAL qualifier the
		// consumer uses: `y` in `import x as y;`, else the module name itself. All
		// consumer-facing resolution + diagnostics key on this; `getModuleName()`
		// (the real module) is only used to find the module's actual namespace.
		void setAlias( const std::string &alias ) { mAlias = alias; }
		const std::string &getAlias() const { return mAlias; }
		const std::string &getLocalQualifier() const
		{
			return mAlias.empty() ? mModuleName : mAlias;
		}

		void setLocation( const SourceLocation &loc ) { mLocation = loc; }
		const SourceLocation &getLocation() const { return mLocation; }

	private:
		std::string mModuleName;
		std::string mAlias;
		SourceLocation mLocation;
		friend class LocationDumper;
		friend class AstLocator;
		friend class Sema;
	};

	class StructDefinition;
	class EnumDefinition;
	class ProtocolDefinition;

	class Module : virtual public RefCount
	{
	public:

		static Module *Parse( Lexer &l, Scope *s );

		const std::vector<SmartPtr<FunctionDefinition>> &getFunctionList() const { return mFunctionList; }
		const std::vector<SmartPtr<ImportStatement>> &getImports() const { return mImports; }
		const std::vector<SmartPtr<StructDefinition>> &getStructList() const { return mStructList; }
		const std::vector<SmartPtr<EnumDefinition>> &getEnumList() const { return mEnumList; }
		const std::vector<SmartPtr<ProtocolDefinition>> &getProtocolList() const { return mProtocolList; }
		const std::vector<SmartPtr<TestBlock>> &getTestBlocks() const { return mTestBlocks; }

		bool isExtern() const { return mIsExtern; }
		void setExtern( bool isExtern ) { mIsExtern = isExtern; }

		// modules-v2-graph U6b-3 (DC9/KI-23) — this module's identity (its source
		// file's basename, e.g. "net", "main"), stamped by stampDefiningOrigin in
		// lockstep with the per-struct getDefiningFile(). Sema compares it against a
		// struct's defining module so a field reach-in from a DIFFERENT module is a
		// located error — keyed on module-of-definition, not the .bmod-arrival
		// isFromInterface() heuristic (which misses the combine-mode stdlib case).
		const std::string &getDefiningFile() const { return mDefiningFile; }
		void setDefiningFile( const std::string &f ) { mDefiningFile = f; }

	private:
		Module() {}
		std::string mDefiningFile;
		friend class CodeGen;
		friend class LocationDumper;
		friend class AstLocator;
		friend class Sema;

		std::vector<SmartPtr<FunctionDefinition> > mFunctionList;
		std::vector<SmartPtr<ImportStatement>> mImports;
		std::vector<SmartPtr<StructDefinition>> mStructList;
		std::vector<SmartPtr<EnumDefinition>> mEnumList;
		std::vector<SmartPtr<ProtocolDefinition>> mProtocolList;
		std::vector<SmartPtr<TestBlock>> mTestBlocks;
		SmartPtr<Scope> mScope;
		bool mIsExtern = false;
	};

	class FunctionDefinition : public Symbol
	{
	public:

		// When deferBody is true, the signature is parsed and registered but the
		// body block is skipped (its start position recorded in mBodyPos) so it
		// can be parsed later via ParseDeferredBody. This lets Module::Parse
		// register every top-level function signature before parsing any body,
		// enabling forward references and mutual recursion.
		static FunctionDefinition *Parse( Lexer &l, Scope *s, bool isExtern = false,
			bool isPublic = false, bool deferBody = false );
		static FunctionDefinition *ParseInit( Lexer &l, Scope *s, bool isPublic = false );

		// Parse the previously-skipped body (see deferBody above) into this same
		// function object, using its already-built scope.
		void ParseDeferredBody( Lexer &l );
		bool hasDeferredBody() const { return mBodyPos >= 0; }

		// Create a builtin function definition (for compiler-provided builtins)
		static FunctionDefinition *CreateBuiltin( const std::string &name, Type *returnType,
			const std::vector<VariableDefinition*> &params, bool isVariadic = false )
		{
			FunctionDefinition *f = new FunctionDefinition( name );
			f->mReturnType = returnType;
			for ( auto *p : params )
				f->mParameters.push_back( p );
			f->mIsVariadic = isVariadic;
			f->mIsBuiltin = true;
			return f;
		}

		virtual Symbol::SymbolType getSymbolType() { return Symbol::TypeFunction; }

		friend std::ostream &operator<<(std::ostream &out, const FunctionDefinition &func);

		Type *getReturnType() { return mReturnType; }
		int getNumberParams() { return mParameters.size(); }
		Type *getParamType( int p );
		VariableDefinition *getParam( int p );
		bool isExtern() const { return mIsExtern; }
		void setFunctionExtern( bool e ) { mIsExtern = e; }
		bool isVariadic() const { return mIsVariadic; }
		bool isGeneric() const { return !mGenericParams.empty(); }
		bool isPublic() const { return mIsPublic; }
		bool isAsync() const { return mIsAsync; }
		bool isBuiltin() const { return mIsBuiltin; }
		bool isStatic() const { return mIsStatic; }
		void setStatic( bool s ) { mIsStatic = s; }
		bool isInit() const { return mIsInit; }
		const std::vector<GenericParam> &getGenericParams() const { return mGenericParams; }
		bool hasRequires() const { return !mRequiresClauses.empty(); }
		bool hasEnsures() const { return !mEnsuresClauses.empty(); }

		void setAnnotations( const std::vector<AnnotationNode> &annotations ) { mAnnotations = annotations; }
		const std::vector<AnnotationNode> &getAnnotations() const { return mAnnotations; }

	private:
		FunctionDefinition( const std::string &name ) : Symbol( name ) {}

		SmartPtr<Type> mReturnType;
		std::vector<SmartPtr<VariableDefinition> > mParameters;
		SmartPtr<Scope> mFuncScope;
		SmartPtr<Block> mFuncBody;
		bool mIsExtern = false;
		bool mIsVariadic = false;
		bool mIsPublic = false;
		bool mIsAsync = false;
		bool mIsBuiltin = false;
		bool mIsStatic = false;
		bool mIsInit = false;
		std::vector<GenericParam> mGenericParams;
		std::vector<SmartPtr<Expression>> mRequiresClauses;
		std::vector<SmartPtr<Expression>> mEnsuresClauses;
		std::vector<AnnotationNode> mAnnotations;
		int mBodyPos = -1;   // lexer position of a deferred body's '{', else -1

		friend class CodeGen;
		friend class LocationDumper;
		friend class AstLocator;
		friend class Sema;
		friend class Module;
	};

	enum class OwnershipQualifier {
		kOwnership_Value,    // default: stack-allocated value type
		kOwnership_Own,      // own: single owner, move semantics
		kOwnership_Shared,   // shared: reference-counted
		kOwnership_Sync,     // sync: synchronized, auto-locked
	};

	class VariableDefinition : public Symbol
	{
	public:
		VariableDefinition( Type *type, const std::string &name ) : Symbol( name ), mType( type ) {}

		static VariableDefinition *ParseFuncParam( Lexer &l, Scope *s, bool isExtern = false, int paramIndex = 0 );

		virtual Symbol::SymbolType getSymbolType() { return Symbol::TypeVariable; }

		friend std::ostream &operator<<(std::ostream &out, const VariableDefinition &var);

		Type *getVariableType() { return mType; }
		const Type *getVariableType() const { return mType; }
		void setType( Type *type ) { mType = type; }

		bool isConst() const { return mIsConst; }
		void setConst( bool isConst ) { mIsConst = isConst; }

		OwnershipQualifier getOwnership() const { return mOwnership; }
		void setOwnership( OwnershipQualifier ownership ) { mOwnership = ownership; }
		bool isMoved() const { return mIsMoved; }
		void setMoved( bool moved ) { mIsMoved = moved; }

	private:
		SmartPtr<Type>	mType;
		bool mIsConst = false;
		OwnershipQualifier mOwnership = OwnershipQualifier::kOwnership_Value;
		bool mIsMoved = false;
	};

	class StructDefinition : public Symbol
	{
	public:

		static StructDefinition *Parse( Lexer &l, Scope *s, bool isPublic = false );
		static void ParseImplBlock( Lexer &l, Scope *s );

		virtual Symbol::SymbolType getSymbolType() { return Symbol::TypeVariable; }

		void addMethod( FunctionDefinition *method ) { mMethods.push_back( method ); }
		void setInitMethod( FunctionDefinition *method ) { mInitMethod = method; }
		FunctionDefinition *getInitMethod() { return mInitMethod; }
		bool hasInit() const { return mInitMethod != nullptr; }
		bool isGeneric() const { return !mGenericParams.empty(); }
		bool isPublic() const { return mIsPublic; }
		bool isTable() const { return mIsTable; }
		const std::vector<GenericParam> &getGenericParams() const { return mGenericParams; }
		void setIsTable( bool isTable ) { mIsTable = isTable; }

		// Provenance: true when this definition arrived through a parsed .bmod
		// interface rather than from .b source in the module being compiled.
		// Primarily an ABI predicate — it answers "must construction go through
		// the library-emitted factory?".
		//
		// U5 ALSO reads it for the field/literal visibility rules (D9), but ONLY
		// in rules that are themselves scoped to ".bmod-arrival": field access and
		// struct literals on a `.bmod`-arrived struct are located errors. That is
		// not the general-visibility overload this comment once warned against —
		// the rule's scope IS exactly what the flag holds. It is deliberately
		// `.bmod`-path-only: a namespaced-stdlib struct (net/fs/timer) arrives as
		// parsed .b source, so isFromInterface() is false for it and its
		// field/literal privacy stays grep-gated this epic (known-issues KI-23,
		// closed by Epic B's per-module scopes). Do NOT reuse this flag for a
		// GENERAL "is this visible here?" test — use module identity (Epic B).
		bool isFromInterface() const { return mFromInterface; }
		void setFromInterface( bool v ) { mFromInterface = v; }

		// Protocols this struct conforms to via `impl Protocol for Struct`.
		// Recorded so the .bmod can carry conformance records across a module
		// boundary (design record D16): without them a consumer cannot dispatch
		// `print("{}", foreignValue)` through Printable, and a foreign type
		// cannot satisfy a generic constraint.
		void addConformedProtocol( const std::string &name )
		{
			for ( const auto &p : mConformedProtocols )
				if ( p == name )
					return;
			mConformedProtocols.push_back( name );
		}
		const std::vector<std::string> &getConformedProtocols() const
		{
			return mConformedProtocols;
		}

		// Which SOURCE FILE defines this type. A visibility-flavoured predicate,
		// deliberately distinct from isFromInterface(), which is an ABI predicate
		// answering "must construction go through the library-emitted factory?".
		//
		// They are not interchangeable: the namespaced stdlib modules (net, fs,
		// timer, ...) have a real module boundary but arrive as parsed .b source,
		// so isFromInterface() is false for them. Reusing the ABI flag for
		// visibility would silently exempt the entire stdlib from every
		// visibility rule.
		//
		// NAMED FOR WHAT IT HOLDS: this is the file's base name, not a module
		// identity. A library split across several .b files yields several
		// distinct values, so a module-private rule keyed directly on this would
		// reject legal intra-library access. The unit that enforces visibility
		// must map file -> module (project name for a library, module name for a
		// namespaced stdlib module) rather than compare these strings; and it
		// must populate the mapping on the blangd path too (lsp/Compile.cpp),
		// which does not set this today, or qcc and the LSP will disagree.
		// Canonical module identity is Epic B's (D5).
		const std::string &getDefiningFile() const { return mDefiningFile; }
		void setDefiningFile( const std::string &f ) { mDefiningFile = f; }

		// Canonical module-identity digest (modules-v2-graph U1, D5/D10): a short
		// SHA-256 (12 hex / 48 bits) of the DEFINING module's canonical origin
		// (realpath of its project dir; url@pin for git). Empty for builtins and
		// for definitions whose origin the driver did not supply. Generic symbol
		// mangling (mangleGenericName) incorporates it so two same-named exported
		// generic types get DISTINCT mangled symbols instead of collapsing onto one
		// linkonce_odr symbol (P10). Unlike mDefiningFile (a bare file base name)
		// this is a real cross-module identity, stamped by the driver from the
		// module's resolved origin — own module and each directly-imported dep alike
		// (clarity-note source (a)); a foreign type reached only transitively gets
		// its digest from the .bmod carrier in U5 (source (b)).
		const std::string &getModuleDigest() const { return mModuleDigest; }
		void setModuleDigest( const std::string &d ) { mModuleDigest = d; }

		void setAnnotations( const std::vector<AnnotationNode> &annotations ) { mAnnotations = annotations; }
		const std::vector<AnnotationNode> &getAnnotations() const { return mAnnotations; }

		const std::vector<SmartPtr<FunctionDefinition> > &getMethods() const { return mMethods; }
		const std::vector<SmartPtr<VariableDefinition> > &getFields() const { return mFields; }

	private:
		StructDefinition( const std::string &name ) : Symbol( name ) {}

		std::vector<SmartPtr<VariableDefinition> > mFields;
		std::vector<SmartPtr<FunctionDefinition> > mMethods;
		SmartPtr<FunctionDefinition> mInitMethod;
		std::vector<GenericParam> mGenericParams;
		std::vector<AnnotationNode> mAnnotations;
		bool mIsPublic = false;
		bool mIsTable = false;
		bool mFromInterface = false;
		std::vector<std::string> mConformedProtocols;
		std::string mDefiningFile;
		std::string mModuleDigest;
		friend class CodeGen;
		friend class LocationDumper;
		friend class AstLocator;
		friend class Sema;
	};

	class ProtocolDefinition : public Symbol
	{
	public:

		static ProtocolDefinition *Parse( Lexer &l, Scope *s, bool isPublic = false );

		// Create a builtin protocol definition
		static ProtocolDefinition *CreateBuiltin( const std::string &name,
			const std::vector<FunctionDefinition*> &methods )
		{
			ProtocolDefinition *p = new ProtocolDefinition( name );
			// A builtin protocol is in scope everywhere without being declared or
			// imported, so it is exported by definition. Marking it public keeps
			// that fact in ONE place: P9 enforcement asks `isPublic()` of every
			// type an exported signature names, and a builtin that answered
			// "private" would reject `impl Printable for MyStruct` in every
			// library — while the .bmod emitter, which special-cases the name,
			// happily emitted the record. Two rules for one fact is how that kind
			// of contradiction survives.
			p->mIsPublic = true;
			for ( auto *m : methods )
				p->mRequiredMethods.push_back( m );
			return p;
		}

		virtual Symbol::SymbolType getSymbolType() { return Symbol::TypeFunction; }

		bool isGeneric() const { return !mGenericParams.empty(); }
		bool isPublic() const { return mIsPublic; }
		const std::vector<GenericParam> &getGenericParams() const { return mGenericParams; }

		const std::vector<SmartPtr<FunctionDefinition> > &getRequiredMethods() const { return mRequiredMethods; }

	private:
		ProtocolDefinition( const std::string &name ) : Symbol( name ) {}

		std::vector<SmartPtr<FunctionDefinition> > mRequiredMethods;
		std::vector<GenericParam> mGenericParams;
		bool mIsPublic = false;
		friend class CodeGen;
		friend class LocationDumper;
		friend class AstLocator;
		friend class Sema;
	};

	class EnumDefinition : public Symbol
	{
	public:

		struct Variant
		{
			std::string mName;
			std::vector<SmartPtr<Type>> mAssociatedTypes;
			// Position of the variant's name token. Unset (0:0) only for the
			// compiler-built Option/Result builtins, which have no source.
			SourceLocation mLocation;
		};

		static EnumDefinition *Parse( Lexer &l, Scope *s, bool isPublic = false );

		// Built-in generic Option<T> { some(T), none }.
		static EnumDefinition *CreateBuiltinOption()
		{
			EnumDefinition *e = new EnumDefinition( "Option" );
			GenericParam t; t.mName = "T";
			e->mGenericParams.push_back( t );

			Variant some; some.mName = "some";
			some.mAssociatedTypes.push_back( new Type( "T" ) );
			e->mVariants.push_back( some );

			Variant none; none.mName = "none";
			e->mVariants.push_back( none );

			e->mIsPublic = true;
			return e;
		}

		// Built-in generic Result<T, E> { ok(T), err(E) }.
		static EnumDefinition *CreateBuiltinResult()
		{
			EnumDefinition *e = new EnumDefinition( "Result" );
			GenericParam t; t.mName = "T";
			GenericParam er; er.mName = "E";
			e->mGenericParams.push_back( t );
			e->mGenericParams.push_back( er );

			Variant ok; ok.mName = "ok";
			ok.mAssociatedTypes.push_back( new Type( "T" ) );
			e->mVariants.push_back( ok );

			Variant err; err.mName = "err";
			err.mAssociatedTypes.push_back( new Type( "E" ) );
			e->mVariants.push_back( err );

			e->mIsPublic = true;
			return e;
		}

		virtual Symbol::SymbolType getSymbolType() { return Symbol::TypeVariable; }

		bool isGeneric() const { return !mGenericParams.empty(); }
		bool isPublic() const { return mIsPublic; }
		const std::vector<GenericParam> &getGenericParams() const { return mGenericParams; }

		const std::vector<Variant> &getVariants() const { return mVariants; }
		int getNumVariants() const { return mVariants.size(); }

		void setAnnotations( const std::vector<AnnotationNode> &annotations ) { mAnnotations = annotations; }
		const std::vector<AnnotationNode> &getAnnotations() const { return mAnnotations; }

	private:
		EnumDefinition( const std::string &name ) : Symbol( name ) {}

		std::vector<Variant> mVariants;
		std::vector<GenericParam> mGenericParams;
		std::vector<AnnotationNode> mAnnotations;
		bool mIsPublic = false;
		friend class CodeGen;
		friend class LocationDumper;
		friend class AstLocator;
		friend class Sema;
	};

	class TestBlock : virtual public RefCount
	{
	public:
		TestBlock( const std::string &name ) : mName( name ) {}

		static TestBlock *Parse( Lexer &l, Scope *s );

		const std::string &getName() const { return mName; }

		void setLocation( const SourceLocation &loc ) { mLocation = loc; }
		const SourceLocation &getLocation() const { return mLocation; }

	private:
		std::string mName;
		SmartPtr<Block> mBody;
		SourceLocation mLocation;
		friend class CodeGen;
		friend class LocationDumper;
		friend class AstLocator;
		friend class Sema;
	};

};

#endif // BLANG_TYPE_H_
