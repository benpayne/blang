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

// Forward declarations for the LLVM debug-info types (U3). The full definitions
// live in llvm/IR/DIBuilder.h + DebugInfoMetadata.h, included only in the .cpp
// files that emit DWARF — keeping this header light. unique_ptr<DIBuilder> with
// an incomplete type is fine because ~CodeGen() is defined in CodeGen.cpp where
// DIBuilder is complete.
namespace llvm
{
	class DIBuilder;
	class DICompileUnit;
	class DIFile;
	class DISubprogram;
}

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

	// Run the LLVM new-PassManager per-module optimization pipeline over the
	// generated module, in-process, before print(). `level` is one of
	// "0","1","2","3","s","z" (empty or "0" => O0). Returns false on an invalid
	// level (the driver reports it). This is layer 1 of the -O pipeline (IR
	// passes); llc -O<n> is layer 2 (backend). Target-independent — no
	// TargetMachine is required, so qcc needs no per-target backend libs.
	bool optimize( const std::string &level );

	// Raw LLVM verifier text captured by the most recent failing verify().
	// The driver surfaces this only under --debug-compiler (U2, FR-010).
	const std::string &getVerifyError() const { return mVerifyError; }

	// Set a module prefix for name mangling (e.g. "sys" → functions become "sys__funcName")
	void setModulePrefix( const std::string &prefix ) { mModulePrefix = prefix; }

	// Test-runner mode (qcc --emit-test-main). When set, a module carrying
	// test{} blocks emits a real main() that registers each test with the C
	// test driver and dispatches to __blang_test_main; asserts inside test mode
	// print a located <file>:<line>: diagnostic on failure. Off by default, so
	// normal (bcc build / single-file) codegen is byte-for-byte unchanged.
	void setTestMode( bool on ) { mTestMode = on; }

	// Enable DWARF debug info emission (qcc -g, U3). Off by default so a
	// non-`-g` build is byte-identical. Drives the DIBuilder setup in generate()
	// and the per-function/per-statement debug-location hooks.
	void setDebugInfo( bool on ) { mDebugInfo = on; }

	// Configure the default database connection opened in main() (from the
	// [database] section of blang.toml, forwarded by bcc via qcc flags).
	// An empty url leaves the connection to the runtime's lazy env fallback.
	void setDbConfig( const std::string &driver, const std::string &url )
	{
		mDbDriver = driver;
		mDbUrl = url;
	}

	// Register a named database connection for @db("name") routing.
	void addDbNamedConn( const std::string &name, const std::string &driver,
		const std::string &url )
	{
		mDbNamedConns.push_back( { name, driver, url } );
	}

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

	// Emit ARC releases for every in-scope local (shared/sync, string, array,
	// buffer, lambda ctx, struct, enum payload) across all open scopes. Shared
	// by genReturnStatement and the `?` operator's early-return error path so an
	// error propagated by `?` runs the same cleanup a normal return does
	// (otherwise locals live at the failing `?` leak).
	void emitScopeStackReleases();
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
	llvm::Value *genConstructExpression( ConstructExpression *expr );
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

	// Retain a refcounted element being stored into an array that owns its
	// elements (an elem_dtor was/will be set — see emitArrayElemDtor).
	// __blang_array_push does NOT retain, so pushing a refcounted value without
	// this leaves the array holding a reference it never took: the pushed
	// temporary/variable is released at scope exit while the array still points
	// at it, causing a dangling pointer and a double-free at array release.
	// Mirrors emitArrayElemDtor's per-type mapping (string/Array/Buffer/struct).
	// No-op for non-refcounted element types (int, byte, ...).
	void emitArrayElemRetain( llvm::Value *elemVal, const std::string &elemTypeName );

	// Array type helper
	bool isArrayType( Expression *expr );
	int getElementSize( Type *elemType );

	// Byte type helper (for unsigned extension)
	bool isByteExpression( Expression *expr );

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
	// Test-mode entry point (qcc --emit-test-main): emits main() that registers
	// each test with the C driver and returns __blang_test_main(argc, argv).
	void genTestMain( const std::vector<llvm::Function*> &testFunctions,
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
	llvm::Function *getOrDeclareStringCompare();
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

	// ---- Generic-context type resolution (generic ARC unit) ----
	// Resolve a declared type name through the ACTIVE monomorphization
	// substitution (mTypeSubstitution): inside sort<string>'s body, "T" -> string.
	// Identity outside generic instantiation. Every ARC decision that keys on a
	// declared type name (scope tracking, bind-retain, untrack, temp tracking)
	// must go through this so a T-typed local participates in refcounting.
	std::string resolvedTypeName( Type *t );
	// Resolve a generic function CALL's declared return type name to the
	// concrete name using the call's type arguments (explicit or inferred):
	// identity<string> declared "T" -> "string". Identity for non-generic calls.
	std::string callReturnTypeName( CallExpression *call );
	// Resolve a METHOD's declared return type name for a call on a generic
	// struct instance, mapping the struct's generic params through the object's
	// type arguments: Map<string,int>.get declared "V" -> "int". Falls back to
	// mTypeSubstitution, then the declared name.
	std::string methodReturnTypeName( MethodCallExpression *mc );
	// Infer a generic call's type arguments from its argument expressions'
	// resolved/declared types when the caller wrote no explicit <...> list.
	// Fills call->mTypeArgs; returns false (caller reports) if any generic
	// param stays unbound.
	bool inferCallTypeArgs( CallExpression *call, FunctionDefinition *funcDef );
	// Map a type declared inside a generic struct to the concrete form for a
	// given instance (Map<K,V>'s Array<K> -> Array<string> for Map<string,int>).
	// nullptr when nothing needed mapping.
	Type *mapTypeForInstance( Type *declared, StructDefinition *structDef,
		Type *instanceType );

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
	llvm::Function *getOrDeclareEventOn();
	llvm::Function *getOrDeclareAsyncCall();
	llvm::Function *getOrDeclareAwait();
	llvm::Function *getOrDeclareTaskDestroy();

	// Builtin to_json(value): compile-time dispatch to StructName_to_json
	llvm::Value *genToJsonCall( CallExpression *call );

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
	llvm::Function *getOrDeclareDbResultGetFloat();
	llvm::Function *getOrDeclareDbResultFree();
	llvm::Function *getOrDeclareDbDefault();
	llvm::Function *getOrDeclareDbGet();
	llvm::Function *getOrDeclareDbOpen();
	llvm::Function *getOrDeclareDbSetDefault();
	llvm::Function *getOrDeclareDbRegister();

	// Resolve the connection pointer for a query on table `tableName`:
	// __blang_db_get("name") if the table struct carries @db("name"),
	// otherwise __blang_db_default().
	llvm::Value *genDbConnForTable( const std::string &tableName );

	// Build an [N x i8*] array of C-string param values from the runtime
	// expressions backing SQL `?` placeholders; returns an i8** to element 0
	// (or a null i8** when there are no params). Sets outCount to N.
	llvm::Value *buildParamArray( const std::vector<const Expression*> &paramExprs,
		int &outCount );

	// Convert an evaluated value to a NUL-terminated C string (i8*) suitable
	// for binding as a SQL parameter.
	llvm::Value *paramToCString( llvm::Value *val );

	// Compile-time validation that query/update/delete field references and
	// insert field names exist on the target table struct.  Reports via
	// cerr + mHasError (consistent with the rest of codegen).
	void validateQueryFields( const std::string &tableName,
		const std::vector<QueryPipelineStep> &steps, Expression *node );
	void validateInsertFields( InsertExpression *insert );
	// Recursively collect .field references (QueryFieldExpression) from an expr.
	void collectQueryFieldRefs( const Expression *expr,
		std::vector<std::string> &out );

	// ForIn codegen
	void genForInStatement( ForInStatement *forInStmt );

	// Helper to get the alloca for an expression's address (for GEP)
	llvm::AllocaInst *getExpressionAddress( Expression *expr );

	// Type mapping
	llvm::Type *getLLVMType( Type *type );

	// Shared helper for declaring external functions (reduces getOrDeclare boilerplate)
	llvm::Function *declareExtern( const char *name, llvm::Type *retType,
		std::initializer_list<llvm::Type*> paramTypes, bool isVariadic = false );

	// LLVM state
	std::unique_ptr<llvm::LLVMContext> mContext;
	std::unique_ptr<llvm::Module> mModule;
	std::unique_ptr<llvm::IRBuilder<>> mBuilder;

	// Debug info (DWARF, U3). Off by default so a non-`-g` build is
	// byte-identical to pre-U3. Enabled via setDebugInfo(true) from qcc's -g arg.
	bool mDebugInfo = false;
	bool mDebugFinalized = false;   // guards finalizeDebugInfo() (idempotent)
	std::unique_ptr<llvm::DIBuilder> mDIBuilder;
	llvm::DICompileUnit *mDICompileUnit = nullptr;
	// Per-source-path DIFile cache (--combine gives each .b its own DIFile so
	// line tables point at the correct source).
	std::map<std::string, llvm::DIFile*> mDIFileCache;
	// The DISubprogram scope of the function currently being generated (null
	// outside a function / when debug info is off). DebugLocs resolve against it.
	llvm::DISubprogram *mCurrentDISubprogram = nullptr;

	// Debug-info helpers (all no-ops when !mDebugInfo). Defined in CGDebug.cpp.
	llvm::DIFile *getOrCreateDIFile( const std::string &path );
	llvm::DISubprogram *createDISubprogram( llvm::Function *llvmFunc,
		const SourceLocation &loc, const std::string &name );
	void applyDebugLoc( const SourceLocation &loc );
	void clearDebugLoc();
	void finalizeDebugInfo();

	// Raw text from the most recent failing verify(); surfaced only under
	// --debug-compiler (U2, FR-010).
	std::string mVerifyError;

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
	// Protocols, for generic-constraint checks on inferred calls (the
	// explicit-type-arg path is checked in Sema)
	std::map<std::string, ProtocolDefinition*> mProtocolDefMap;
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

	// Database configuration (from blang.toml [database], forwarded by bcc).
	std::string mDbDriver;   // "sqlite" / "postgres" (default sqlite)
	std::string mDbUrl;      // connection string; empty => runtime env fallback
	struct DbNamedConn { std::string name, driver, url; };
	std::vector<DbNamedConn> mDbNamedConns;

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
	// mConcreteType carries the subject's concrete instantiation (e.g.
	// Result<int,string>) when the enum is generic (built-in Option/Result), so
	// a generic-param payload (T/E) can be resolved to its concrete refcounted
	// type at release time; nullptr for non-generic enums whose payload types are
	// already concrete in the definition.
	struct EnumCleanupEntry
	{
		llvm::AllocaInst *alloca;
		EnumDefinition *enumDef;
		Type *concreteType;
	};
	std::vector<std::vector<EnumCleanupEntry>> mEnumScopeStack;

	// Emit cleanup code for an enum variable's refcounted payload. concreteEnumType
	// (optional) supplies type arguments for a generic enum so generic-param
	// payloads resolve to their concrete refcounted types.
	void emitEnumPayloadRelease( llvm::AllocaInst *alloca, EnumDefinition *enumDef,
		Type *concreteEnumType = nullptr );

	// Register a payload-carrying enum RVALUE passed directly as a call argument
	// (e.g. `pick(Option.some("hi"))`) for scope-exit payload release. The
	// callee borrows its parameters, so without this no one owns the temp
	// enum's refcounted payload and it leaks (ARC ledger #7). declParamType is
	// the callee's declared parameter type — the best source of the concrete
	// instantiation (Option<string>) since an EnumConstructExpression carries
	// no Sema-resolved type. No-op for variables/fields/index reads (their
	// declaration sites own the payload) and for payload-free enums.
	void trackEnumArgTemp( Expression *argExpr, llvm::Value *argVal,
		Type *declParamType );

	llvm::Function *getOrDeclareStringEqualsCstr();

	// --- Recursive enums (boxed enum payloads) ---
	// A variant payload whose type names a registered enum is stored as a
	// POINTER to a heap-allocated copy (a "box", __blang_rc_alloc_dtor'd), so
	// self-/mutually-recursive enums (enum Expr { add(Expr, Expr) ... }) have
	// finite layout. Non-generic enums only: a generic enum's payload slots
	// stay type-erased.
	//
	// Emit release calls for one enum value's refcounted payloads, reading
	// through enumPtr (a pointer to the enum struct). Shared by the scope-exit
	// release (emitEnumPayloadRelease) and the generated box destructor.
	void emitEnumPayloadReleaseFromPtr( llvm::Value *enumPtr,
		EnumDefinition *enumDef, Type *concreteEnumType );
	// __enum_<Name>_box_dtor(void*): releases the boxed value's refcounted
	// payloads when the box's refcount hits zero (recursion happens at runtime
	// through child boxes' own dtors). Returns nullptr when the enum has no
	// refcounted payloads (plain __blang_rc_alloc suffices).
	llvm::Function *getOrGenEnumBoxDtor( EnumDefinition *enumDef );
	// __enum_<Name>_payload_retain(void*): retains the refcounted payloads of
	// the enum value at the pointer — used when a box is filled by COPYING
	// from an existing owner (variable/field source), whose own release still
	// runs. Returns nullptr when the enum has no refcounted payloads.
	llvm::Function *getOrGenEnumPayloadRetain( EnumDefinition *enumDef );
	// True if any variant payload (resolved for concreteEnumType) is
	// refcounted: string/Array/Buffer/user struct/boxed enum.
	bool enumHasRefcountedPayload( EnumDefinition *enumDef, Type *concreteEnumType );

	// Resolve a variant's associated type to a concrete type: if it names one of
	// the enum's generic parameters, substitute the matching argument from
	// concreteEnumType; otherwise return it unchanged. Returns the input when no
	// substitution applies (never null for a non-null input).
	Type *resolveVariantPayloadType( Type *assocType, EnumDefinition *enumDef,
		Type *concreteEnumType );

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

	// Temporary struct tracking: struct literals created as expression temporaries
	// (e.g., function arguments) that need to be released after the enclosing statement.
	std::vector<llvm::Value*> mTempStructs;
	void trackTempStruct( llvm::Value *structPtr );
	void releaseTempStructs();
	void untrackTempStruct( llvm::Value *structPtr );

	// Temporary array tracking: arrays produced as expression rvalues (a call or
	// method that returns Array<T>) that are not bound to a variable and must be
	// released after the enclosing statement. Mirrors the struct-temp discipline:
	// tracked at the producing call/method, untracked when ownership transfers
	// (stored into a variable / struct field / enum payload / returned).
	std::vector<llvm::Value*> mTempArrays;
	void trackTempArray( llvm::Value *arrPtr );
	void releaseTempArrays();
	void untrackTempArray( llvm::Value *arrPtr );

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

	// Test-runner mode (qcc --emit-test-main); see setTestMode().
	bool mTestMode = false;

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
