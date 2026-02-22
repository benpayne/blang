
#ifndef BLANG_TYPE_H_
#define BLANG_TYPE_H_

#include <string>
#include <map>
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
	class CodeGen;

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

		friend std::ostream &operator<<(std::ostream &out, const Type &type);

	private:
		std::string mName;
		std::vector<SmartPtr<Type>> mTypeParams;
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
		
		Scope( ScopeType type = kScope_Anonymous ) {}
		Scope( ScopeType type, const std::string &name ) {}
		
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
		
	private:
		typedef std::map<std::string, SmartPtr<Symbol> > SymbolListType;
		typedef std::map<std::string, SmartPtr<Type> > TypeListType;
		
		ScopeType	mType;
		SmartPtr<Scope> mParent;		
		SymbolListType mSymbolList;
		TypeListType mTypeList;
	};
	
	struct GenericParam
	{
		std::string mName;
		std::string mConstraint; // protocol constraint, empty if unconstrained
	};

	class ImportStatement : virtual public RefCount
	{
	public:
		ImportStatement( const std::string &moduleName ) : mModuleName( moduleName ) {}

		const std::string &getModuleName() const { return mModuleName; }

	private:
		std::string mModuleName;
	};

	class Module : virtual public RefCount
	{
	public:

		static Module *Parse( Lexer &l, Scope *s );

	private:
		Module() {}
		friend class CodeGen;

		std::vector<SmartPtr<FunctionDefinition> > mFunctionList;
		std::vector<SmartPtr<ImportStatement>> mImports;
	};
	
	class FunctionDefinition : public Symbol
	{
	public:

		static FunctionDefinition *Parse( Lexer &l, Scope *s, bool isExtern = false, bool isPublic = false );

		virtual Symbol::SymbolType getSymbolType() { return Symbol::TypeFunction; }

		friend std::ostream &operator<<(std::ostream &out, const FunctionDefinition &func);

		Type *getReturnType() { return mReturnType; }
		int getNumberParams() { return mParameters.size(); }
		Type *getParamType( int p );
		VariableDefinition *getParam( int p );
		bool isExtern() const { return mIsExtern; }
		bool isVariadic() const { return mIsVariadic; }
		bool isGeneric() const { return !mGenericParams.empty(); }
		bool isPublic() const { return mIsPublic; }

	private:
		FunctionDefinition( const std::string &name ) : Symbol( name ) {}

		SmartPtr<Type> mReturnType;
		std::vector<SmartPtr<VariableDefinition> > mParameters;
		SmartPtr<Scope> mFuncScope;
		SmartPtr<Block> mFuncBody;
		bool mIsExtern = false;
		bool mIsVariadic = false;
		bool mIsPublic = false;
		std::vector<GenericParam> mGenericParams;

		friend class CodeGen;
	};

	class VariableDefinition : public Symbol
	{
	public:
		VariableDefinition( Type *type, const std::string &name ) : Symbol( name ), mType( type ) {}

		static VariableDefinition *ParseFuncParam( Lexer &l, Scope *s, bool isExtern = false, int paramIndex = 0 );

		virtual Symbol::SymbolType getSymbolType() { return Symbol::TypeVariable; }

		friend std::ostream &operator<<(std::ostream &out, const VariableDefinition &var);

		Type *getVariableType() { return mType; }

		bool isConst() const { return mIsConst; }
		void setConst( bool isConst ) { mIsConst = isConst; }

	private:
		SmartPtr<Type>	mType;
		bool mIsConst = false;
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

		const std::vector<SmartPtr<FunctionDefinition> > &getMethods() const { return mMethods; }

	private:
		StructDefinition( const std::string &name ) : Symbol( name ) {}

		std::vector<SmartPtr<VariableDefinition> > mFields;
		std::vector<SmartPtr<FunctionDefinition> > mMethods;
		std::vector<GenericParam> mGenericParams;
		bool mIsPublic = false;
		friend class CodeGen;
	};

	class ProtocolDefinition : public Symbol
	{
	public:

		static ProtocolDefinition *Parse( Lexer &l, Scope *s );

		virtual Symbol::SymbolType getSymbolType() { return Symbol::TypeFunction; }

		bool isGeneric() const { return !mGenericParams.empty(); }

		const std::vector<SmartPtr<FunctionDefinition> > &getRequiredMethods() const { return mRequiredMethods; }

	private:
		ProtocolDefinition( const std::string &name ) : Symbol( name ) {}

		std::vector<SmartPtr<FunctionDefinition> > mRequiredMethods;
		std::vector<GenericParam> mGenericParams;
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

		static EnumDefinition *Parse( Lexer &l, Scope *s );

		virtual Symbol::SymbolType getSymbolType() { return Symbol::TypeVariable; }

		bool isGeneric() const { return !mGenericParams.empty(); }

	private:
		EnumDefinition( const std::string &name ) : Symbol( name ) {}

		std::vector<Variant> mVariants;
		std::vector<GenericParam> mGenericParams;
		friend class CodeGen;
	};

};

#endif // BLANG_TYPE_H_
