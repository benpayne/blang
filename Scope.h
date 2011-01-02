#ifndef BLANG_SCOPE_H_
#define BLANG_SCOPE_H_

#include <string>
#include <vector>
#include "parse_helpers.h"
#include "llvm/BasicBlock.h"
#include "llvm/Function.h"

namespace BLang
{
	class Scope
	{
	public:		
		Scope( int type = SCOPE_ANONYMOUS ) : mType( type ), mParentScope( NULL ), mBlock( NULL ), mFunc( NULL ) {}
		Scope( int type, std::string &name ) : mType( type ), mName( name ), mParentScope( NULL ), mBlock( NULL ), mFunc( NULL ) {}
		
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
			
			if ( mParentScope != NULL )
				return mParentScope->findSymbol( name );
			else
				return NULL;
		}
		
		Symbol *findType( const std::string &name )
		{
			for( unsigned i = 0; i < mTypes.size(); i++ )
			{
				if ( mTypes[ i ]->getName() == name )
					return mTypes[ i ];
			}
			
			if ( mParentScope != NULL )
				return mParentScope->findType( name );
			else
				return NULL;
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
			while ( s != NULL )
			{
				if ( s->mType != SCOPE_FUNCTION )
					s = s->mParentScope;
				else
					return s->mFunc;
			}
			
			return NULL;
		}
		
		void createBasicBlock( const char *name = "entry" )
		{
			llvm::Function *f = getFunction();
			
			if ( f == NULL )
				printf( "Failed to get Function\n" );
			
			mBlock = llvm::BasicBlock::Create( gContext, name, getFunction() );
		}

		void setFunction( llvm::Function *func )
		{
			mFunc = func;
		}
		
	private:
		int mType;
		std::string mName;
		std::vector<Symbol *> mTypes;
		std::vector<Symbol *> mSymbols;
		Scope	*mParentScope;
		llvm::BasicBlock	*mBlock;
		llvm::Function		*mFunc;
	};
	
};

#endif // BLANG_SCOPE_H_
