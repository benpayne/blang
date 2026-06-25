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
#include "FormatString.h"

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

	// Set a module prefix for name mangling (e.g. "sys" → functions become "sys__funcName")
	void setModulePrefix( const std::string &prefix ) { mModulePrefix = prefix; }

	// Multi-module support: register struct/enum defs from other modules
	void registerExternalTypes(
		const std::vector<SmartPtr<StructDefinition>> &structs,
		const std::vector<SmartPtr<EnumDefinition>> &enums );

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
	llvm::Value *genFieldAssignment( FieldAssignmentExpression *expr );
	llvm::Value *genIndexAssignment( IndexAssignmentExpression *expr );
	llvm::Value *genMethodCall( MethodCallExpression *expr );

	// Builtin type method/field codegen
	llvm::Value *genStringMethodCall( MethodCallExpression *expr );
	llvm::Value *genArrayMethodCall( MethodCallExpression *expr );
	llvm::Value *genBufferMethodCall( MethodCallExpression *expr );
	llvm::Value *genStringFieldAccess( FieldAccessExpression *expr );
	llvm::Value *genArrayFieldAccess( FieldAccessExpression *expr );
	llvm::Value *genBufferFieldAccess( FieldAccessExpression *expr );

	// Enum tagged union codegen
	llvm::StructType *getOrCreateEnumType( EnumDefinition *enumDef );
	llvm::Value *genEnumConstruct( EnumConstructExpression *expr );
	bool enumHasPayload( EnumDefinition *enumDef );
	uint64_t getEnumMaxPayloadSize( EnumDefinition *enumDef );

	// Match and error handling codegen
	llvm::Value *genMatchExpression( MatchExpression *expr );
	llvm::Value *genTryExpression( TryExpression *expr );

	// Try operator helper: resolve the QLang enum type of an expression
	EnumDefinition *resolveExpressionEnumDef( Expression *expr );

	// Resolve the full QLang type (with type arguments) of an expression, used to
	// recover concrete types for generic enum variant payloads (Option<T>/Result<T,E>).
	Type *resolveExpressionQType( Expression *expr );
	// Resolve the concrete QLang type of a variant's first associated type,
	// substituting the enum's generic params from `subjectType`'s type arguments.
	Type *resolveVariantBindingQType( EnumDefinition *enumDef, int variantIdx, Type *subjectType );

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
	llvm::Function *getOrDeclareArraySetElemDtor();

	// Set element destructor on an array based on element type name
	void emitArrayElemDtor( llvm::Value *arrayPtr, const std::string &elemTypeName );

	// Array type helper
	bool isArrayType( Expression *expr );
	int getElementSize( Type *elemType );

	// Buffer type helper
	bool isBufferType( Expression *expr );

	// Channel type helpers and method codegen (chan<T> .send()/.recv()/.close())
	bool isChanType( Expression *expr );
	Type *getChanElementQType( Expression *expr );
	llvm::Value *genChanMethodCall( MethodCallExpression *expr );

	// Buffer runtime declarations
	llvm::Function *getOrDeclareBufferCreate();
	llvm::Function *getOrDeclareBufferCreateFromString();
	llvm::Function *getOrDeclareBufferRetain();
	llvm::Function *getOrDeclareBufferRelease();
	llvm::Function *getOrDeclareBufferLength();
	llvm::Function *getOrDeclareBufferCapacity();
	llvm::Function *getOrDeclareBufferIsEmpty();
	llvm::Function *getOrDeclareBufferGet();
	llvm::Function *getOrDeclareBufferSet();
	llvm::Function *getOrDeclareBufferAppendByte();
	llvm::Function *getOrDeclareBufferAppendBytes();
	llvm::Function *getOrDeclareBufferAppendString();
	llvm::Function *getOrDeclareBufferIndexOf();
	llvm::Function *getOrDeclareBufferSlice();
	llvm::Function *getOrDeclareBufferToString();
	llvm::Function *getOrDeclareBufferToStringRange();
	llvm::Function *getOrDeclareBufferClear();
	llvm::Function *getOrDeclareBufferCompact();

	// Break/continue codegen
	void genBreakStatement();
	void genContinueStatement();

	// String interpolation codegen
	llvm::Value *genStringInterpolation( StringInterpolation *interp );

	// Pipeline expression codegen
	llvm::Value *genPipelineExpression( PipelineExpression *pipeline );

	// Builtin print/println codegen
	void genPrintCall( CallExpression *call, bool appendNewline );

	// Print runtime declarations
	llvm::Function *getOrDeclarePrintBlang();
	llvm::Function *getOrDeclarePrintNewline();
	llvm::Function *getOrDeclarePrintFlush();
	llvm::Function *getOrDeclareIntToStringFmt();
	llvm::Function *getOrDeclareFloatToStringFmt();
	llvm::Function *getOrDeclareCharToString();

	// Lambda, function reference, and indirect call codegen
	llvm::Value *genLambdaExpression( LambdaExpression *lambda );
	llvm::Value *genFunctionRefExpression( FunctionRefExpression *funcRef );
	llvm::Value *genIndirectCallExpression( IndirectCallExpression *indCall );

	// Lambda capture analysis: walk AST and collect referenced VariableDefinitions
	void collectReferencedVars( Statement *stmt, std::set<VariableDefinition*> &vars );

	// Lambda context destructor generation (releases captured refcounted types)
	llvm::Function *genLambdaDestructor(
		const std::string &name,
		llvm::StructType *ctxType,
		const std::vector<std::pair<VariableDefinition*, llvm::AllocaInst*>> &captures,
		const std::vector<llvm::Type*> &captureTypes );

	// Lambda context lifetime runtime declarations
	llvm::Function *getOrDeclareLambdaCtxRetain();
	llvm::Function *getOrDeclareLambdaCtxRelease();

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

	// Field type resolution helper (for FieldAccessExpression)
	std::string getFieldTypeName( FieldAccessExpression *fa );
	Type *getFieldType( FieldAccessExpression *fa );

	// Memory allocation helpers
	llvm::Function *getOrDeclareMalloc();
	llvm::Function *getOrDeclareBlangAlloc();
	llvm::Function *getOrDeclareFree();

	// BLang runtime library declarations
	llvm::Function *getOrDeclareRcAlloc();
	llvm::Function *getOrDeclareRcAllocSync();
	llvm::Function *getOrDeclareRcRetain();
	llvm::Function *getOrDeclareRcRelease();
	llvm::Function *getOrDeclareSyncLock();
	llvm::Function *getOrDeclareSyncUnlock();
	llvm::Function *getOrDeclareSysInit();
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

	// Maps self parameters to their owning struct definition
	std::map<VariableDefinition*, StructDefinition*> mSelfStructMap;
	// Maps self parameters to the mangled struct name (for generic struct methods)
	std::map<VariableDefinition*, std::string> mSelfStructMangledName;
	std::map<std::string, EnumDefinition*> mEnumDefMap;
	// Owns enums synthesized at codegen time (e.g. built-in Option/Result).
	std::vector<SmartPtr<EnumDefinition>> mSyntheticEnums;
	// Owns QLang types synthesized at codegen time (e.g. Option<T> for chan recv).
	std::vector<SmartPtr<Type>> mSyntheticTypes;
	std::map<std::string, llvm::StructType*> mEnumTypeMap;

	// Generic instantiation tracking
	std::map<std::string, llvm::StructType*> mGenericInstanceMap; // "Box_int" -> LLVM type
	std::map<std::string, llvm::Function*> mGenericFunctionMap;   // "identity_int" -> LLVM func
	std::map<std::string, Type*> mTypeSubstitution; // active during generic instantiation

	// Lambda/callback thunk cache: named function -> thunk wrapper
	std::map<std::string, llvm::Function*> mThunkMap;
	int mLambdaCounter = 0;

	// Module scope for type resolution
	Scope *mScope = nullptr;
	QLang::Module *mQLangModule = nullptr;

	// Module prefix for namespace name mangling (empty for user code)
	std::string mModulePrefix;

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

	// Buffer refcount tracking: buffer variables per scope depth
	std::vector<std::vector<std::pair<llvm::AllocaInst*, VariableDefinition*>>> mBufferScopeStack;

	// Lambda/fn-typed variable tracking: release ctx on scope exit
	std::vector<std::vector<std::pair<llvm::AllocaInst*, VariableDefinition*>>> mLambdaScopeStack;

	// Struct variable tracking: heap-allocated struct variables per scope depth.
	// All user-defined structs are heap-allocated via __blang_rc_alloc and refcounted.
	// At scope exit, __blang_rc_release is called, which invokes the destructor to
	// release refcounted fields when the refcount reaches zero.
	std::vector<std::vector<llvm::AllocaInst*>> mStructScopeStack;

	// Generate a destructor function for a struct type that releases refcounted fields.
	// Returns nullptr if the struct has no refcounted fields.
	llvm::Function *getOrGenStructDestructor( StructDefinition *sd,
		const std::map<std::string, std::string> &typeSub );

	// Cache of generated struct destructor functions
	std::map<std::string, llvm::Function*> mStructDtorMap;

	// Check if a type name refers to a user-defined struct (not a builtin)
	bool isUserStructType( const std::string &typeName );

	// Enum variable tracking: release refcounted payloads at scope exit.
	// Each entry pairs an alloca with the enum definition for tag-based cleanup.
	std::vector<std::vector<std::pair<llvm::AllocaInst*, EnumDefinition*>>> mEnumScopeStack;

	// Emit cleanup code for an enum variable's refcounted payload
	void emitEnumPayloadRelease( llvm::AllocaInst *alloca, EnumDefinition *enumDef );

	// Runtime declaration for __blang_rc_alloc_dtor
	llvm::Function *getOrDeclareRcAllocDtor();

	// Temporary string tracking: strings created during expression evaluation
	// (concat results, string literals in expressions, method return values)
	// that need to be released after the enclosing statement completes.
	std::vector<llvm::Value*> mTempStrings;

	// Helper to register a temporary string for deferred release
	void trackTempString( llvm::Value *val );

	// Release and clear all tracked temporary strings
	void releaseTempStrings();

	// Remove a value from the temp list (when it becomes owned by a variable)
	void untrackTempString( llvm::Value *val );

	// Temporary lambda context tracking: inline lambdas (not stored to a variable)
	// that need their context released after the enclosing statement completes.
	std::vector<llvm::Value*> mTempLambdaCtxs;
	void trackTempLambdaCtx( llvm::Value *ctxPtr );
	void releaseTempLambdaCtxs();
	void untrackTempLambdaCtx( llvm::Value *ctxPtr );

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

	// Hint for empty array literals: set by variable declaration handler
	// to the LLVM element type from the Array<T> type parameter.
	// Reset to nullptr after use by genArrayLiteral.
	llvm::Type *mArrayElemTypeHint = nullptr;
	std::string mArrayElemTypeNameHint;  // semantic type name for array element dtor
};

} // namespace QLang

#endif // BLANG_CODEGEN_H_
