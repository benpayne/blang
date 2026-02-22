#ifndef BLANG_CODEGEN_H_
#define BLANG_CODEGEN_H_

#include <string>
#include <map>
#include <memory>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"

#include "Type.h"
#include "Expression.h"

namespace QLang
{

class CodeGen
{
public:
	CodeGen( const std::string &moduleName );
	~CodeGen();

	bool generate( Module *mod );
	void print( llvm::raw_ostream &os );
	bool verify();

private:
	// Top-level generators
	llvm::Function *genFunction( FunctionDefinition *func );
	void genBlock( Block *block );

	// Statement generators
	void genStatement( Statement *stmt );
	void genVariableDeclaration( VariableDeclaration *decl );
	void genReturnStatement( ReturnStatement *ret );
	void genIfStatement( IfStatement *ifStmt );
	void genWhileStatement( WhileStatement *whileStmt );
	void genForStatement( ForStatement *forStmt );

	// Expression generators (return llvm::Value*)
	llvm::Value *genExpression( Expression *expr );
	llvm::Value *genCallExpression( CallExpression *call );
	llvm::Value *genVariableExpression( VariableExpression *var );
	llvm::Value *genOperationsExpression( OperationsExpression *ops );
	llvm::Value *genAssignmentExpression( AssignmentExpression *assign );
	llvm::Value *genConstInteger( ConstInteger *ci );
	llvm::Value *genConstFloat( ConstFloat *cf );
	llvm::Value *genConstString( ConstString *cs );
	llvm::Value *genConstChar( ConstChar *cc );

	// Type mapping
	llvm::Type *getLLVMType( Type *type );

	// LLVM state
	std::unique_ptr<llvm::LLVMContext> mContext;
	std::unique_ptr<llvm::Module> mModule;
	std::unique_ptr<llvm::IRBuilder<>> mBuilder;

	// Maps from AST nodes to LLVM values
	std::map<VariableDefinition*, llvm::AllocaInst*> mVariableMap;
	std::map<FunctionDefinition*, llvm::Function*> mFunctionMap;
};

} // namespace QLang

#endif // BLANG_CODEGEN_H_
