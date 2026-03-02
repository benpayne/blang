#ifndef BLANG_CODEGEN_H_
#define BLANG_CODEGEN_H_

#include <string>
#include <map>
#include <vector>
#include <memory>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"

#include "Type.h"
#include "Expression.h"
#include "SQLGen.h"

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

	// Phase 2 statement generators
	void genSpawnStatement( SpawnStatement *spawn );
	void genAssertStatement( AssertStatement *assertStmt );
	void genEventHandler( EventHandler *handler );

	// Expression generators (return llvm::Value*)
	llvm::Value *genExpression( Expression *expr );
	llvm::Value *genCallExpression( CallExpression *call );
	llvm::Value *genVariableExpression( VariableExpression *var );
	llvm::Value *genOperationsExpression( OperationsExpression *ops );
	llvm::Value *genAssignmentExpression( AssignmentExpression *assign );
	llvm::Value *genUnaryExpression( UnaryExpression *unary );
	llvm::Value *genConstInteger( ConstInteger *ci );
	llvm::Value *genConstFloat( ConstFloat *cf );
	llvm::Value *genConstString( ConstString *cs );
	llvm::Value *genConstChar( ConstChar *cc );

	// Struct and field codegen
	llvm::StructType *getOrCreateStructType( StructDefinition *structDef );
	llvm::Value *genStructLiteral( StructLiteralExpression *expr );
	llvm::Value *genFieldAccess( FieldAccessExpression *expr );
	llvm::Value *genMethodCall( MethodCallExpression *expr );

	// Match and error handling codegen
	llvm::Value *genMatchExpression( MatchExpression *expr );
	llvm::Value *genTryExpression( TryExpression *expr );

	// Array codegen
	llvm::Value *genArrayLiteral( ArrayLiteralExpression *expr );
	llvm::Value *genIndexExpression( IndexExpression *expr );

	// Break/continue codegen
	void genBreakStatement();
	void genContinueStatement();

	// String interpolation codegen
	llvm::Value *genStringInterpolation( StringInterpolation *interp );

	// Pipeline expression codegen
	llvm::Value *genPipelineExpression( PipelineExpression *pipeline );

	// Phase 2 expression generators
	llvm::Value *genAwaitExpression( AwaitExpression *await );

	// Phase 2 test block and contract codegen
	llvm::Function *genTestBlock( TestBlock *testBlock );
	void genTestRunner( const std::vector<llvm::Function*> &testFunctions,
		const std::vector<SmartPtr<TestBlock>> &testBlocks );
	void genContractCheck( Expression *condition, const std::string &message );

	// Runtime helper declarations
	llvm::Function *getOrDeclarePuts();
	llvm::Function *getOrDeclareExit();
	llvm::Function *getOrDeclarePrintf();

	// String helper declarations
	llvm::Function *getOrDeclareSnprintf();

	// Memory allocation helpers
	llvm::Function *getOrDeclareMalloc();

	// BLang runtime library declarations
	llvm::Function *getOrDeclareRcAlloc();
	llvm::Function *getOrDeclareRcAllocSync();
	llvm::Function *getOrDeclareRcRetain();
	llvm::Function *getOrDeclareRcRelease();
	llvm::Function *getOrDeclareSyncLock();
	llvm::Function *getOrDeclareSyncUnlock();
	llvm::Function *getOrDeclareRuntimeInit();
	llvm::Function *getOrDeclareSpawn();
	llvm::Function *getOrDeclareRuntimeShutdown();
	llvm::Function *getOrDeclareChanCreate();
	llvm::Function *getOrDeclareChanSend();
	llvm::Function *getOrDeclareChanRecv();
	llvm::Function *getOrDeclareChanClose();
	llvm::Function *getOrDeclareChanDestroy();
	llvm::Function *getOrDeclareAsyncCall();
	llvm::Function *getOrDeclareAwait();
	llvm::Function *getOrDeclareTaskDestroy();

	// Database query codegen
	llvm::Value *genQueryExpression( QueryExpression *query );
	llvm::Value *genInsertExpression( InsertExpression *insert );
	llvm::Value *genUpdateExpression( UpdateExpression *update );
	llvm::Value *genDeleteExpression( DeleteExpression *del );

	// Database runtime declarations
	llvm::Function *getOrDeclareDbQuery();
	llvm::Function *getOrDeclareDbExec();
	llvm::Function *getOrDeclareDbResultCount();
	llvm::Function *getOrDeclareDbResultGet();
	llvm::Function *getOrDeclareDbResultGetInt();
	llvm::Function *getOrDeclareDbResultFree();

	// ForIn codegen
	void genForInStatement( ForInStatement *forInStmt );

	// Helper to get the alloca for an expression's address (for GEP)
	llvm::AllocaInst *getExpressionAddress( Expression *expr );

	// Type mapping
	llvm::Type *getLLVMType( Type *type );

	// LLVM state
	std::unique_ptr<llvm::LLVMContext> mContext;
	std::unique_ptr<llvm::Module> mModule;
	std::unique_ptr<llvm::IRBuilder<>> mBuilder;

	// Maps from AST nodes to LLVM values
	std::map<VariableDefinition*, llvm::AllocaInst*> mVariableMap;
	std::map<FunctionDefinition*, llvm::Function*> mFunctionMap;

	// Struct type maps
	std::map<std::string, llvm::StructType*> mStructTypeMap;
	std::map<std::string, StructDefinition*> mStructDefMap;
	std::map<std::string, EnumDefinition*> mEnumDefMap;

	// Module scope for type resolution
	Scope *mScope = nullptr;
	QLang::Module *mQLangModule = nullptr;

	// Loop break/continue target stack (for nested loops)
	std::vector<std::pair<llvm::BasicBlock*, llvm::BasicBlock*>> mLoopStack;
	// Each entry is (continueBB, exitBB)

	// ARC tracking: shared/sync variables per scope depth
	std::vector<std::vector<llvm::AllocaInst*>> mArcScopeStack;

	// Flag indicating module uses concurrency features
	bool mUsesConcurrency = false;

	// Current function context (for contract support)
	FunctionDefinition *mCurrentFunction = nullptr;
	llvm::AllocaInst *mResultAlloca = nullptr;

	// Async wrapper context: when non-null, return statements should
	// store the value here and branch to mAsyncExitBB instead of ret
	llvm::AllocaInst *mAsyncResultAlloca = nullptr;
	llvm::BasicBlock *mAsyncExitBB = nullptr;
	llvm::Type *mAsyncReturnType = nullptr;
};

} // namespace QLang

#endif // BLANG_CODEGEN_H_
