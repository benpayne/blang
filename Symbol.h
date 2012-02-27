#ifndef BLANG_SYMBOL_H_
#define BLANG_SYMBOL_H_

#include <string>
#include <vector>
#include "llvm/Type.h"
#include "llvm/Value.h"

namespace BLang
{		
	class Symbol
	{
	public:				
		static Symbol *CreateSymbol( llvm::Type *type, const std::string &name )
		{
			return new Symbol( type, name );
		}
		
		static Symbol *CreateType( llvm::Type *type, const std::string &name )
		{
			return new Symbol( type, name );
		}

		virtual ~Symbol() {}
		
		llvm::Type *getType() { return mType; }
		std::string &getName() { return mName; }
		
		void setValue( llvm::Value *v ) { mValue = v; }
		llvm::Value  *getValue() { return mValue; }
		
	private:
		Symbol( llvm::Type *type, const std::string &name ) : mType( type ), mName( name ) {}

		llvm::Type *mType;
		llvm::Value *mValue;
		std::string mName;
	};
};

#endif // BLANG_SYMBOL_H_
