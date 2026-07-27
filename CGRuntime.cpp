#include "CodeGen.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"

#include <iostream>

using namespace QLang;
using namespace std;

// ---- declareExtern helper ----

llvm::Function *CodeGen::declareExtern( const char *name, llvm::Type *retType,
	std::initializer_list<llvm::Type*> paramTypes, bool isVariadic )
{
	llvm::Function *f = mModule->getFunction( name );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get( retType,
		std::vector<llvm::Type*>( paramTypes ), isVariadic );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, name, mModule.get() );
}

// ---- C library declarations ----

llvm::Function *CodeGen::getOrDeclarePuts()
{
	return declareExtern( "puts", llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareExit()
{
	return declareExtern( "exit", llvm::Type::getVoidTy( *mContext ),
		{ llvm::Type::getInt32Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclarePrintf()
{
	return declareExtern( "printf", llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) }, true /* variadic */ );
}

llvm::Function *CodeGen::getOrDeclareMalloc()
{
	return declareExtern( "malloc", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareFree()
{
	return declareExtern( "free", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareSnprintf()
{
	return declareExtern( "snprintf", llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ) }, true /* variadic */ );
}

llvm::Function *CodeGen::getOrDeclareStrlen()
{
	return declareExtern( "strlen", llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

// ---- BLang runtime library declarations ----

llvm::Function *CodeGen::getOrDeclareRcAlloc()
{
	return declareExtern( "__blang_rc_alloc", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareRcAllocSync()
{
	return declareExtern( "__blang_rc_alloc_sync", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareRcRetain()
{
	return declareExtern( "__blang_rc_retain", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareRcRelease()
{
	return declareExtern( "__blang_rc_release", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareSyncLock()
{
	return declareExtern( "__blang_sync_lock", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareSyncUnlock()
{
	return declareExtern( "__blang_sync_unlock", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareSysInit()
{
	return declareExtern( "__blang_sys_init", llvm::Type::getVoidTy( *mContext ),
		{ llvm::Type::getInt32Ty( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareRuntimeInit()
{
	return declareExtern( "__blang_runtime_init", llvm::Type::getVoidTy( *mContext ),
		{ llvm::Type::getInt32Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareSpawn()
{
	return declareExtern( "__blang_spawn", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareSpawnWait()
{
	return declareExtern( "__blang_spawn_wait", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareSpawnTaskDestroy()
{
	return declareExtern( "__blang_spawn_task_destroy", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareWaitAll()
{
	return declareExtern( "__blang_wait_all", llvm::Type::getVoidTy( *mContext ), {} );
}

llvm::Function *CodeGen::getOrDeclareRuntimeShutdown()
{
	return declareExtern( "__blang_runtime_shutdown", llvm::Type::getVoidTy( *mContext ), {} );
}

llvm::Function *CodeGen::getOrDeclareChanCreate()
{
	return declareExtern( "__blang_chan_create", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareChanSend()
{
	return declareExtern( "__blang_chan_send", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareChanRecv()
{
	return declareExtern( "__blang_chan_recv", llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareChanClose()
{
	return declareExtern( "__blang_chan_close", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareChanDestroy()
{
	return declareExtern( "__blang_chan_destroy", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

// Register a handler on the global event loop, keyed by an event source fd
// (timerfd from timer.every()/after(), or a socket fd). The handler ABI is
// void(void* ctx, int fd). Used by `on EXPR { }` event handler codegen.
//   void __blang_event_on( int fd, void(*handler)(void*, int), void* ctx )
llvm::Function *CodeGen::getOrDeclareEventOn()
{
	return declareExtern( "__blang_event_on", llvm::Type::getVoidTy( *mContext ),
		{ llvm::Type::getInt32Ty( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareAsyncCall()
{
	return declareExtern( "__blang_async_call", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareAwait()
{
	return declareExtern( "__blang_await", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareTaskDestroy()
{
	return declareExtern( "__blang_task_destroy", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

// ---- Lambda context lifetime runtime declarations ----

llvm::Function *CodeGen::getOrDeclareLambdaCtxRetain()
{
	return declareExtern( "__blang_lambda_ctx_retain", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareLambdaCtxRelease()
{
	return declareExtern( "__blang_lambda_ctx_release", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

// ---- Print runtime declarations ----

llvm::Function *CodeGen::getOrDeclarePrintBlang()
{
	return declareExtern( "__blang_print", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclarePrintNewline()
{
	return declareExtern( "__blang_print_newline", llvm::Type::getVoidTy( *mContext ), {} );
}

llvm::Function *CodeGen::getOrDeclarePrintFlush()
{
	return declareExtern( "__blang_print_flush", llvm::Type::getVoidTy( *mContext ), {} );
}

llvm::Function *CodeGen::getOrDeclareIntToStringFmt()
{
	return declareExtern( "__blang_int_to_string_fmt", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt32Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareFloatToStringFmt()
{
	return declareExtern( "__blang_float_to_string_fmt", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getDoubleTy( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt32Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareCharToString()
{
	return declareExtern( "__blang_char_to_string", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt32Ty( *mContext ) } );
}

// ---- String runtime declarations ----

llvm::Function *CodeGen::getOrDeclareStringCreate()
{
	return declareExtern( "__blang_string_create", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareStringCreateStatic()
{
	return declareExtern( "__blang_string_create_static", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareStringRetain()
{
	return declareExtern( "__blang_string_retain", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareStringRelease()
{
	return declareExtern( "__blang_string_release", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareStringConcat()
{
	return declareExtern( "__blang_string_concat", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareStringConcatMany()
{
	return declareExtern( "__blang_string_concat_many", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareStringEquals()
{
	return declareExtern( "__blang_string_equals", llvm::Type::getInt1Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareStringEqualsCstr()
{
	// __blang_string_equals_cstr(s, lit, len) -> i1 — match string-pattern
	// dispatch: compares against a global literal without allocating.
	return declareExtern( "__blang_string_equals_cstr", llvm::Type::getInt1Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareStringCompare()
{
	// __blang_string_compare(a,b) -> i32 : <0, 0, >0 (lexicographic, like strcmp)
	return declareExtern( "__blang_string_compare", llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareStringLength()
{
	return declareExtern( "__blang_string_length", llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareStringCharAt()
{
	return declareExtern( "__blang_string_char_at", llvm::Type::getInt8Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareIntToString()
{
	return declareExtern( "__blang_int_to_string", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareFloatToString()
{
	return declareExtern( "__blang_float_to_string", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getDoubleTy( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareBoolToString()
{
	return declareExtern( "__blang_bool_to_string", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt1Ty( *mContext ) } );
}

// ---- Additional string runtime declarations ----

llvm::Function *CodeGen::getOrDeclareStringIsEmpty()
{
	return declareExtern( "__blang_string_is_empty", llvm::Type::getInt1Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareStringContains()
{
	return declareExtern( "__blang_string_contains", llvm::Type::getInt1Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareStringStartsWith()
{
	return declareExtern( "__blang_string_starts_with", llvm::Type::getInt1Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareStringEndsWith()
{
	return declareExtern( "__blang_string_ends_with", llvm::Type::getInt1Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareStringIndexOf()
{
	return declareExtern( "__blang_string_index_of", llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareStringToUpper()
{
	return declareExtern( "__blang_string_to_upper", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareStringToLower()
{
	return declareExtern( "__blang_string_to_lower", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareStringTrim()
{
	return declareExtern( "__blang_string_trim", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareStringSubstring()
{
	return declareExtern( "__blang_string_substring", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareStringReplace()
{
	return declareExtern( "__blang_string_replace", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareStringByteAt()
{
	return declareExtern( "__blang_string_byte_at", llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareStringToCstring()
{
	return declareExtern( "__blang_string_to_cstring", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

// ---- Array runtime declarations ----

llvm::Function *CodeGen::getOrDeclareArrayCreate()
{
	return declareExtern( "__blang_array_create", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt32Ty( *mContext ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareArrayCreateFromData()
{
	return declareExtern( "__blang_array_create_from_data", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt32Ty( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareArrayRetain()
{
	return declareExtern( "__blang_array_retain", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareArrayRelease()
{
	return declareExtern( "__blang_array_release", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareArrayGet()
{
	return declareExtern( "__blang_array_get", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareArraySet()
{
	return declareExtern( "__blang_array_set", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareArrayPush()
{
	return declareExtern( "__blang_array_push", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareArrayLength()
{
	return declareExtern( "__blang_array_length", llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareArrayConcat()
{
	return declareExtern( "__blang_array_concat", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

// ---- Additional array runtime declarations ----

llvm::Function *CodeGen::getOrDeclareArrayIsEmpty()
{
	return declareExtern( "__blang_array_is_empty", llvm::Type::getInt1Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareArrayPop()
{
	return declareExtern( "__blang_array_pop", llvm::Type::getInt1Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareArrayCapacity()
{
	return declareExtern( "__blang_array_capacity", llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareArrayClear()
{
	return declareExtern( "__blang_array_clear", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareArraySetElemDtor()
{
	return declareExtern( "__blang_array_set_elem_dtor", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

void CodeGen::emitArrayElemDtor( llvm::Value *arrayPtr, const std::string &elemTypeName )
{
	llvm::Function *dtorFn = nullptr;

	if ( elemTypeName == "string" )
		dtorFn = getOrDeclareStringRelease();
	else if ( elemTypeName == "Array" )
		dtorFn = getOrDeclareArrayRelease();
	else if ( elemTypeName == "Buffer" )
		dtorFn = getOrDeclareBufferRelease();
	else if ( isUserStructType( elemTypeName ) )
		dtorFn = getOrDeclareRcRelease();

	if ( dtorFn != nullptr )
	{
		mBuilder->CreateCall( getOrDeclareArraySetElemDtor(), { arrayPtr, dtorFn } );
	}
}

void CodeGen::emitArrayElemRetain( llvm::Value *elemVal, const std::string &elemTypeName )
{
	llvm::Function *retainFn = nullptr;

	if ( elemTypeName == "string" )
		retainFn = getOrDeclareStringRetain();
	else if ( elemTypeName == "Array" )
		retainFn = getOrDeclareArrayRetain();
	else if ( elemTypeName == "Buffer" )
		retainFn = getOrDeclareBufferRetain();
	else if ( isUserStructType( elemTypeName ) )
		retainFn = getOrDeclareRcRetain();

	if ( retainFn != nullptr && elemVal != nullptr )
	{
		mBuilder->CreateCall( retainFn, { elemVal } );
	}
}

// ---- Buffer runtime declarations ----

llvm::Function *CodeGen::getOrDeclareBufferCreate()
{
	return declareExtern( "__blang_buffer_create", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferCreateFromString()
{
	return declareExtern( "__blang_buffer_create_from_string", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferRetain()
{
	return declareExtern( "__blang_buffer_retain", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferRelease()
{
	return declareExtern( "__blang_buffer_release", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferLength()
{
	return declareExtern( "__blang_buffer_length", llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferCapacity()
{
	return declareExtern( "__blang_buffer_capacity", llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferIsEmpty()
{
	return declareExtern( "__blang_buffer_is_empty", llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferGet()
{
	return declareExtern( "__blang_buffer_get", llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferSet()
{
	return declareExtern( "__blang_buffer_set", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::Type::getInt32Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferAppendByte()
{
	return declareExtern( "__blang_buffer_append_byte", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt32Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferAppendBytes()
{
	return declareExtern( "__blang_buffer_append_bytes", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferAppendString()
{
	return declareExtern( "__blang_buffer_append_string", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferIndexOf()
{
	return declareExtern( "__blang_buffer_index_of", llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferSlice()
{
	return declareExtern( "__blang_buffer_slice", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferToString()
{
	return declareExtern( "__blang_buffer_to_string", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferToStringRange()
{
	return declareExtern( "__blang_buffer_to_string_range", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferClear()
{
	return declareExtern( "__blang_buffer_clear", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareBufferCompact()
{
	return declareExtern( "__blang_buffer_compact", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) } );
}

// ---- Database runtime declarations ----

llvm::Function *CodeGen::getOrDeclareDbQuery()
{
	return declareExtern( "__blang_db_query", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt32Ty( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareDbExec()
{
	return declareExtern( "__blang_db_exec", llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt32Ty( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareDbResultCount()
{
	return declareExtern( "__blang_db_result_count", llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareDbResultGet()
{
	return declareExtern( "__blang_db_result_get", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt32Ty( *mContext ),
		  llvm::Type::getInt32Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareDbResultGetInt()
{
	return declareExtern( "__blang_db_result_get_int", llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt32Ty( *mContext ),
		  llvm::Type::getInt32Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareDbResultFree()
{
	return declareExtern( "__blang_db_result_free", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareDbResultGetFloat()
{
	llvm::Function *f = mModule->getFunction( "__blang_db_result_get_float" );
	if ( f != nullptr )
		return f;

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );
	llvm::Type *dblTy = llvm::Type::getDoubleTy( *mContext );
	llvm::FunctionType *ft = llvm::FunctionType::get( dblTy, { ptrTy, i32Ty, i32Ty }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_db_result_get_float", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareDbDefault()
{
	llvm::Function *f = mModule->getFunction( "__blang_db_default" );
	if ( f != nullptr )
		return f;

	// BlangDBConn* __blang_db_default(void)
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get( ptrTy, {}, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_db_default", mModule.get() );
}

// Connection-config runtime declarations used by generate() to open and register
// the default/named database connections from blang.toml (forwarded by bcc).
llvm::Function *CodeGen::getOrDeclareDbOpen()
{
	llvm::Function *f = mModule->getFunction( "__blang_db_open" );
	if ( f != nullptr )
		return f;

	// BlangDBConn* __blang_db_open(int driver, const char* conn_str, const char** err)
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );
	llvm::FunctionType *ft = llvm::FunctionType::get(
		ptrTy, { i32Ty, ptrTy, ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_db_open", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareDbSetDefault()
{
	llvm::Function *f = mModule->getFunction( "__blang_db_set_default" );
	if ( f != nullptr )
		return f;

	// void __blang_db_set_default(BlangDBConn*)
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ), { ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_db_set_default", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareDbRegister()
{
	llvm::Function *f = mModule->getFunction( "__blang_db_register" );
	if ( f != nullptr )
		return f;

	// void __blang_db_register(const char* name, BlangDBConn*)
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ), { ptrTy, ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_db_register", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareDbGet()
{
	llvm::Function *f = mModule->getFunction( "__blang_db_get" );
	if ( f != nullptr )
		return f;

	// BlangDBConn* __blang_db_get(const char* name)
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get( ptrTy, { ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_db_get", mModule.get() );
}

// ---- Database query/insert/update/delete codegen ----

llvm::Value *CodeGen::genDbConnForTable( const std::string &tableName )
{
	// Route to a named connection if the table struct carries @db("name").
	auto it = mStructDefMap.find( tableName );
	if ( it != mStructDefMap.end() && it->second != nullptr )
	{
		for ( const auto &ann : it->second->getAnnotations() )
		{
			if ( ann.mName == "db" && !ann.mArgs.empty() )
			{
				llvm::Value *nameStr =
					mBuilder->CreateGlobalStringPtr( ann.mArgs[0], "db.name" );
				return mBuilder->CreateCall(
					getOrDeclareDbGet(), { nameStr }, "db.conn" );
			}
		}
	}
	return mBuilder->CreateCall( getOrDeclareDbDefault(), {}, "db.conn" );
}

llvm::Value *CodeGen::paramToCString( llvm::Value *val )
{
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i8Ty = llvm::Type::getInt8Ty( *mContext );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );
	llvm::Type *i64Ty = llvm::Type::getInt64Ty( *mContext );

	if ( val == nullptr )
		return llvm::ConstantPointerNull::get( llvm::PointerType::get( *mContext, 0 ) );

	llvm::Type *t = val->getType();

	// Integers (including bool) → decimal string via snprintf.
	if ( t->isIntegerTy() )
	{
		llvm::AllocaInst *buf = mBuilder->CreateAlloca(
			i8Ty, llvm::ConstantInt::get( i32Ty, 32 ), "param.ibuf" );
		llvm::Value *fmt = mBuilder->CreateGlobalStringPtr( "%lld", "param.ifmt" );
		llvm::Value *ext = val;
		if ( !t->isIntegerTy( 64 ) )
			ext = mBuilder->CreateSExt( val, i64Ty, "param.iext" );
		mBuilder->CreateCall( getOrDeclareSnprintf(),
			{ buf, llvm::ConstantInt::get( i64Ty, 32 ), fmt, ext } );
		return buf;
	}

	// Floats/doubles → "%g" via snprintf (varargs promote float to double).
	if ( t->isFloatingPointTy() )
	{
		llvm::AllocaInst *buf = mBuilder->CreateAlloca(
			i8Ty, llvm::ConstantInt::get( i32Ty, 64 ), "param.fbuf" );
		llvm::Value *fmt = mBuilder->CreateGlobalStringPtr( "%g", "param.ffmt" );
		llvm::Value *dval = val;
		if ( t->isFloatTy() )
			dval = mBuilder->CreateFPExt(
				val, llvm::Type::getDoubleTy( *mContext ), "param.fext" );
		mBuilder->CreateCall( getOrDeclareSnprintf(),
			{ buf, llvm::ConstantInt::get( i64Ty, 64 ), fmt, dval } );
		return buf;
	}

	// Pointers are BlangString* — bind the underlying C string. Extract the
	// null-terminated `.data` field directly (BlangString stores a length+1
	// buffer with a trailing '\0', see __blang_string_create) rather than
	// __blang_string_to_cstring, which mallocs a copy the caller would have to
	// free (it never did — a per-string-param leak). The db driver copies the
	// value during bind, and the source BlangString outlives the db call, so
	// borrowing `.data` is safe and leak-free.
	if ( t->isPointerTy() )
	{
		// BlangString: { char* data, i64 length, i64 capacity, i32 ref_count }
		llvm::StructType *bsType = llvm::StructType::get( *mContext,
			{ llvm::PointerType::get( *mContext, 0 ),
			  i64Ty, i64Ty, i32Ty } );
		llvm::Value *dataPtr = mBuilder->CreateStructGEP(
			bsType, val, 0, "param.str.data.ptr" );
		return mBuilder->CreateLoad( ptrTy, dataPtr, "param.str.data" );
	}

	// Unknown type → bind SQL NULL.
	return llvm::ConstantPointerNull::get( llvm::PointerType::get( *mContext, 0 ) );
}

llvm::Value *CodeGen::buildParamArray(
	const std::vector<const Expression*> &paramExprs, int &outCount )
{
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );

	outCount = static_cast<int>( paramExprs.size() );
	if ( outCount == 0 )
		return llvm::ConstantPointerNull::get( llvm::PointerType::get( *mContext, 0 ) );

	llvm::ArrayType *arrTy = llvm::ArrayType::get( ptrTy, outCount );
	llvm::AllocaInst *arr = mBuilder->CreateAlloca( arrTy, nullptr, "db.params" );
	llvm::Value *idx0 = llvm::ConstantInt::get( i32Ty, 0 );

	for ( int i = 0; i < outCount; i++ )
	{
		// genExpression borrows the value (read-only); no ownership transfer.
		llvm::Value *val = genExpression( const_cast<Expression*>( paramExprs[i] ) );
		llvm::Value *cstr = paramToCString( val );
		llvm::Value *elem = mBuilder->CreateGEP( arrTy, arr,
			{ idx0, llvm::ConstantInt::get( i32Ty, i ) }, "db.param.ptr" );
		mBuilder->CreateStore( cstr, elem );
	}

	return mBuilder->CreateGEP( arrTy, arr, { idx0, idx0 }, "db.params.ptr" );
}

void CodeGen::collectQueryFieldRefs( const Expression *expr,
	std::vector<std::string> &out )
{
	if ( expr == nullptr )
		return;

	const QueryFieldExpression *field =
		dynamic_cast<const QueryFieldExpression*>( expr );
	if ( field )
	{
		out.push_back( field->getFieldName() );
		return;
	}

	const OperationsExpression *ops =
		dynamic_cast<const OperationsExpression*>( expr );
	if ( ops )
	{
		collectQueryFieldRefs( ops->mOp1, out );
		collectQueryFieldRefs( ops->mOp2, out );
	}
}

void CodeGen::validateQueryFields( const std::string &tableName,
	const std::vector<QueryPipelineStep> &steps, Expression *node )
{
	auto it = mStructDefMap.find( tableName );
	if ( it == mStructDefMap.end() || it->second == nullptr )
	{
		cerr << "CodeGen: query on unknown table '" << tableName << "'" << endl;
		mHasError = true;
		return;
	}

	StructDefinition *structDef = it->second;
	if ( !structDef->isTable() )
	{
		cerr << "CodeGen: '" << tableName
			 << "' is not a table struct (use `table struct`)" << endl;
		mHasError = true;
		return;
	}

	// Build the set of valid column names.  JOIN steps reference a second
	// table; to avoid false positives, validation is skipped once a join is
	// present (the primary-table-only check would reject valid join columns).
	bool hasJoin = false;
	for ( const auto &step : steps )
		if ( step.mType == QueryPipelineStep::JOIN )
			hasJoin = true;
	if ( hasJoin )
		return;

	std::set<std::string> fields;
	for ( const auto &f : structDef->getFields() )
		fields.insert( f->getName() );

	auto checkField = [&]( const std::string &name )
	{
		if ( fields.find( name ) == fields.end() )
		{
			cerr << "CodeGen: field '" << name << "' not found in table '"
				 << tableName << "'" << endl;
			mHasError = true;
		}
	};

	for ( const auto &step : steps )
	{
		if ( step.mType == QueryPipelineStep::SET )
		{
			for ( const auto &sf : step.mSetFields )
				checkField( sf.first );
		}
		else
		{
			std::vector<std::string> refs;
			collectQueryFieldRefs( step.mExpression, refs );
			for ( const auto &r : refs )
				checkField( r );
		}
	}
}

void CodeGen::validateInsertFields( InsertExpression *insert )
{
	auto it = mStructDefMap.find( insert->mTableName );
	if ( it == mStructDefMap.end() || it->second == nullptr )
	{
		cerr << "CodeGen: insert into unknown table '"
			 << insert->mTableName << "'" << endl;
		mHasError = true;
		return;
	}

	StructDefinition *structDef = it->second;
	if ( !structDef->isTable() )
	{
		cerr << "CodeGen: '" << insert->mTableName
			 << "' is not a table struct (use `table struct`)" << endl;
		mHasError = true;
		return;
	}

	std::set<std::string> fields;
	for ( const auto &f : structDef->getFields() )
		fields.insert( f->getName() );

	for ( const auto &name : insert->mFieldNames )
	{
		if ( fields.find( name ) == fields.end() )
		{
			cerr << "CodeGen: field '" << name << "' not found in table '"
				 << insert->mTableName << "'" << endl;
			mHasError = true;
		}
	}
}

llvm::Value *CodeGen::genQueryExpression( QueryExpression *query )
{
	validateQueryFields( query->mTableName, query->mSteps, query );

	// Generate SQL + the runtime expressions backing its ? placeholders.
	SQLStatement sqlStmt = SQLGen::generateSelect( query, mQLangModule );

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );

	llvm::Value *sqlStr = mBuilder->CreateGlobalStringPtr( sqlStmt.sql, "query.sql" );
	llvm::Value *conn = genDbConnForTable( query->mTableName );

	int numParams = 0;
	llvm::Value *params = buildParamArray( sqlStmt.paramExprs, numParams );

	llvm::Value *nullPtr = llvm::ConstantPointerNull::get( llvm::PointerType::get( *mContext, 0 ) );
	llvm::AllocaInst *errMsgAlloca = mBuilder->CreateAlloca( ptrTy, nullptr, "query.err" );
	mBuilder->CreateStore( nullPtr, errMsgAlloca );

	// __blang_db_query(conn, sql, params, num_params, &error_msg) -> BlangDBResult*
	llvm::Value *result = mBuilder->CreateCall(
		getOrDeclareDbQuery(),
		{ conn, sqlStr, params,
		  llvm::ConstantInt::get( i32Ty, numParams ), errMsgAlloca },
		"query.result" );

	// Map result rows into an Array<T> of the table struct — or, when the
	// pipeline ends in |> first, into an Option<T> (some(row 0) / none).
	// SELECT * returns columns in the table's (== struct's) field order, so
	// column i feeds field i. Each row becomes a heap struct.
	llvm::Type *i64Ty = llvm::Type::getInt64Ty( *mContext );

	StructDefinition *structDef = nullptr;
	{
		auto it = mStructDefMap.find( query->mTableName );
		if ( it != mStructDefMap.end() )
			structDef = it->second;
	}

	bool hasFirst = false;
	for ( const auto &step : query->mSteps )
	{
		if ( step.mType == QueryPipelineStep::FIRST )
		{
			hasFirst = true;
			break;
		}
	}

	if ( structDef == nullptr )
	{
		// Unknown table — free the result and return an empty array.
		llvm::Value *arr = mBuilder->CreateCall(
			getOrDeclareArrayCreate(),
			{ llvm::ConstantInt::get( i32Ty, 8 ),
			  llvm::ConstantInt::get( i64Ty, 8 ) }, "rows" );
		mBuilder->CreateCall( getOrDeclareDbResultFree(), { result } );
		trackTempArray( arr );
		return arr;
	}

	llvm::StructType *structType = getOrCreateStructType( structDef );
	llvm::DataLayout dl( mModule.get() );

	// Build one heap struct from result row `rowIdx` (with destructor for
	// refcounted field cleanup). Shared by the Array<T> loop and the |> first
	// Option<T> path.
	auto buildRowStruct = [&]( llvm::Value *rowIdx ) -> llvm::Value *
	{
		llvm::Value *structSize = llvm::ConstantInt::get(
			i64Ty, dl.getTypeAllocSize( structType ) );
		std::map<std::string, std::string> noSub;
		llvm::Function *structDtor = getOrGenStructDestructor( structDef, noSub );
		llvm::Value *rowStruct;
		if ( structDtor != nullptr )
			rowStruct = mBuilder->CreateCall( getOrDeclareRcAllocDtor(),
				{ structSize, structDtor }, "row.struct" );
		else
			rowStruct = mBuilder->CreateCall( getOrDeclareRcAlloc(),
				{ structSize }, "row.struct" );

		const auto &fields = structDef->getFields();
		for ( size_t f = 0; f < fields.size(); f++ )
		{
			llvm::Value *colIdx = llvm::ConstantInt::get( i32Ty, (int)f );
			llvm::Type *fieldLLVM = structType->getElementType( (unsigned)f );
			llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
				structType, rowStruct, (unsigned)f, "row.field" );

			std::string fieldTypeName = fields[f]->getVariableType()->getName();
			llvm::Value *stored = nullptr;

			if ( fieldTypeName == "string" )
			{
				llvm::Value *cstr = mBuilder->CreateCall(
					getOrDeclareDbResultGet(), { result, rowIdx, colIdx }, "col.str" );
				llvm::Value *len = mBuilder->CreateCall(
					getOrDeclareStrlen(), { cstr }, "col.len" );
				stored = mBuilder->CreateCall(
					getOrDeclareStringCreate(), { cstr, len }, "col.blangstr" );
			}
			else if ( fieldLLVM->isFloatingPointTy() )
			{
				llvm::Value *d = mBuilder->CreateCall(
					getOrDeclareDbResultGetFloat(), { result, rowIdx, colIdx }, "col.f" );
				if ( fieldLLVM->isFloatTy() )
					d = mBuilder->CreateFPTrunc( d, fieldLLVM, "col.f.trunc" );
				stored = d;
			}
			else if ( fieldLLVM->isIntegerTy() )
			{
				llvm::Value *v = mBuilder->CreateCall(
					getOrDeclareDbResultGetInt(), { result, rowIdx, colIdx }, "col.i" );
				stored = mBuilder->CreateIntCast( v, fieldLLVM, true, "col.i.cast" );
			}
			else
			{
				// Unreachable in practice: Sema rejects table struct fields with
				// no SQL column mapping. Kept as a safe default.
				stored = llvm::Constant::getNullValue( fieldLLVM );
			}

			mBuilder->CreateStore( stored, fieldPtr );
		}

		return rowStruct;
	};

	llvm::Value *count = mBuilder->CreateCall(
		getOrDeclareDbResultCount(), { result }, "row.count" );
	llvm::Function *fn = mBuilder->GetInsertBlock()->getParent();

	if ( hasFirst )
	{
		// |> first: Option<T> = some(row 0) when the (LIMIT 1) result has a
		// row, else none. The fresh row struct's refcount-1 reference transfers
		// into the Option payload; the consumer's enum tracking (scope release,
		// match-temp registration) releases it with the concrete T recovered
		// from the expression's resolved type Option<T>.
		EnumDefinition *optDef = nullptr;
		{
			auto oit = mEnumDefMap.find( "Option" );
			if ( oit != mEnumDefMap.end() )
				optDef = oit->second;
		}
		if ( optDef == nullptr )
		{
			cerr << "CodeGen: built-in Option is unavailable for `query |> first`" << endl;
			mHasError = true;
			mBuilder->CreateCall( getOrDeclareDbResultFree(), { result } );
			return nullptr;
		}

		int someIdx = -1, noneIdx = -1;
		for ( size_t v = 0; v < optDef->mVariants.size(); v++ )
		{
			if ( optDef->mVariants[v].mName == "some" )
				someIdx = (int)v;
			else if ( optDef->mVariants[v].mName == "none" )
				noneIdx = (int)v;
		}

		llvm::StructType *optType = getOrCreateEnumType( optDef );
		llvm::AllocaInst *optAlloca = mBuilder->CreateAlloca( optType, nullptr, "first.opt" );
		llvm::Value *tagPtr = mBuilder->CreateStructGEP( optType, optAlloca, 0, "first.tag" );
		llvm::Value *payloadPtr = mBuilder->CreateStructGEP( optType, optAlloca, 1, "first.payload" );
		llvm::Type *payloadArrTy = optType->getElementType( 1 );
		llvm::Value *payloadByte = mBuilder->CreateGEP( payloadArrTy, payloadPtr,
			{ llvm::ConstantInt::get( i64Ty, 0 ), llvm::ConstantInt::get( i64Ty, 0 ) },
			"first.payload.byte" );

		llvm::BasicBlock *someBB = llvm::BasicBlock::Create( *mContext, "first.some", fn );
		llvm::BasicBlock *noneBB = llvm::BasicBlock::Create( *mContext, "first.none", fn );
		llvm::BasicBlock *doneBB = llvm::BasicBlock::Create( *mContext, "first.done", fn );

		llvm::Value *haveRow = mBuilder->CreateICmpSGT(
			count, llvm::ConstantInt::get( i32Ty, 0 ), "first.have" );
		mBuilder->CreateCondBr( haveRow, someBB, noneBB );

		mBuilder->SetInsertPoint( someBB );
		llvm::Value *rowStruct = buildRowStruct( llvm::ConstantInt::get( i32Ty, 0 ) );
		mBuilder->CreateStore( llvm::ConstantInt::get( i32Ty, someIdx ), tagPtr );
		mBuilder->CreateStore( rowStruct, payloadByte );
		mBuilder->CreateBr( doneBB );

		mBuilder->SetInsertPoint( noneBB );
		mBuilder->CreateStore( llvm::ConstantInt::get( i32Ty, noneIdx ), tagPtr );
		mBuilder->CreateStore(
			llvm::ConstantPointerNull::get( llvm::PointerType::get( *mContext, 0 ) ),
			payloadByte );
		mBuilder->CreateBr( doneBB );

		mBuilder->SetInsertPoint( doneBB );
		mBuilder->CreateCall( getOrDeclareDbResultFree(), { result } );
		return mBuilder->CreateLoad( optType, optAlloca, "first.val" );
	}

	// arr = __blang_array_create(elem_size = sizeof(ptr), capacity)
	llvm::Value *arr = mBuilder->CreateCall(
		getOrDeclareArrayCreate(),
		{ llvm::ConstantInt::get( i32Ty, 8 ),
		  llvm::ConstantInt::get( i64Ty, 8 ) }, "rows" );

	// Release each row struct when the array is freed.
	emitArrayElemDtor( arr, structDef->getName() );

	// for ( i = 0; i < count; i++ ) { build struct from row i; push }
	llvm::BasicBlock *condBB = llvm::BasicBlock::Create( *mContext, "qrow.cond", fn );
	llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create( *mContext, "qrow.body", fn );
	llvm::BasicBlock *endBB  = llvm::BasicBlock::Create( *mContext, "qrow.end", fn );

	llvm::AllocaInst *iAlloca = mBuilder->CreateAlloca( i32Ty, nullptr, "qrow.i" );
	mBuilder->CreateStore( llvm::ConstantInt::get( i32Ty, 0 ), iAlloca );
	mBuilder->CreateBr( condBB );

	mBuilder->SetInsertPoint( condBB );
	llvm::Value *iVal = mBuilder->CreateLoad( i32Ty, iAlloca, "i" );
	llvm::Value *cmp = mBuilder->CreateICmpSLT( iVal, count, "i.lt.count" );
	mBuilder->CreateCondBr( cmp, bodyBB, endBB );

	mBuilder->SetInsertPoint( bodyBB );
	llvm::Value *rowIdx = mBuilder->CreateLoad( i32Ty, iAlloca, "row" );
	llvm::Value *rowStruct = buildRowStruct( rowIdx );

	// arr.push(rowStruct) — push copies the pointer; the array now owns the row.
	llvm::AllocaInst *elemSlot = mBuilder->CreateAlloca( ptrTy, nullptr, "row.slot" );
	mBuilder->CreateStore( rowStruct, elemSlot );
	mBuilder->CreateCall( getOrDeclareArrayPush(), { arr, elemSlot } );

	llvm::Value *iNext = mBuilder->CreateAdd(
		rowIdx, llvm::ConstantInt::get( i32Ty, 1 ), "i.next" );
	mBuilder->CreateStore( iNext, iAlloca );
	mBuilder->CreateBr( condBB );

	mBuilder->SetInsertPoint( endBB );
	mBuilder->CreateCall( getOrDeclareDbResultFree(), { result } );
	// Fresh Array<T> rvalue: track as a statement temporary so a discarded
	// `query T |> ...;` result (never bound to a variable) is released at
	// statement end. Consumers that keep the array (var-decl init, assignment,
	// return) call untrackTempArray to take ownership.
	trackTempArray( arr );
	return arr;
}

llvm::Value *CodeGen::genInsertExpression( InsertExpression *insert )
{
	validateInsertFields( insert );

	SQLStatement sqlStmt = SQLGen::generateInsert( insert, mQLangModule );

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );

	llvm::Value *sqlStr = mBuilder->CreateGlobalStringPtr( sqlStmt.sql, "insert.sql" );
	llvm::Value *conn = genDbConnForTable( insert->mTableName );

	int numParams = 0;
	llvm::Value *params = buildParamArray( sqlStmt.paramExprs, numParams );

	llvm::Value *nullPtr = llvm::ConstantPointerNull::get( llvm::PointerType::get( *mContext, 0 ) );
	llvm::AllocaInst *errMsgAlloca = mBuilder->CreateAlloca( ptrTy, nullptr, "insert.err" );
	mBuilder->CreateStore( nullPtr, errMsgAlloca );

	// __blang_db_exec(conn, sql, params, num_params, &error_msg)
	return mBuilder->CreateCall(
		getOrDeclareDbExec(),
		{ conn, sqlStr, params,
		  llvm::ConstantInt::get( i32Ty, numParams ), errMsgAlloca },
		"insert.result" );
}

llvm::Value *CodeGen::genUpdateExpression( UpdateExpression *update )
{
	validateQueryFields( update->mTableName, update->mSteps, update );

	SQLStatement sqlStmt = SQLGen::generateUpdate( update, mQLangModule );

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );

	llvm::Value *sqlStr = mBuilder->CreateGlobalStringPtr( sqlStmt.sql, "update.sql" );
	llvm::Value *conn = genDbConnForTable( update->mTableName );

	int numParams = 0;
	llvm::Value *params = buildParamArray( sqlStmt.paramExprs, numParams );

	llvm::Value *nullPtr = llvm::ConstantPointerNull::get( llvm::PointerType::get( *mContext, 0 ) );
	llvm::AllocaInst *errMsgAlloca = mBuilder->CreateAlloca( ptrTy, nullptr, "update.err" );
	mBuilder->CreateStore( nullPtr, errMsgAlloca );

	return mBuilder->CreateCall(
		getOrDeclareDbExec(),
		{ conn, sqlStr, params,
		  llvm::ConstantInt::get( i32Ty, numParams ), errMsgAlloca },
		"update.result" );
}

llvm::Value *CodeGen::genDeleteExpression( DeleteExpression *del )
{
	validateQueryFields( del->mTableName, del->mSteps, del );

	SQLStatement sqlStmt = SQLGen::generateDelete( del, mQLangModule );

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );

	llvm::Value *sqlStr = mBuilder->CreateGlobalStringPtr( sqlStmt.sql, "delete.sql" );
	llvm::Value *conn = genDbConnForTable( del->mTableName );

	int numParams = 0;
	llvm::Value *params = buildParamArray( sqlStmt.paramExprs, numParams );

	llvm::Value *nullPtr = llvm::ConstantPointerNull::get( llvm::PointerType::get( *mContext, 0 ) );
	llvm::AllocaInst *errMsgAlloca = mBuilder->CreateAlloca( ptrTy, nullptr, "delete.err" );
	mBuilder->CreateStore( nullPtr, errMsgAlloca );

	return mBuilder->CreateCall(
		getOrDeclareDbExec(),
		{ conn, sqlStr, params,
		  llvm::ConstantInt::get( i32Ty, numParams ), errMsgAlloca },
		"delete.result" );
}

// ---- Phase 2: Test block codegen ----

llvm::Function *CodeGen::genTestBlock( TestBlock *testBlock )
{
	// Sanitize the test name for use as a function name
	string testName = "__blang_test_";
	for ( char c : testBlock->getName() )
	{
		if ( isalnum( c ) )
			testName += c;
		else
			testName += '_';
	}

	// Create the test function: void __blang_test_xxx()
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ), {}, false );
	llvm::Function *testFunc = llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, testName, mModule.get() );

	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create( *mContext, "entry", testFunc );
	mBuilder->SetInsertPoint( entryBB );

	// Generate the test body
	if ( testBlock->mBody != nullptr )
		genBlock( testBlock->mBody );

	// Add implicit return
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateRetVoid();

	mVariableMap.clear();
	mMovedVariables.clear();
	return testFunc;
}

void CodeGen::genTestRunner( const std::vector<llvm::Function*> &testFunctions,
	const std::vector<SmartPtr<TestBlock>> &testBlocks )
{
	// Create void __blang_run_tests() that calls each test function
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ), {}, false );
	llvm::Function *runTests = llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_run_tests", mModule.get() );

	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create( *mContext, "entry", runTests );
	mBuilder->SetInsertPoint( entryBB );

	llvm::Function *putsFunc = getOrDeclarePuts();

	for ( size_t i = 0; i < testFunctions.size(); i++ )
	{
		// Print test name
		std::string msg = "Running test: " + testBlocks[i]->getName();
		llvm::Value *msgVal = mBuilder->CreateGlobalStringPtr( msg, "testmsg" );
		mBuilder->CreateCall( putsFunc, { msgVal } );

		// Call the test function
		mBuilder->CreateCall( testFunctions[i], {} );

		// Print pass (if assert failed inside, exit() already terminated)
		std::string passMsg = "  PASSED: " + testBlocks[i]->getName();
		llvm::Value *passMsgVal = mBuilder->CreateGlobalStringPtr( passMsg, "passmsg" );
		mBuilder->CreateCall( putsFunc, { passMsgVal } );
	}

	mBuilder->CreateRetVoid();
}

// Test-runner mode (qcc --emit-test-main): emit a real entry point that
// registers each test{} block with the C test driver and dispatches to it.
//
//   int main(int argc, char **argv) {
//       __blang_test_register("name0", __blang_test_name0);
//       ...
//       return __blang_test_main(argc, argv);
//   }
//
// The driver forks per test for isolation, counts pass/fail, honors
// --filter, and returns non-zero iff any test failed.
void CodeGen::genTestMain( const std::vector<llvm::Function*> &testFunctions,
	const std::vector<SmartPtr<TestBlock>> &testBlocks )
{
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );

	// void __blang_test_register(i8* name, void()* fn)
	llvm::FunctionType *regTy = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ), { ptrTy, ptrTy }, false );
	llvm::FunctionCallee regFn = mModule->getOrInsertFunction(
		"__blang_test_register", regTy );

	// i32 __blang_test_main(i32 argc, i8** argv)
	llvm::FunctionType *tmainTy = llvm::FunctionType::get(
		i32Ty, { i32Ty, ptrTy }, false );
	llvm::FunctionCallee tmainFn = mModule->getOrInsertFunction(
		"__blang_test_main", tmainTy );

	// int main(int argc, char **argv)
	llvm::FunctionType *mainTy = llvm::FunctionType::get(
		i32Ty, { i32Ty, ptrTy }, false );
	llvm::Function *mainFunc = llvm::Function::Create(
		mainTy, llvm::Function::ExternalLinkage, "main", mModule.get() );
	mainFunc->getArg( 0 )->setName( "argc" );
	mainFunc->getArg( 1 )->setName( "argv" );

	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create( *mContext, "entry", mainFunc );
	mBuilder->SetInsertPoint( entryBB );

	// Register each test by name with its generated function.
	for ( size_t i = 0; i < testFunctions.size(); i++ )
	{
		llvm::Value *nameVal = mBuilder->CreateGlobalStringPtr(
			testBlocks[i]->getName(), "testname" );
		mBuilder->CreateCall( regFn, { nameVal, testFunctions[i] } );
	}

	// return __blang_test_main(argc, argv);
	llvm::Value *rc = mBuilder->CreateCall(
		tmainFn, { mainFunc->getArg( 0 ), mainFunc->getArg( 1 ) }, "test.rc" );
	mBuilder->CreateRet( rc );
}

// ---- JSON codegen (@json annotation) ----

bool CodeGen::genJsonToJson( StructDefinition *structDef )
{
	llvm::StructType *structType = getOrCreateStructType( structDef );
	std::string funcName = structDef->getName() + "_to_json";

	// StructName_to_json(ptr self) -> char*
	// self is a heap pointer to struct data (structs are by-reference)
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get( ptrTy, { ptrTy }, false );
	llvm::Function *func = llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, funcName, mModule.get() );
	func->getArg( 0 )->setName( "self" );

	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create( *mContext, "entry", func );
	mBuilder->SetInsertPoint( entryBB );

	// self is already a pointer to struct data — use it directly for GEP
	llvm::Value *selfAlloca = func->getArg( 0 );

	// Create JSON object
	llvm::Value *jsonObj = mBuilder->CreateCall( getOrDeclareJsonObject(), {}, "json.obj" );

	// For each field: extract, wrap in JSON value, set on object
	for ( unsigned i = 0; i < structDef->mFields.size(); i++ )
	{
		VariableDefinition *field = structDef->mFields[i];
		std::string fieldName = field->getName();
		Type *fieldType = field->getVariableType();
		std::string typeName = fieldType ? fieldType->getName() : "int";

		// GEP to field
		llvm::Value *fieldPtr = mBuilder->CreateStructGEP( structType, selfAlloca, i, fieldName + ".ptr" );
		llvm::Value *fieldVal = mBuilder->CreateLoad( getLLVMType( fieldType ), fieldPtr, fieldName + ".val" );

		// Create JSON value based on type
		llvm::Value *jsonVal = nullptr;
		if ( typeName == "int" || typeName == "short" || typeName == "long" )
		{
			// Extend to i64 for the JSON runtime
			llvm::Value *i64Val = fieldVal;
			if ( !fieldVal->getType()->isIntegerTy( 64 ) )
				i64Val = mBuilder->CreateSExt( fieldVal, llvm::Type::getInt64Ty( *mContext ), fieldName + ".ext" );
			jsonVal = mBuilder->CreateCall( getOrDeclareJsonInt(), { i64Val }, fieldName + ".json" );
		}
		else if ( typeName == "char" )
		{
			// Extend i8 to i64 and use JSON int
			llvm::Value *i64Val = mBuilder->CreateSExt( fieldVal, llvm::Type::getInt64Ty( *mContext ), fieldName + ".ext" );
			jsonVal = mBuilder->CreateCall( getOrDeclareJsonInt(), { i64Val }, fieldName + ".json" );
		}
		else if ( typeName == "float" || typeName == "double" )
		{
			llvm::Value *dblVal = fieldVal;
			if ( fieldVal->getType()->isFloatTy() )
				dblVal = mBuilder->CreateFPExt( fieldVal, llvm::Type::getDoubleTy( *mContext ), fieldName + ".fpext" );
			jsonVal = mBuilder->CreateCall( getOrDeclareJsonFloat(), { dblVal }, fieldName + ".json" );
		}
		else if ( typeName == "string" )
		{
			// Extract raw char* from BlangString for JSON runtime
			llvm::StructType *bsType = llvm::StructType::get( *mContext,
				{ ptrTy,
				  llvm::Type::getInt64Ty( *mContext ),
				  llvm::Type::getInt64Ty( *mContext ),
				  llvm::Type::getInt32Ty( *mContext ) } );
			llvm::Value *dataGep = mBuilder->CreateStructGEP(
				bsType, fieldVal, 0, fieldName + ".data.ptr" );
			llvm::Value *rawStr = mBuilder->CreateLoad( ptrTy, dataGep, fieldName + ".data" );
			jsonVal = mBuilder->CreateCall( getOrDeclareJsonString(), { rawStr }, fieldName + ".json" );
		}
		else if ( typeName == "bool" )
		{
			// Extend i1 to i32
			llvm::Value *i32Val = mBuilder->CreateZExt( fieldVal, llvm::Type::getInt32Ty( *mContext ), fieldName + ".zext" );
			jsonVal = mBuilder->CreateCall( getOrDeclareJsonBool(), { i32Val }, fieldName + ".json" );
		}
		else
		{
			// Check if this is a nested @json struct
			auto it = mStructDefMap.find( typeName );
			if ( it != mStructDefMap.end() )
			{
				StructDefinition *nestedDef = it->second;
				bool hasJson = false;
				for ( const auto &ann : nestedDef->getAnnotations() )
				{
					if ( ann.mName == "json" )
					{
						hasJson = true;
						break;
					}
				}
				if ( hasJson )
				{
					// Call NestedStruct_to_json(ptr) -> char*
					std::string nestedToJson = typeName + "_to_json";
					llvm::Function *nestedFn = mModule->getFunction( nestedToJson );
					if ( !nestedFn )
					{
						// Forward declare if not yet generated
						llvm::FunctionType *nestedFt = llvm::FunctionType::get( ptrTy, { ptrTy }, false );
						nestedFn = llvm::Function::Create(
							nestedFt, llvm::Function::ExternalLinkage, nestedToJson, mModule.get() );
					}
					llvm::Value *nestedStr = mBuilder->CreateCall( nestedFn, { fieldVal }, fieldName + ".str" );

					// Extract raw char* from BlangString for json_decode
					llvm::StructType *bsType2 = llvm::StructType::get( *mContext,
						{ ptrTy,
						  llvm::Type::getInt64Ty( *mContext ),
						  llvm::Type::getInt64Ty( *mContext ),
						  llvm::Type::getInt32Ty( *mContext ) } );
					llvm::Value *nestedDataPtr = mBuilder->CreateStructGEP(
						bsType2, nestedStr, 0, fieldName + ".data.ptr" );
					llvm::Value *nestedRawStr = mBuilder->CreateLoad(
						ptrTy, nestedDataPtr, fieldName + ".data" );

					// Decode the JSON string back to a value tree
					llvm::Value *nullErrPtr = llvm::ConstantPointerNull::get(
						llvm::PointerType::get( *mContext, 0 ) );
					jsonVal = mBuilder->CreateCall(
						getOrDeclareJsonDecode(), { nestedRawStr, nullErrPtr }, fieldName + ".json" );

					// Release the intermediate BlangString
					mBuilder->CreateCall( getOrDeclareStringRelease(), { nestedStr } );
				}
				else
				{
					cerr << "Error: @json struct '" << structDef->getName()
						<< "' has field '" << fieldName << "' of struct type '" << typeName
						<< "' which does not have @json annotation" << endl;
					return false;
				}
			}
			else
			{
				cerr << "Error: @json struct '" << structDef->getName()
					<< "' has field '" << fieldName << "' of unsupported type '" << typeName << "'" << endl;
				return false;
			}
		}

		llvm::Value *keyStr = mBuilder->CreateGlobalStringPtr( fieldName, fieldName + ".key" );
		mBuilder->CreateCall( getOrDeclareJsonObjectSet(), { jsonObj, keyStr, jsonVal } );
	}

	// Encode to string (returns raw char*)
	llvm::Value *rawStr = mBuilder->CreateCall( getOrDeclareJsonEncode(), { jsonObj }, "json.str" );

	// Free the JSON tree
	mBuilder->CreateCall( getOrDeclareJsonFree(), { jsonObj } );

	// Wrap raw char* in BlangString: strlen then __blang_string_create
	llvm::Value *len = mBuilder->CreateCall( getOrDeclareStrlen(), { rawStr }, "json.len" );
	llvm::Value *result = mBuilder->CreateCall(
		getOrDeclareStringCreate(), { rawStr, len }, "json.blangstr" );

	// Free the malloc'd char* from __blang_json_encode (string_create copied it)
	mBuilder->CreateCall( getOrDeclareFree(), { rawStr } );

	mBuilder->CreateRet( result );
	return true;
}

// Builtin to_json(value): resolve the argument's struct type at compile time and
// dispatch to the generated StructName_to_json (see genJsonToJson). The Sema pass
// already rejected a non-@json argument with a located error (all build modes),
// so this codegen only asserts the serializer exists as a backstop. Returns a
// tracked temporary string. (Ported from origin's monolith into the CG* layer.)
llvm::Value *CodeGen::genToJsonCall( CallExpression *call )
{
	if ( call->mParams.size() != 1 )
	{
		mHasError = true;
		return nullptr;
	}

	Expression *arg = call->mParams[0];

	// Resolve the argument's struct type name from the AST/Sema annotations.
	std::string structTypeName;
	if ( auto *slit = dynamic_cast<StructLiteralExpression*>( arg ) )
		structTypeName = slit->mTypeName;
	else if ( auto *ve = dynamic_cast<VariableExpression*>( arg ) )
	{
		if ( ve->mVariable != nullptr && ve->mVariable->getVariableType() != nullptr )
			structTypeName = ve->mVariable->getVariableType()->getName();
	}
	else if ( Type *qt = arg->getResolvedType() )
		structTypeName = qt->getName();

	auto sIt = mStructDefMap.find( structTypeName );
	if ( structTypeName.empty() || sIt == mStructDefMap.end() )
	{
		mHasError = true;
		return nullptr;
	}

	llvm::Value *val = genExpression( arg );
	if ( val == nullptr )
		return nullptr;

	// StructName_to_json(ptr self) -> string. Structs are heap pointers, so the
	// generated value is already the self pointer the function expects.
	std::string fnName = structTypeName + "_to_json";
	llvm::Function *toJsonFn = mModule->getFunction( fnName );
	if ( toJsonFn == nullptr )
	{
		mHasError = true;
		return nullptr;
	}

	llvm::Value *result = mBuilder->CreateCall( toJsonFn, { val }, "to_json" );
	trackTempString( result );
	return result;
}

bool CodeGen::genJsonFromJson( StructDefinition *structDef )
{
	llvm::StructType *structType = getOrCreateStructType( structDef );
	std::string funcName = structDef->getName() + "_from_json";

	// StructName_from_json(ptr input) -> ptr (heap-allocated struct)
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get( ptrTy, { ptrTy }, false );
	llvm::Function *func = llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, funcName, mModule.get() );
	func->getArg( 0 )->setName( "input" );

	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create( *mContext, "entry", func );
	mBuilder->SetInsertPoint( entryBB );

	// Extract raw char* from BlangString input (GEP to .data field)
	llvm::StructType *bsType = llvm::StructType::get( *mContext,
		{ ptrTy,
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::Type::getInt32Ty( *mContext ) } );
	llvm::Value *dataPtr = mBuilder->CreateStructGEP(
		bsType, func->getArg( 0 ), 0, "input.data.ptr" );
	llvm::Value *rawInput = mBuilder->CreateLoad( ptrTy, dataPtr, "input.data" );

	// Decode JSON string
	llvm::Value *nullPtr = llvm::ConstantPointerNull::get(
		llvm::PointerType::get( *mContext, 0 ) );
	llvm::Value *jsonObj = mBuilder->CreateCall(
		getOrDeclareJsonDecode(), { rawInput, nullPtr }, "json.obj" );

	// Heap-allocate result struct via ARC (with destructor for refcounted fields)
	llvm::DataLayout dl( mModule.get() );
	uint64_t dataSize = dl.getTypeAllocSize( structType );
	llvm::Value *sizeVal = llvm::ConstantInt::get(
		llvm::Type::getInt64Ty( *mContext ), dataSize );
	std::map<std::string, std::string> emptyTypeSub;
	llvm::Function *dtorFn = getOrGenStructDestructor( structDef, emptyTypeSub );
	llvm::Value *resultAlloca = nullptr;
	if ( dtorFn != nullptr )
		resultAlloca = mBuilder->CreateCall(
			getOrDeclareRcAllocDtor(), { sizeVal, dtorFn }, "result.ptr" );
	else
		resultAlloca = mBuilder->CreateCall(
			getOrDeclareRcAlloc(), { sizeVal }, "result.ptr" );

	// For each field: look up in JSON, extract, store
	for ( unsigned i = 0; i < structDef->mFields.size(); i++ )
	{
		VariableDefinition *field = structDef->mFields[i];
		std::string fieldName = field->getName();
		Type *fieldType = field->getVariableType();
		std::string typeName = fieldType ? fieldType->getName() : "int";

		// Get the field node from JSON
		llvm::Value *keyStr = mBuilder->CreateGlobalStringPtr( fieldName, fieldName + ".key" );
		llvm::Value *fieldNode = mBuilder->CreateCall(
			getOrDeclareJsonObjectGet(), { jsonObj, keyStr }, fieldName + ".node" );

		// Extract typed value
		llvm::Value *fieldVal = nullptr;
		if ( typeName == "int" || typeName == "short" || typeName == "long" )
		{
			llvm::Value *i64Val = mBuilder->CreateCall(
				getOrDeclareJsonGetInt(), { fieldNode }, fieldName + ".i64" );
			if ( typeName == "int" )
				fieldVal = mBuilder->CreateTrunc( i64Val, llvm::Type::getInt32Ty( *mContext ), fieldName + ".val" );
			else if ( typeName == "short" )
				fieldVal = mBuilder->CreateTrunc( i64Val, llvm::Type::getInt16Ty( *mContext ), fieldName + ".val" );
			else
				fieldVal = i64Val;
		}
		else if ( typeName == "char" )
		{
			llvm::Value *i64Val = mBuilder->CreateCall(
				getOrDeclareJsonGetInt(), { fieldNode }, fieldName + ".i64" );
			fieldVal = mBuilder->CreateTrunc( i64Val, llvm::Type::getInt8Ty( *mContext ), fieldName + ".val" );
		}
		else if ( typeName == "float" || typeName == "double" )
		{
			llvm::Value *dblVal = mBuilder->CreateCall(
				getOrDeclareJsonGetFloat(), { fieldNode }, fieldName + ".dbl" );
			if ( typeName == "float" )
				fieldVal = mBuilder->CreateFPTrunc( dblVal, llvm::Type::getFloatTy( *mContext ), fieldName + ".val" );
			else
				fieldVal = dblVal;
		}
		else if ( typeName == "string" )
		{
			// __blang_json_get_string returns raw char*, wrap in BlangString
			llvm::Value *rawStr = mBuilder->CreateCall(
				getOrDeclareJsonGetString(), { fieldNode }, fieldName + ".raw" );
			llvm::Value *len = mBuilder->CreateCall(
				getOrDeclareStrlen(), { rawStr }, fieldName + ".len" );
			fieldVal = mBuilder->CreateCall(
				getOrDeclareStringCreate(), { rawStr, len }, fieldName + ".val" );
		}
		else if ( typeName == "bool" )
		{
			llvm::Value *i32Val = mBuilder->CreateCall(
				getOrDeclareJsonGetBool(), { fieldNode }, fieldName + ".i32" );
			fieldVal = mBuilder->CreateTrunc( i32Val, llvm::Type::getInt1Ty( *mContext ), fieldName + ".val" );
		}
		else
		{
			// Check if this is a nested @json struct
			auto it = mStructDefMap.find( typeName );
			if ( it != mStructDefMap.end() )
			{
				StructDefinition *nestedDef = it->second;
				bool hasJson = false;
				for ( const auto &ann : nestedDef->getAnnotations() )
				{
					if ( ann.mName == "json" )
					{
						hasJson = true;
						break;
					}
				}
				if ( hasJson )
				{
					// Encode the nested JSON node back to a string
					llvm::Value *nestedRawStr = mBuilder->CreateCall(
						getOrDeclareJsonEncode(), { fieldNode }, fieldName + ".rawstr" );

					// Wrap raw char* in BlangString for from_json call
					llvm::Value *nestedLen = mBuilder->CreateCall(
						getOrDeclareStrlen(), { nestedRawStr }, fieldName + ".len" );
					llvm::Value *nestedStr = mBuilder->CreateCall(
						getOrDeclareStringCreate(), { nestedRawStr, nestedLen }, fieldName + ".str" );

					// Free the malloc'd char* from __blang_json_encode (string_create copied it)
					mBuilder->CreateCall( getOrDeclareFree(), { nestedRawStr } );

					// Call NestedStruct_from_json(str) -> ptr (heap-allocated)
					std::string nestedFromJson = typeName + "_from_json";
					llvm::Function *nestedFn = mModule->getFunction( nestedFromJson );
					if ( !nestedFn )
					{
						llvm::FunctionType *nestedFt = llvm::FunctionType::get( ptrTy, { ptrTy }, false );
						nestedFn = llvm::Function::Create(
							nestedFt, llvm::Function::ExternalLinkage, nestedFromJson, mModule.get() );
					}
					fieldVal = mBuilder->CreateCall( nestedFn, { nestedStr }, fieldName + ".val" );

					// Release the intermediate BlangString
					mBuilder->CreateCall( getOrDeclareStringRelease(), { nestedStr } );
				}
				else
				{
					cerr << "Error: @json struct '" << structDef->getName()
						<< "' has field '" << fieldName << "' of struct type '" << typeName
						<< "' which does not have @json annotation" << endl;
					return false;
				}
			}
			else
			{
				cerr << "Error: @json struct '" << structDef->getName()
					<< "' has field '" << fieldName << "' of unsupported type '" << typeName << "'" << endl;
				return false;
			}
		}

		// Store to result struct
		llvm::Value *fieldPtr = mBuilder->CreateStructGEP( structType, resultAlloca, i, fieldName + ".ptr" );
		mBuilder->CreateStore( fieldVal, fieldPtr );
	}

	// Free JSON tree
	mBuilder->CreateCall( getOrDeclareJsonFree(), { jsonObj } );

	// Return the heap-allocated struct pointer
	mBuilder->CreateRet( resultAlloca );
	return true;
}

// ---- JSON runtime declarations ----

llvm::Function *CodeGen::getOrDeclareJsonObject()
{
	return declareExtern( "__blang_json_object", llvm::PointerType::get( *mContext, 0 ), {} );
}

llvm::Function *CodeGen::getOrDeclareJsonInt()
{
	return declareExtern( "__blang_json_int", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareJsonFloat()
{
	return declareExtern( "__blang_json_float", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getDoubleTy( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareJsonString()
{
	return declareExtern( "__blang_json_string", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareJsonBool()
{
	return declareExtern( "__blang_json_bool", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt32Ty( *mContext ) } );
}

llvm::Function *CodeGen::getOrDeclareJsonObjectSet()
{
	return declareExtern( "__blang_json_object_set", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareJsonObjectGet()
{
	return declareExtern( "__blang_json_object_get", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareJsonEncode()
{
	return declareExtern( "__blang_json_encode", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareJsonDecode()
{
	return declareExtern( "__blang_json_decode", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareJsonFree()
{
	return declareExtern( "__blang_json_free", llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareJsonGetInt()
{
	return declareExtern( "__blang_json_get_int", llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareJsonGetFloat()
{
	return declareExtern( "__blang_json_get_float", llvm::Type::getDoubleTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareJsonGetString()
{
	return declareExtern( "__blang_json_get_string", llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}

llvm::Function *CodeGen::getOrDeclareJsonGetBool()
{
	return declareExtern( "__blang_json_get_bool", llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) } );
}
