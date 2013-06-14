
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
	
	class Statement : virtual public RefCount
	{
	public:

		static Statement *Parse( Lexer &l, Scope *scope );

	protected:
		Statement() {}	
	};
		
	class Type : virtual public RefCount
	{
	public:
		Type( const std::string &name ) : mName( name ) {}
		
		static Type *Parse( Lexer &l, Scope *s, bool allow_void );
		
		const std::string &getName() const { return mName; }

		friend std::ostream &operator<<(std::ostream &out, const Type &type);
		
	private:
		std::string mName;
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
		
		void addSymbol( Symbol *sym )
		{
			mSymbolList[ sym->getName() ] = sym;
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
				if ( mParent != NULL )
					return mParent->findSymbol( str );
				else
					return NULL;
			}
			else
				return (*i).second;
		}
		
		Type *findType( const std::string &str )
		{
			TypeListType::iterator i = mTypeList.find( str );
			if ( i == mTypeList.end() )
			{
				if ( mParent != NULL )
					return mParent->findType( str );
				else
					return NULL;
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
	
	class Module : virtual public RefCount
	{
	public:
		
		static Module *Parse( Lexer &l, Scope *s );
		
	private:
		Module() {}
		
		std::vector<SmartPtr<FunctionDefinition> > mFunctionList;
	};
	
	class FunctionDefinition : public Symbol
	{
	public:
		
		static FunctionDefinition *Parse( Lexer &l, Scope *s );

		virtual Symbol::SymbolType getSymbolType() { return Symbol::TypeFunction; }
		
		friend std::ostream &operator<<(std::ostream &out, const FunctionDefinition &func);

		Type *getReturnType() { return mReturnType; }
		int getNumberParams() { return mParameters.size(); }
		Type *getParamType( int p );
		VariableDefinition *getParam( int p );
		
	private:
		FunctionDefinition( const std::string &name ) : Symbol( name ) {}

		SmartPtr<Type> mReturnType;
		std::vector<SmartPtr<VariableDefinition> > mParameters;
		SmartPtr<Scope> mFuncScope;
		SmartPtr<Block> mFuncBody;
	};
	
	class VariableDefinition : public Symbol
	{
	public:
		VariableDefinition( Type *type, const std::string &name ) : Symbol( name ), mType( type ) {}

		static VariableDefinition *ParseFuncParam( Lexer &l, Scope *s );

		virtual Symbol::SymbolType getSymbolType() { return Symbol::TypeVariable; }
		
		friend std::ostream &operator<<(std::ostream &out, const VariableDefinition &var);
		
		Type *getVariableType() { return mType; }
		
	private:		
		SmartPtr<Type>	mType;
	};
		
};

#endif // BLANG_TYPE_H_
