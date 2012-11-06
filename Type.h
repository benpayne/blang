
#ifndef BLANG_TYPE_H_
#define BLANG_TYPE_H_

#include <string>
#include <map>

#include "RefCount.h"

class Lexer;

namespace QLang
{
	class Scope;
	
	class Type : public RefCount
	{
	public:
		Type( std::string &name ) : mName( name ) {}
		
		static Type *Parse( Lexer &l, Scope &s, bool allow_void );
		
		const std::string &getName() { return mName; }

	private:
		std::string mName;
	};

	class Symbol : public RefCount
	{
	public:
		Symbol( std::string &name, Type *t ) : mName( name ), mType( t ) {}
		
		static Symbol *Parse( Lexer &l, Scope &s );
		
		const std::string &getName() { return mName; }

	private:
		std::string mName;
		SmartPtr<Type>	mType;
	};
		
	class Scope : public RefCount
	{
	public:
		enum ScopeType {
			kScope_Global,
			kScope_Module,
			kScope_Anonymous,
			kScope_Namespace,
			kScope_Class,
			kScope_Function,
			kScope_IfElse
		};
		
		Scope( ScopeType type = kScope_Anonymous ) {}
		Scope( ScopeType type, std::string &name ) {}
		
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
				return NULL;
			else
				return (*i).second;
		}
		
		Type *findType( const std::string &str )
		{
			TypeListType::iterator i = mTypeList.find( str );
			if ( i == mTypeList.end() )
				return NULL;
			else
				return (*i).second;
		}
		
	private:
		typedef std::map<std::string, SmartPtr<Symbol> > SymbolListType;
		typedef std::map<std::string, SmartPtr<Type> > TypeListType;
		
		ScopeType	mType;
	
		SymbolListType mSymbolList;
		TypeListType mTypeList;
	};
};

#endif // BLANG_TYPE_H_
