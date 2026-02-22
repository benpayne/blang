#ifndef BLANG_SCOPE_H_
#define BLANG_SCOPE_H_

#include <string>
#include <vector>
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "Symbol.h"

namespace BLang
{
	class Scope
	{
	public:		
		enum ScopeType {
			SCOPE_GLOBAL,
			SCOPE_MODULE,
			SCOPE_ANONYMOUS,
			SCOPE_NAMESPACE,
			SCOPE_CLASS,
			SCOPE_FUNCTION,
			SCOPE_IF,
		};
		
		Scope( ScopeType type = SCOPE_ANONYMOUS ) : mType( type ), mParentScope( nullptr ), mBlock( nullptr ), mFunc( nullptr ) {}
		Scope( ScopeType type, std::string &name ) : mType( type ), mName( name ), mParentScope( nullptr ), mBlock( nullptr ), mFunc( nullptr ) {}
		
		virtual ~Scope() 
		{
			for( unsigned i = 0; i < mSymbols.size(); i++ )
			{
				delete mSymbols[ i ];
			}
			for( unsigned i = 0; i < mTypes.size(); i++ )
			{
				delete mTypes[ i ];
			}
		}
		
		Symbol *findSymbol( const std::string &name )
		{
			for( unsigned i = 0; i < mSymbols.size(); i++ )
			{
				if ( mSymbols[ i ]->getName() == name )
					return mSymbols[ i ];
			}
			
			if ( mParentScope != nullptr )
				return mParentScope->findSymbol( name );
			else
				return nullptr;
		}
		
		Symbol *findType( const std::string &name )
		{
			for( unsigned i = 0; i < mTypes.size(); i++ )
			{
				if ( mTypes[ i ]->getName() == name )
					return mTypes[ i ];
			}
			
			if ( mParentScope != nullptr )
				return mParentScope->findType( name );
			else
				return nullptr;
		}
		
		void addSymbol( Symbol *sym )
		{
			mSymbols.push_back( sym );
		}

		void addType( Symbol *type )
		{
			mTypes.push_back( type );			
		}
			
		void setParentScope( Scope *scope )
		{
			mParentScope = scope;
		}
		
		Scope *getParentScope()
		{
			return mParentScope;
		}
		
		llvm::BasicBlock *getBasicBlock()
		{
			return mBlock;
		}

		llvm::Function *getFunction()
		{
			Scope *s = this; 
			while ( s != nullptr )
			{
				if ( s->mType != SCOPE_FUNCTION )
					s = s->mParentScope;
				else
					return s->mFunc;
			}
			
			return nullptr;
		}
		
		void createBasicBlock( const char *name = "entry" )
		{
			llvm::Function *f = getFunction();
			
			if ( f == nullptr )
				printf( "Failed to get Function\n" );
			
			//mBlock = llvm::BasicBlock::Create( gContext, name, getFunction() );
		}

		void setFunction( llvm::Function *func )
		{
			mFunc = func;
		}
		
	private:
		ScopeType mType;
		std::string mName;
		std::vector<Symbol *> mTypes;
		std::vector<Symbol *> mSymbols;
		Scope	*mParentScope;
		llvm::BasicBlock	*mBlock;
		llvm::Function		*mFunc;
	};
	
};

#endif // BLANG_SCOPE_H_
