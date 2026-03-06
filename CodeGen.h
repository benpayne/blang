#ifndef BLANG_CODEGEN_H_
#define BLANG_CODEGEN_H_

#include <string>
#include <map>
#include <set>
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
	llvm::Value *genSpawnStatement( SpawnStatement *spawn );
	void genWaitStatement( WaitStatement *wait );
	void genWaitAllStatement( WaitAllStatement *waitAll );
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
	llvm::StructType *instantiateGenericStruct(
		StructDefinition *genericDef,
		const std::vector<SmartPtr<Type>> &typeArgs );
	std::string mangleGenericName(
		const std::string &baseName,
		const std::vector<SmartPtr<Type>> &typeArgs );
	llvm::Function *instantiateGenericFunction(
		FunctionDefinition *genericDef,
		const std::vector<SmartPtr<Type>> &typeArgs );
	llvm::Value *genStructLiteral( StructLiteralExpression *expr );
	llvm::Value *genFieldAccess( FieldAccessExpression *expr );
	llvm::Value *genMethodCall( MethodCallExpression *expr );

	// Builtin type method/field codegen
	llvm::Value *genStringMethodCall( MethodCallExpression *expr );
	llvm::Value *genArrayMethodCall( MethodCallExpression *expr );
	llvm::Value *genStringFieldAccess( FieldAccessExpression *expr );
	llvm::Value *genArrayFieldAccess( FieldAccessExpression *expr );

	// Enum tagged union codegen
	llvm::StructType *getOrCreateEnumType( EnumDefinition *enumDef );
	llvm::Value *genEnumConstruct( EnumConstructExpression *expr );
	bool enumHasPayload( EnumDefinition *enumDef );
	uint64_t getEnumMaxPayloadSize( EnumDefinition *enumDef );

	// Match and error handling codegen
	llvm::Value *genMatchExpression( MatchExpression *expr );
	llvm::Value *genTryExpression( TryExpression *expr );

	// Array codegen
	llvm::Value *genArrayLiteral( ArrayLiteralExpression *expr );
	llvm::Value *genIndexExpression( IndexExpression *expr );

	// Array runtime declarations
	llvm::Function *getOrDeclareArrayCreate();
	llvm::Function *getOrDeclareArrayCreateFromData();
	llvm::Function *getOrDeclareArrayRetain();
	llvm::Function *getOrDeclareArrayRelease();
	llvm::Function *getOrDeclareArrayGet();
	llvm::Function *getOrDeclareArraySet();
	llvm::Function *getOrDeclareArrayPush();
	llvm::Function *getOrDeclareArrayLength();
	llvm::Function *getOrDeclareArrayConcat();
	llvm::Function *getOrDeclareArrayCapacity();
	llvm::Function *getOrDeclareArrayIsEmpty();
	llvm::Function *getOrDeclareArrayPop();
	llvm::Function *getOrDeclareArrayClear();

	// Array type helper
	bool isArrayType( Expression *expr );
	int getElementSize( Type *elemType );

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

	// String runtime declarations
	llvm::Function *getOrDeclareStringCreate();
	llvm::Function *getOrDeclareStringCreateStatic();
	llvm::Function *getOrDeclareStringRetain();
	llvm::Function *getOrDeclareStringRelease();
	llvm::Function *getOrDeclareStringConcat();
	llvm::Function *getOrDeclareStringConcatMany();
	llvm::Function *getOrDeclareStringEquals();
	llvm::Function *getOrDeclareStringLength();
	llvm::Function *getOrDeclareStringCharAt();
	llvm::Function *getOrDeclareIntToString();
	llvm::Function *getOrDeclareFloatToString();
	llvm::Function *getOrDeclareBoolToString();
	llvm::Function *getOrDeclareStrlen();
	llvm::Function *getOrDeclareStringIsEmpty();
	llvm::Function *getOrDeclareStringContains();
	llvm::Function *getOrDeclareStringStartsWith();
	llvm::Function *getOrDeclareStringEndsWith();
	llvm::Function *getOrDeclareStringIndexOf();
	llvm::Function *getOrDeclareStringToUpper();
	llvm::Function *getOrDeclareStringToLower();
	llvm::Function *getOrDeclareStringTrim();
	llvm::Function *getOrDeclareStringByteAt();
	llvm::Function *getOrDeclareStringSubstring();
	llvm::Function *getOrDeclareStringReplace();
	llvm::Function *getOrDeclareStringToCstring();

	// String type helper
	bool isStringType( Expression *expr );

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
	llvm::Function *getOrDeclareSpawnWait();
	llvm::Function *getOrDeclareSpawnTaskDestroy();
	llvm::Function *getOrDeclareWaitAll();
	llvm::Function *getOrDeclareRuntimeShutdown();
	llvm::Function *getOrDeclareChanCreate();
	llvm::Function *getOrDeclareChanSend();
	llvm::Function *getOrDeclareChanRecv();
	llvm::Function *getOrDeclareChanClose();
	llvm::Function *getOrDeclareChanDestroy();
	llvm::Function *getOrDeclareAsyncCall();
	llvm::Function *getOrDeclareAwait();
	llvm::Function *getOrDeclareTaskDestroy();

	// JSON codegen (@json annotation)
	bool genJsonToJson( StructDefinition *structDef );
	bool genJsonFromJson( StructDefinition *structDef );

	// JSON runtime declarations
	llvm::Function *getOrDeclareJsonObject();
	llvm::Function *getOrDeclareJsonInt();
	llvm::Function *getOrDeclareJsonFloat();
	llvm::Function *getOrDeclareJsonString();
	llvm::Function *getOrDeclareJsonBool();
	llvm::Function *getOrDeclareJsonObjectSet();
	llvm::Function *getOrDeclareJsonObjectGet();
	llvm::Function *getOrDeclareJsonEncode();
	llvm::Function *getOrDeclareJsonDecode();
	llvm::Function *getOrDeclareJsonFree();
	llvm::Function *getOrDeclareJsonGetInt();
	llvm::Function *getOrDeclareJsonGetFloat();
	llvm::Function *getOrDeclareJsonGetString();
	llvm::Function *getOrDeclareJsonGetBool();

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
	std::map<std::string, llvm::StructType*> mEnumTypeMap;

	// Generic instantiation tracking
	std::map<std::string, llvm::StructType*> mGenericInstanceMap; // "Box_int" -> LLVM type
	std::map<std::string, llvm::Function*> mGenericFunctionMap;   // "identity_int" -> LLVM func
	std::map<std::string, Type*> mTypeSubstitution; // active during generic instantiation

	// Module scope for type resolution
	Scope *mScope = nullptr;
	QLang::Module *mQLangModule = nullptr;

	// Loop break/continue target stack (for nested loops)
	std::vector<std::pair<llvm::BasicBlock*, llvm::BasicBlock*>> mLoopStack;
	// Each entry is (continueBB, exitBB)

	// ARC tracking: shared/sync variables per scope depth
	std::vector<std::vector<llvm::AllocaInst*>> mArcScopeStack;

	// String refcount tracking: string variables per scope depth
	// Each entry is (alloca, varDef) so we can skip moved variables
	std::vector<std::vector<std::pair<llvm::AllocaInst*, VariableDefinition*>>> mStringScopeStack;

	// Array refcount tracking: array variables per scope depth
	std::vector<std::vector<std::pair<llvm::AllocaInst*, VariableDefinition*>>> mArrayScopeStack;

	// Ownership move tracking: own variables that have been moved
	std::set<VariableDefinition*> mMovedVariables;

	// Flag set when inside a loop body (for move-in-loop detection)
	bool mInsideLoop = false;

	// Set of own variables from outer scope when inside a spawn body
	std::set<VariableDefinition*> mSpawnOuterOwnVars;

	// Flag indicating module uses concurrency features
	bool mUsesConcurrency = false;

	// Flag indicating a codegen error occurred (e.g., ownership violation)
	bool mHasError = false;

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
