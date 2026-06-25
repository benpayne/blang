
#ifndef BLANG_TYPE_H_
#define BLANG_TYPE_H_

#include <string>
#include <map>
#include <set>
#include <vector>

#include "RefCount.h"

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

	protected:
		Statement() {}
		friend class CodeGen;
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

		friend std::ostream &operator<<(std::ostream &out, const Type &type);

	private:
		std::string mName;
		std::vector<SmartPtr<Type>> mTypeParams;
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

	private:
		std::string mName;
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

	private:
		typedef std::map<std::string, SmartPtr<Symbol> > SymbolListType;
		typedef std::map<std::string, SmartPtr<Type> > TypeListType;

		ScopeType	mType;
		SmartPtr<Scope> mParent;
		SymbolListType mSymbolList;
		TypeListType mTypeList;
		std::map<std::string, SmartPtr<Scope>> mNamespaceMap;
		std::set<std::string> mImportedModules;
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

		const std::string &getModuleName() const { return mModuleName; }

	private:
		std::string mModuleName;
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

		bool isExtern() const { return mIsExtern; }
		void setExtern( bool isExtern ) { mIsExtern = isExtern; }

	private:
		Module() {}
		friend class CodeGen;

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

		static FunctionDefinition *Parse( Lexer &l, Scope *s, bool isExtern = false, bool isPublic = false );

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
		std::vector<GenericParam> mGenericParams;
		std::vector<SmartPtr<Expression>> mRequiresClauses;
		std::vector<SmartPtr<Expression>> mEnsuresClauses;
		std::vector<AnnotationNode> mAnnotations;

		friend class CodeGen;
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
		void setVariableType( Type *t ) { mType = t; }

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
		bool isGeneric() const { return !mGenericParams.empty(); }
		bool isPublic() const { return mIsPublic; }
		bool isTable() const { return mIsTable; }
		const std::vector<GenericParam> &getGenericParams() const { return mGenericParams; }
		void setIsTable( bool isTable ) { mIsTable = isTable; }

		void setAnnotations( const std::vector<AnnotationNode> &annotations ) { mAnnotations = annotations; }
		const std::vector<AnnotationNode> &getAnnotations() const { return mAnnotations; }

		const std::vector<SmartPtr<FunctionDefinition> > &getMethods() const { return mMethods; }
		const std::vector<SmartPtr<VariableDefinition> > &getFields() const { return mFields; }

	private:
		StructDefinition( const std::string &name ) : Symbol( name ) {}

		std::vector<SmartPtr<VariableDefinition> > mFields;
		std::vector<SmartPtr<FunctionDefinition> > mMethods;
		std::vector<GenericParam> mGenericParams;
		std::vector<AnnotationNode> mAnnotations;
		bool mIsPublic = false;
		bool mIsTable = false;
		friend class CodeGen;
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
	};

	class EnumDefinition : public Symbol
	{
	public:

		struct Variant
		{
			std::string mName;
			std::vector<SmartPtr<Type>> mAssociatedTypes;
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
	};

	class TestBlock : virtual public RefCount
	{
	public:
		TestBlock( const std::string &name ) : mName( name ) {}

		static TestBlock *Parse( Lexer &l, Scope *s );

		const std::string &getName() const { return mName; }

	private:
		std::string mName;
		SmartPtr<Block> mBody;
		friend class CodeGen;
	};

};

#endif // BLANG_TYPE_H_
