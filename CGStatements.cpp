#include "CodeGen.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"

#include <iostream>

using namespace QLang;
using namespace std;

void CodeGen::genVariableDeclaration( VariableDeclaration *decl )
{
	for ( auto &data : decl->mVariables )
	{
		VariableDefinition *varDef = data.mVaribale;
		llvm::Type *llvmType = getLLVMType( varDef->getVariableType() );
		OwnershipQualifier ownership = varDef->getOwnership();

		// Check for channel type: chan<T>
		Type *varType = varDef->getVariableType();
		if ( varType != nullptr && varType->getName() == "chan" )
		{
			// chan<T> -> __blang_chan_create(sizeof(T), capacity)
			llvm::Type *ptrType = llvm::PointerType::get( *mContext, 0 );
			llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
				ptrType, nullptr, varDef->getName() );
			mVariableMap[varDef] = alloca;

			// Determine element size from type parameter
			uint64_t elemSize = 4; // default to int-sized
			if ( varType->getNumTypeParams() > 0 )
			{
				llvm::Type *elemType = getLLVMType( varType->getTypeParam( 0 ) );
				llvm::DataLayout dl( mModule.get() );
				elemSize = dl.getTypeAllocSize( elemType );
			}

			// Default capacity of 16
			llvm::Value *sizeVal = llvm::ConstantInt::get(
				llvm::Type::getInt64Ty( *mContext ), elemSize );
			llvm::Value *capVal = llvm::ConstantInt::get(
				llvm::Type::getInt64Ty( *mContext ), 16 );

			llvm::Value *chanPtr = mBuilder->CreateCall(
				getOrDeclareChanCreate(), { sizeVal, capVal }, "chan.ptr" );
			mBuilder->CreateStore( chanPtr, alloca );

			mUsesConcurrency = true;
			continue;
		}

		if ( ownership == OwnershipQualifier::kOwnership_Shared ||
			 ownership == OwnershipQualifier::kOwnership_Sync )
		{
			// Check if this is a user-defined struct type — structs are already
			// heap-allocated with ARC, so we just store the struct ptr directly.
			bool isStructOwnership = false;
			if ( varType != nullptr )
			{
				string stn = varType->getName();
				auto sub2 = mTypeSubstitution.find( stn );
				if ( sub2 != mTypeSubstitution.end() )
					stn = sub2->second->getName();
				isStructOwnership = isUserStructType( stn );
				if ( !isStructOwnership && varType->getNumTypeParams() > 0 )
				{
					std::vector<SmartPtr<Type>> ta;
					for ( int i = 0; i < varType->getNumTypeParams(); i++ )
						ta.push_back( varType->getTypeParam( i ) );
					string mangledStn = mangleGenericName( stn, ta );
					isStructOwnership = ( mStructDefMap.find( mangledStn ) != mStructDefMap.end() );
				}
			}

			if ( isStructOwnership )
			{
				// Struct types are already heap-allocated with their own ARC.
				// Just store the struct ptr in the alloca and track for release.
				llvm::Type *ptrType = llvm::PointerType::get( *mContext, 0 );
				llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
					ptrType, nullptr, varDef->getName() );
				mVariableMap[varDef] = alloca;

				if ( !mStructScopeStack.empty() )
					mStructScopeStack.back().push_back( alloca );

				if ( data.mInitialValue != nullptr )
				{
					llvm::Value *initVal = genExpression( data.mInitialValue );
					if ( initVal != nullptr )
						mBuilder->CreateStore( initVal, alloca );
				}
			}
			else
			{
				// Non-struct types: heap-allocate via runtime ARC.
				// The alloca stores a pointer (opaque ptr) to the heap data.
				llvm::Type *ptrType = llvm::PointerType::get( *mContext, 0 );
				llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
					ptrType, nullptr, varDef->getName() );
				mVariableMap[varDef] = alloca;

				// Track for ARC release at scope exit
				if ( !mArcScopeStack.empty() )
					mArcScopeStack.back().push_back( alloca );

				// Determine data size
				llvm::DataLayout dl( mModule.get() );
				uint64_t dataSize = dl.getTypeAllocSize( llvmType );
				llvm::Value *sizeVal = llvm::ConstantInt::get(
					llvm::Type::getInt64Ty( *mContext ), dataSize );

				// Call __blang_rc_alloc or __blang_rc_alloc_sync
				llvm::Function *allocFn = ( ownership == OwnershipQualifier::kOwnership_Sync )
					? getOrDeclareRcAllocSync() : getOrDeclareRcAlloc();
				llvm::Value *heapPtr = mBuilder->CreateCall( allocFn, { sizeVal }, "rc.ptr" );
				mBuilder->CreateStore( heapPtr, alloca );

				// If there's an initializer, generate and store through the heap pointer
				if ( data.mInitialValue != nullptr )
				{
					llvm::Value *initVal = genExpression( data.mInitialValue );
					if ( initVal != nullptr )
					{
						if ( initVal->getType() != llvmType )
						{
							if ( llvmType->isIntegerTy() && initVal->getType()->isIntegerTy() )
							{
								bool isSigned = !isByteExpression( data.mInitialValue );
								initVal = mBuilder->CreateIntCast( initVal, llvmType, isSigned, "icast" );
							}
						}
						if ( initVal != nullptr )
						{
							// Store through the heap pointer
							mBuilder->CreateStore( initVal, heapPtr );
						}
					}
				}
			}
		}
		else
		{
			// Value type or own: stack allocation (same as before)
			// For user-defined struct types, the alloca stores a heap pointer (ptr)
			// since getLLVMType returns ptr for structs.
			llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
				llvmType, nullptr, varDef->getName() );
			mVariableMap[varDef] = alloca;

			// Check if this is a user-defined struct type (heap-allocated by reference)
			bool isStructVar = false;
			if ( varType != nullptr )
			{
				string sTypeName = varType->getName();
				auto subIt2 = mTypeSubstitution.find( sTypeName );
				if ( subIt2 != mTypeSubstitution.end() )
					sTypeName = subIt2->second->getName();
				isStructVar = isUserStructType( sTypeName );
				if ( !isStructVar && varType->getNumTypeParams() > 0 )
				{
					std::vector<SmartPtr<Type>> typeArgs;
					for ( int tpi = 0; tpi < varType->getNumTypeParams(); tpi++ )
						typeArgs.push_back( varType->getTypeParam( tpi ) );
					string mangledTypeName = mangleGenericName( sTypeName, typeArgs );
					isStructVar = ( mStructDefMap.find( mangledTypeName ) != mStructDefMap.end() );
				}
			}

			// Track struct variables for __blang_rc_release at scope exit
			if ( isStructVar && !mStructScopeStack.empty() )
			{
				mStructScopeStack.back().push_back( alloca );
			}

			// Track string variables for release at scope exit. The declared name
			// is resolved through the active generic substitution so a `T`-typed
			// local inside a monomorphized generic (sort<string>'s `T tmp`)
			// participates in refcounting like a directly-declared string.
			if ( resolvedTypeName( varType ) == "string" &&
				 !mStringScopeStack.empty() )
			{
				mStringScopeStack.back().push_back( { alloca, varDef } );
			}

			// Track array variables for release at scope exit (substitution-aware,
			// same rationale as strings above).
			if ( resolvedTypeName( varType ) == "Array" &&
				 !mArrayScopeStack.empty() )
			{
				mArrayScopeStack.back().push_back( { alloca, varDef } );
			}

			// Track buffer variables for release at scope exit
			// (only when Buffer is a builtin type, not a struct)
			if ( varType != nullptr && varType->getName() == "Buffer" &&
				 !mBufferScopeStack.empty() &&
				 mStructDefMap.find( "Buffer" ) == mStructDefMap.end() )
			{
				mBufferScopeStack.back().push_back( { alloca, varDef } );
			}

			// Track fn-typed variables for lambda context release at scope exit
			if ( varType != nullptr && varType->isFunctionType() &&
				 !mLambdaScopeStack.empty() )
			{
				mLambdaScopeStack.back().push_back( { alloca, varDef } );
			}

			// Track enum variables with refcounted payloads for cleanup at scope exit
			if ( varType != nullptr && !mEnumScopeStack.empty() )
			{
				string enumTypeName = varType->getName();
				auto enumIt = mEnumDefMap.find( enumTypeName );
				if ( enumIt != mEnumDefMap.end() )
				{
					// Check if any variant has a refcounted payload. For a generic
					// enum (built-in Option<T>/Result<T,E>) resolve each variant's
					// generic-param payload to the concrete type from the variable's
					// own type (e.g. Option<string>), so string/Array payloads are
					// released at scope exit rather than leaked.
					EnumDefinition *ed = enumIt->second;
					bool hasRefPayload = false;
					for ( auto &variant : ed->mVariants )
					{
						for ( auto &assocType : variant.mAssociatedTypes )
						{
							string atn = resolveVariantPayloadType(
								(Type *)assocType, ed, varType )->getName();
							if ( atn == "string" || atn == "Array" || atn == "Buffer" ||
								 isUserStructType( atn ) )
							{
								hasRefPayload = true;
								break;
							}
						}
						if ( hasRefPayload ) break;
					}
					if ( hasRefPayload )
						mEnumScopeStack.back().push_back( { alloca, ed, varType } );
				}
			}

			// If there's an initializer, generate it and store
			if ( data.mInitialValue != nullptr )
			{
				// For Array<T> declarations with empty literal initializer [],
				// set the element type hint so genArrayLiteral uses the correct
				// element size (e.g. 8 bytes for string/pointer types, not default 4)
				if ( varType != nullptr && varType->getName() == "Array" &&
					 varType->getNumTypeParams() > 0 &&
					 dynamic_cast<ArrayLiteralExpression*>( (Expression*)data.mInitialValue ) != nullptr )
				{
					Type *elemType = varType->getTypeParam( 0 );
					mArrayElemTypeHint = getLLVMType( elemType );
					string etn = elemType->getName();
					auto subEtn = mTypeSubstitution.find( etn );
					if ( subEtn != mTypeSubstitution.end() )
						etn = subEtn->second->getName();
					mArrayElemTypeNameHint = etn;
				}

				llvm::Value *initVal = genExpression( data.mInitialValue );
				if ( initVal != nullptr )
				{
					// For struct variables initialized from another variable or field
					// access, retain the reference (the source keeps its own reference).
					// For new allocations (struct literal, function return), the refcount
					// is already 1 (ownership transfer, no retain needed).
					if ( isStructVar )
					{
						auto *srcVarExpr = dynamic_cast<VariableExpression*>(
							(Expression*)data.mInitialValue );
						auto *srcFieldExpr = dynamic_cast<FieldAccessExpression*>(
							(Expression*)data.mInitialValue );
						// Array element access (arr[i]) returns a borrowed reference —
						// __blang_array_get does not retain — so binding it to a tracked
						// local must retain, exactly like a variable copy or field access.
						// Without this the local's scope-exit release double-frees the
						// element the array still owns. (Ported from origin 535058b.)
						auto *srcIndexExpr = dynamic_cast<IndexExpression*>(
							(Expression*)data.mInitialValue );
						if ( srcVarExpr != nullptr || srcFieldExpr != nullptr ||
							 srcIndexExpr != nullptr )
						{
							mBuilder->CreateCall( getOrDeclareRcRetain(), { initVal } );
						}
					}

					// Resolve the declared type through the active generic
					// substitution so `T`-typed locals inside monomorphized
					// generics get the same borrowed-source retains as directly
					// declared string/Array locals. This moves together with the
					// substitution-aware scope tracking above and the
					// substitution-aware isStringType/isArrayType predicates —
					// changing only one of those sites unbalances the counts.
					string boundTypeName = resolvedTypeName( varType );

					// Same borrowed-source rule for Array-typed locals: binding an
					// element of a nested array (Array<int> row = grid[i]) — or a
					// variable/field copy — creates a second tracked owner, so it
					// must retain. Without this, the local's scope-exit release and
					// the outer array's elem_dtor double-free the same array.
					if ( boundTypeName == "Array" )
					{
						Expression *srcExpr = (Expression *)data.mInitialValue;
						if ( dynamic_cast<VariableExpression*>( srcExpr ) != nullptr ||
							 dynamic_cast<FieldAccessExpression*>( srcExpr ) != nullptr ||
							 dynamic_cast<IndexExpression*>( srcExpr ) != nullptr )
						{
							mBuilder->CreateCall( getOrDeclareArrayRetain(), { initVal } );
						}
					}

					// And for string locals bound from a borrowed source: a plain
					// variable copy (string b = a;) or an array element
					// (string s = args[i];) creates a second tracked owner, which
					// must retain — genVariableExpression and genIndexExpression
					// return borrows. FieldAccess is deliberately EXCLUDED: string
					// field reads already retain + track as a statement temp
					// (CGStruct genFieldAccess), and the untrackTempString below
					// transfers that reference to the variable. Owned sources
					// (literals, concat/interp temps, call results) also transfer
					// via untrack. This was known-issue #1 ("array-element string
					// ARC") — the earlier reverted attempt retained at the
					// IndexExpression node, which double-counted the already-
					// balanced flows; retaining only at this binding site is safe.
					if ( boundTypeName == "string" )
					{
						Expression *srcExpr = (Expression *)data.mInitialValue;
						auto *srcVar = dynamic_cast<VariableExpression*>( srcExpr );

						// An own-from-own initialization is a MOVE: the single
						// reference transfers (the source is marked moved and
						// skipped at scope release below), so no retain.
						bool isMove = false;
						if ( ownership == OwnershipQualifier::kOwnership_Own &&
							 srcVar != nullptr &&
							 srcVar->getVariable()->getOwnership() ==
								 OwnershipQualifier::kOwnership_Own )
							isMove = true;

						if ( !isMove &&
							 ( srcVar != nullptr ||
							   dynamic_cast<IndexExpression*>( srcExpr ) != nullptr ) )
						{
							mBuilder->CreateCall( getOrDeclareStringRetain(), { initVal } );
						}
					}

					// Cast if types don't match (skip for struct ptrs which are already ptr)
					if ( !isStructVar && initVal->getType() != llvmType )
					{
						if ( llvmType->isIntegerTy() && initVal->getType()->isIntegerTy() )
						{
							bool isSigned = !isByteExpression( data.mInitialValue );
							initVal = mBuilder->CreateIntCast( initVal, llvmType, isSigned, "icast" );
						}
						else if ( llvmType->isFloatTy() && initVal->getType()->isDoubleTy() )
							initVal = mBuilder->CreateFPTrunc( initVal, llvmType, "fptrunc" );
						else if ( llvmType->isDoubleTy() && initVal->getType()->isFloatTy() )
							initVal = mBuilder->CreateFPExt( initVal, llvmType, "fpext" );
						// U4 (REQ-012): the silent dropped-initializer fallback (a null
						// assignment followed by a skipped store) is deleted. Incompatible
						// initializers are rejected by sema before codegen; only the
						// documented numeric conversions above remain.
					}
					if ( initVal != nullptr )
					{
						mBuilder->CreateStore( initVal, alloca );

						// If storing a string, untrack it from temps — the variable now
						// owns it (substitution-aware: T-typed locals transfer too)
						if ( resolvedTypeName( varType ) == "string" )
							untrackTempString( initVal );

						// If storing a struct, untrack it from temps — the variable now owns it
						if ( isStructVar )
							untrackTempStruct( initVal );

						// If storing an array, untrack it from temps — the variable now
						// owns it (released at scope exit via mArrayScopeStack).
						if ( resolvedTypeName( varType ) == "Array" )
							untrackTempArray( initVal );

						// If storing a fn-typed value, untrack its lambda context from temps.
						// The variable now owns the context via mLambdaScopeStack.
						if ( varType != nullptr && varType->isFunctionType() &&
							 !mTempLambdaCtxs.empty() )
						{
							mTempLambdaCtxs.pop_back();
						}
					}
				}

				// Move semantics: if this is an own variable initialized from
				// another own variable, mark the source as moved
				if ( ownership == OwnershipQualifier::kOwnership_Own )
				{
					auto *srcVarExpr = dynamic_cast<VariableExpression*>( (Expression*)data.mInitialValue );
					if ( srcVarExpr != nullptr )
					{
						VariableDefinition *srcDef = srcVarExpr->getVariable();
						if ( srcDef->getOwnership() == OwnershipQualifier::kOwnership_Own )
						{
							if ( mInsideLoop )
							{
								cerr << "Error: cannot move own variable '" << srcDef->getName()
									 << "' inside a loop (would move on each iteration)" << endl;
								mHasError = true;
								return;
							}
							mMovedVariables.insert( srcDef );
						}
					}
				}
			}
		}
	}
}

void CodeGen::emitScopeStackReleases()
{
	// Emit ARC releases for all in-scope shared/sync variables
	for ( auto it = mArcScopeStack.rbegin(); it != mArcScopeStack.rend(); ++it )
	{
		for ( auto *alloca : *it )
		{
			llvm::Value *heapPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), alloca, "rc.ret.ptr" );
			mBuilder->CreateCall( getOrDeclareRcRelease(), { heapPtr } );
		}
	}

	// Release string variables (skip moved vars)
	for ( auto it = mStringScopeStack.rbegin(); it != mStringScopeStack.rend(); ++it )
	{
		for ( auto &entry : *it )
		{
			if ( entry.second != nullptr && mMovedVariables.count( entry.second ) )
				continue;
			llvm::Value *strPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), entry.first, "str.ret.ptr" );
			mBuilder->CreateCall( getOrDeclareStringRelease(), { strPtr } );
		}
	}

	// Release array variables (skip moved vars)
	for ( auto it = mArrayScopeStack.rbegin(); it != mArrayScopeStack.rend(); ++it )
	{
		for ( auto &entry : *it )
		{
			if ( entry.second != nullptr && mMovedVariables.count( entry.second ) )
				continue;
			llvm::Value *arrPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), entry.first, "arr.ret.ptr" );
			mBuilder->CreateCall( getOrDeclareArrayRelease(), { arrPtr } );
		}
	}

	// Release buffer variables (skip moved vars)
	for ( auto it = mBufferScopeStack.rbegin(); it != mBufferScopeStack.rend(); ++it )
	{
		for ( auto &entry : *it )
		{
			if ( entry.second != nullptr && mMovedVariables.count( entry.second ) )
				continue;
			llvm::Value *bufPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), entry.first, "buf.ret.ptr" );
			mBuilder->CreateCall( getOrDeclareBufferRelease(), { bufPtr } );
		}
	}

	// Release lambda/fn-typed variable contexts (skip moved vars)
	for ( auto it = mLambdaScopeStack.rbegin(); it != mLambdaScopeStack.rend(); ++it )
	{
		for ( auto &entry : *it )
		{
			if ( entry.second != nullptr && mMovedVariables.count( entry.second ) )
				continue;
			llvm::Type *pairType = llvm::StructType::get( *mContext, {
				llvm::PointerType::get( *mContext, 0 ),
				llvm::PointerType::get( *mContext, 0 )
			} );
			llvm::Value *pairVal = mBuilder->CreateLoad(
				pairType, entry.first, "fn.ret.pair" );
			llvm::Value *ctxPtr = mBuilder->CreateExtractValue(
				pairVal, 1, "fn.ret.ctx" );
			mBuilder->CreateCall( getOrDeclareLambdaCtxRelease(), { ctxPtr } );
		}
	}

	// Release heap-allocated struct variables
	for ( auto it = mStructScopeStack.rbegin(); it != mStructScopeStack.rend(); ++it )
	{
		for ( auto *structAlloca : *it )
		{
			llvm::Value *structPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), structAlloca, "struct.ret.ptr" );
			mBuilder->CreateCall( getOrDeclareRcRelease(), { structPtr } );
		}
	}

	// Release enum variables with refcounted payloads
	for ( auto it = mEnumScopeStack.rbegin(); it != mEnumScopeStack.rend(); ++it )
	{
		for ( auto &entry : *it )
		{
			emitEnumPayloadRelease( entry.alloca, entry.enumDef, entry.concreteType );
		}
	}
}

void CodeGen::genReturnStatement( ReturnStatement *ret )
{
	// Generate the return value FIRST, before releasing scope variables.
	// This prevents use-after-free when returning a string/array variable
	// that would be released by the scope cleanup below.
	llvm::Value *retVal = nullptr;
	if ( ret->mExpression != nullptr )
	{
		retVal = genExpression( ret->mExpression );

		// If returning a string, retain it so scope release doesn't free it
		if ( retVal != nullptr && isStringType( ret->mExpression ) )
			mBuilder->CreateCall( getOrDeclareStringRetain(), { retVal } );

		// If returning an array sourced from an existing owner (a local array
		// variable or a struct field), retain it so it survives the scope
		// cleanup below (local var) / so the caller receives its own owned
		// reference (field). A fresh array from a call/method result already
		// carries an owned reference (tracked as a temp at its producing site and
		// untracked below), so it must NOT be retained again — that double count
		// is the source of the Buffer.get_bytes()/list_dir array leaks. This
		// mirrors the struct-return retain policy immediately below.
		if ( retVal != nullptr && isArrayType( ret->mExpression ) )
		{
			Expression *retRawExpr = (Expression *)ret->mExpression;
			// IndexExpression is a borrowed source too: __blang_array_get does
			// not retain, so returning an element (a generic Map's
			// `return self.values[idx]` with V=Array) must hand the caller its
			// own reference or the caller's release corrupts the container.
			if ( dynamic_cast<VariableExpression*>( retRawExpr ) != nullptr ||
				 dynamic_cast<FieldAccessExpression*>( retRawExpr ) != nullptr ||
				 dynamic_cast<IndexExpression*>( retRawExpr ) != nullptr )
				mBuilder->CreateCall( getOrDeclareArrayRetain(), { retVal } );
		}

		// If returning a fn-typed value (lambda/callback pair), retain the
		// context so scope cleanup doesn't free it before the caller gets it
		if ( retVal != nullptr && mCurrentFunction != nullptr &&
			 mCurrentFunction->getReturnType() != nullptr &&
			 mCurrentFunction->getReturnType()->isFunctionType() )
		{
			llvm::Value *ctxPtr = mBuilder->CreateExtractValue(
				retVal, 1, "ret.fn.ctx" );
			mBuilder->CreateCall( getOrDeclareLambdaCtxRetain(), { ctxPtr } );
		}

		// If returning a heap-allocated struct, retain the pointer so it
		// survives the scope cleanup below (which will release the local reference).
		// Only retain when the source expression is a borrowed read — a variable,
		// a field access, or an array-element index (__blang_array_get does not
		// retain, so a generic Map's `return self.values[idx]` with V = a struct
		// must hand the caller its own reference, mirroring the Array policy
		// above). New allocations (struct literals, function call results) already
		// have refcount=1 and transfer ownership directly to the caller. The
		// declared return type is resolved through the active monomorphization
		// substitution so a generic `-> V` participates when V is a struct.
		if ( retVal != nullptr && mCurrentFunction != nullptr &&
			 mCurrentFunction->getReturnType() != nullptr )
		{
			string retTypeName = resolvedTypeName( mCurrentFunction->getReturnType() );
			if ( isUserStructType( retTypeName ) )
			{
				bool needsRetain = false;
				Expression *retRawExpr = (Expression *)ret->mExpression;
				auto *retExpr = dynamic_cast<VariableExpression*>( retRawExpr );
				auto *retField = dynamic_cast<FieldAccessExpression*>( retRawExpr );
				auto *retIndex = dynamic_cast<IndexExpression*>( retRawExpr );
				if ( retExpr != nullptr || retField != nullptr || retIndex != nullptr )
					needsRetain = true;
				if ( needsRetain )
					mBuilder->CreateCall( getOrDeclareRcRetain(), { retVal } );
			}
		}
	}

	// Release temporary strings created during expression evaluation
	releaseTempStrings();

	// Untrack the returned value if it is a tracked temporary struct: ownership
	// transfers to the caller, so releaseTempStructs() below must NOT release it
	// (that would return an already-freed pointer — a use-after-free). This is
	// done UNCONDITIONALLY rather than gated on mCurrentFunction->getReturnType()
	// because mCurrentFunction is null while generating a lambda body (see
	// CGLambda.cpp) — the old gate silently skipped the untrack for lambdas that
	// return a struct rvalue (e.g. `return http_ok("OK");`), releasing the value
	// just before `ret`. untrackTempStruct only removes the exact returned
	// pointer if it is present in the temp list, and a value being returned is
	// never also a statement-local temporary, so the unconditional form is safe
	// for every return (non-struct returns are a no-op).
	if ( retVal != nullptr )
		untrackTempStruct( retVal );
	releaseTempStructs();

	// Same ownership transfer for a returned array temporary: untrack it (so the
	// statement-end release below does not free the value being returned) and
	// release any other array temporaries produced while evaluating the return
	// expression.
	if ( retVal != nullptr )
		untrackTempArray( retVal );
	releaseTempArrays();

	// Insert runtime shutdown before ARC releases in main() — threads must
	// finish before we free shared/sync memory they may be using
	if ( mCurrentFunction != nullptr && mCurrentFunction->getName() == "main" && mUsesConcurrency )
	{
		mBuilder->CreateCall( getOrDeclareRuntimeShutdown(), {} );
	}

	// Release every in-scope local (shared/sync, string, array, buffer, lambda,
	// struct, enum payload) before returning. Shared with the `?` operator's
	// early-return error path (CGEnum.cpp) so both exits run identical cleanup.
	emitScopeStackReleases();

	// In an async wrapper, return statements store the value and branch to exit
	if ( mAsyncExitBB != nullptr )
	{
		if ( retVal != nullptr && mAsyncResultAlloca != nullptr )
		{
			// Cast if needed
			if ( retVal->getType() != mAsyncReturnType )
			{
				if ( mAsyncReturnType->isIntegerTy() && retVal->getType()->isIntegerTy() )
					retVal = mBuilder->CreateIntCast( retVal, mAsyncReturnType, true, "icast" );
			}
			mBuilder->CreateStore( retVal, mAsyncResultAlloca );
		}
		mBuilder->CreateBr( mAsyncExitBB );
		return;
	}

	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();
	llvm::Type *expectedType = func->getReturnType();

	if ( retVal != nullptr )
	{
		// Cast if the value type doesn't match the function return type
		if ( retVal->getType() != expectedType )
		{
			if ( expectedType->isFloatTy() && retVal->getType()->isDoubleTy() )
				retVal = mBuilder->CreateFPTrunc( retVal, expectedType, "fptrunc" );
			else if ( expectedType->isDoubleTy() && retVal->getType()->isFloatTy() )
				retVal = mBuilder->CreateFPExt( retVal, expectedType, "fpext" );
			else if ( expectedType->isIntegerTy() && retVal->getType()->isIntegerTy() )
				retVal = mBuilder->CreateIntCast( retVal, expectedType, true, "icast" );
			// U4 (REQ-012): the return-type fabrication coercions (getNullValue for
			// a struct return, ptrtoint, inttoptr) are deleted. Return-type
			// mismatches are rejected by sema before codegen; only the documented
			// numeric conversions above remain.
		}

		// Store return value and check ensures (postcondition) clauses
		if ( mResultAlloca != nullptr && mCurrentFunction != nullptr )
		{
			mBuilder->CreateStore( retVal, mResultAlloca );
			for ( auto &clause : mCurrentFunction->mEnsuresClauses )
			{
				genContractCheck( clause, "Postcondition violated" );
			}
		}

		mBuilder->CreateRet( retVal );
	}
	else if ( ret->mExpression != nullptr )
	{
		// Expression was present but genExpression returned null
		if ( expectedType->isVoidTy() )
			mBuilder->CreateRetVoid();
		else
			mBuilder->CreateRet( llvm::Constant::getNullValue( expectedType ) );
	}
	else
	{
		// Check ensures for void return
		if ( mCurrentFunction != nullptr )
		{
			for ( auto &clause : mCurrentFunction->mEnsuresClauses )
			{
				genContractCheck( clause, "Postcondition violated" );
			}
		}
		mBuilder->CreateRetVoid();
	}
}

void CodeGen::genIfStatement( IfStatement *ifStmt )
{
	llvm::Value *condVal = genExpression( ifStmt->mIfExpression );
	if ( condVal == nullptr )
		return;

	// Convert condition to a bool (i1) by comparing != 0
	if ( !condVal->getType()->isIntegerTy( 1 ) )
	{
		condVal = mBuilder->CreateICmpNE(
			condVal,
			llvm::ConstantInt::get( condVal->getType(), 0 ),
			"ifcond" );
	}

	// Release any temps created during condition evaluation (e.g., string
	// comparison literals) before branching.  Both branches may diverge
	// (early return, etc.) so we must release before the split.
	releaseTempStrings();

	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();

	llvm::BasicBlock *thenBB = llvm::BasicBlock::Create( *mContext, "then", func );
	llvm::BasicBlock *elseBB = llvm::BasicBlock::Create( *mContext, "else", func );
	llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create( *mContext, "ifmerge", func );

	mBuilder->CreateCondBr( condVal, thenBB, elseBB );

	// Save moved set before branches for conservative union
	auto savedMoved = mMovedVariables;

	// Then block
	mBuilder->SetInsertPoint( thenBB );
	if ( ifStmt->mStatement != nullptr )
		genStatement( ifStmt->mStatement );
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateBr( mergeBB );

	// Capture moves from then branch
	auto thenMoved = mMovedVariables;

	// Restore to pre-branch state for else
	mMovedVariables = savedMoved;

	// Else block
	mBuilder->SetInsertPoint( elseBB );
	if ( ifStmt->mElseStatement != nullptr )
		genStatement( ifStmt->mElseStatement );
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateBr( mergeBB );

	// Conservative: union moves from both branches
	// If moved in either branch, consider moved after the if/else
	mMovedVariables.insert( thenMoved.begin(), thenMoved.end() );

	// Continue at merge
	mBuilder->SetInsertPoint( mergeBB );
}

void CodeGen::genWhileStatement( WhileStatement *whileStmt )
{
	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();

	llvm::BasicBlock *condBB = llvm::BasicBlock::Create( *mContext, "whilecond", func );
	llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create( *mContext, "whilebody", func );
	llvm::BasicBlock *afterBB = llvm::BasicBlock::Create( *mContext, "whileend", func );

	mBuilder->CreateBr( condBB );

	// Condition block
	mBuilder->SetInsertPoint( condBB );
	llvm::Value *condVal = genExpression( whileStmt->mLoopExpression );
	if ( condVal != nullptr )
	{
		if ( !condVal->getType()->isIntegerTy( 1 ) )
		{
			condVal = mBuilder->CreateICmpNE(
				condVal,
				llvm::ConstantInt::get( condVal->getType(), 0 ),
				"whilecond" );
		}
		// Release condition temps before branching
		releaseTempStrings();
		mBuilder->CreateCondBr( condVal, bodyBB, afterBB );
	}

	// Push loop targets for break/continue
	mLoopStack.push_back( { condBB, afterBB } );

	// Body block
	mBuilder->SetInsertPoint( bodyBB );
	bool savedInsideLoop = mInsideLoop;
	mInsideLoop = true;
	if ( whileStmt->mLoopStatement != nullptr )
		genStatement( whileStmt->mLoopStatement );
	mInsideLoop = savedInsideLoop;
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateBr( condBB );

	mLoopStack.pop_back();

	// Continue after loop
	mBuilder->SetInsertPoint( afterBB );
}

void CodeGen::genForStatement( ForStatement *forStmt )
{
	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();

	// Init expression (in current block)
	if ( forStmt->mInitialExpression != nullptr )
		genExpression( forStmt->mInitialExpression );

	llvm::BasicBlock *condBB = llvm::BasicBlock::Create( *mContext, "forcond", func );
	llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create( *mContext, "forbody", func );
	llvm::BasicBlock *iterBB = llvm::BasicBlock::Create( *mContext, "foriter", func );
	llvm::BasicBlock *afterBB = llvm::BasicBlock::Create( *mContext, "forend", func );

	mBuilder->CreateBr( condBB );

	// Condition
	mBuilder->SetInsertPoint( condBB );
	if ( forStmt->mTestExpression != nullptr )
	{
		llvm::Value *condVal = genExpression( forStmt->mTestExpression );
		if ( condVal != nullptr )
		{
			if ( !condVal->getType()->isIntegerTy( 1 ) )
			{
				condVal = mBuilder->CreateICmpNE(
					condVal,
					llvm::ConstantInt::get( condVal->getType(), 0 ),
					"forcond" );
			}
			mBuilder->CreateCondBr( condVal, bodyBB, afterBB );
		}
	}
	else
	{
		mBuilder->CreateBr( bodyBB );
	}

	// Push loop targets: continue goes to iter, break goes to after
	mLoopStack.push_back( { iterBB, afterBB } );

	// Body
	mBuilder->SetInsertPoint( bodyBB );
	if ( forStmt->mStatement != nullptr )
		genStatement( forStmt->mStatement );
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateBr( iterBB );

	mLoopStack.pop_back();

	// Iteration
	mBuilder->SetInsertPoint( iterBB );
	if ( forStmt->mIterationExpression != nullptr )
		genExpression( forStmt->mIterationExpression );
	mBuilder->CreateBr( condBB );

	// Continue after loop
	mBuilder->SetInsertPoint( afterBB );
}

void CodeGen::genAssertStatement( AssertStatement *assertStmt )
{
	llvm::Value *condVal = genExpression( assertStmt->mExpression );
	if ( condVal == nullptr )
		return;

	// Convert condition to i1 if needed
	if ( !condVal->getType()->isIntegerTy( 1 ) )
	{
		condVal = mBuilder->CreateICmpNE(
			condVal,
			llvm::ConstantInt::get( condVal->getType(), 0 ),
			"assertcond" );
	}

	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();
	llvm::BasicBlock *failBB = llvm::BasicBlock::Create( *mContext, "assert.fail", func );
	llvm::BasicBlock *passBB = llvm::BasicBlock::Create( *mContext, "assert.pass", func );

	mBuilder->CreateCondBr( condVal, passBB, failBB );

	// Fail block: print message and exit(1)
	mBuilder->SetInsertPoint( failBB );

	llvm::Function *putsFunc = getOrDeclarePuts();
	llvm::Function *exitFunc = getOrDeclareExit();

	std::string msg = assertStmt->mMessage.empty()
		? "Assertion failed" : assertStmt->mMessage;

	// In test-runner mode, prefix the failure with the assert's source location
	// (<file>:<line>:) so `bcc test` reports where a test failed. The path is
	// emitted verbatim from the AST SourceLocation and contains no ':' before
	// the extension, so it matches the epic's `[^:]+\.b:[0-9]+:` regex. Outside
	// test mode the message is unchanged (normal-build codegen invariant).
	if ( mTestMode )
	{
		const SourceLocation &loc = assertStmt->getLocation();
		if ( loc.isSet() )
		{
			msg = loc.file + ":" + std::to_string( loc.line ) + ":" +
				std::to_string( loc.col ) + ": assertion failed: " + msg;
		}
	}

	llvm::Value *msgVal = mBuilder->CreateGlobalStringPtr( msg, "assert.msg" );
	mBuilder->CreateCall( putsFunc, { msgVal } );
	mBuilder->CreateCall( exitFunc,
		{ llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), 1 ) } );
	mBuilder->CreateUnreachable();

	// Continue in pass block
	mBuilder->SetInsertPoint( passBB );
}

// ---- Phase 2: Contract check codegen ----

void CodeGen::genContractCheck( Expression *condition, const std::string &message )
{
	llvm::Value *condVal = genExpression( condition );
	if ( condVal == nullptr )
		return;

	// Convert to i1 if needed
	if ( !condVal->getType()->isIntegerTy( 1 ) )
	{
		condVal = mBuilder->CreateICmpNE(
			condVal,
			llvm::ConstantInt::get( condVal->getType(), 0 ),
			"contractcond" );
	}

	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();
	llvm::BasicBlock *failBB = llvm::BasicBlock::Create( *mContext, "contract.fail", func );
	llvm::BasicBlock *passBB = llvm::BasicBlock::Create( *mContext, "contract.pass", func );

	mBuilder->CreateCondBr( condVal, passBB, failBB );

	// Fail block: print message and exit(1)
	mBuilder->SetInsertPoint( failBB );

	llvm::Function *putsFunc = getOrDeclarePuts();
	llvm::Function *exitFunc = getOrDeclareExit();

	llvm::Value *msgVal = mBuilder->CreateGlobalStringPtr( message, "contract.msg" );
	mBuilder->CreateCall( putsFunc, { msgVal } );
	mBuilder->CreateCall( exitFunc,
		{ llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), 1 ) } );
	mBuilder->CreateUnreachable();

	// Continue in pass block
	mBuilder->SetInsertPoint( passBB );
}

void CodeGen::genForInStatement( ForInStatement *forInStmt )
{
	// Check for infinite loop: for { ... }
	if ( forInStmt->mIsInfinite )
	{
		llvm::Function *func = mBuilder->GetInsertBlock()->getParent();
		llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create( *mContext, "forinf.body", func );
		llvm::BasicBlock *afterBB = llvm::BasicBlock::Create( *mContext, "forinf.end", func );

		mBuilder->CreateBr( bodyBB );
		mBuilder->SetInsertPoint( bodyBB );

		// Push loop targets: continue goes back to body, break goes to after
		mLoopStack.push_back( { bodyBB, afterBB } );

		bool savedInsideLoop = mInsideLoop;
		mInsideLoop = true;
		if ( forInStmt->mBody != nullptr )
			genStatement( forInStmt->mBody );
		mInsideLoop = savedInsideLoop;

		if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
			mBuilder->CreateBr( bodyBB );

		mLoopStack.pop_back();

		mBuilder->SetInsertPoint( afterBB );
		return;
	}

	// Range-based for-in: for x in start..end { ... }
	if ( forInStmt->mIterableExpression != nullptr )
	{
		auto *rangeExpr = dynamic_cast<RangeExpression*>( (Expression*)forInStmt->mIterableExpression );
		if ( rangeExpr != nullptr )
		{
			llvm::Function *func = mBuilder->GetInsertBlock()->getParent();

			// Generate range bounds
			llvm::Value *startVal = genExpression( rangeExpr->mStart );
			llvm::Value *endVal = genExpression( rangeExpr->mEnd );
			if ( startVal == nullptr || endVal == nullptr )
				return;

			// Promote start/end to matching types if widths differ
			if ( startVal->getType() != endVal->getType() &&
				 startVal->getType()->isIntegerTy() && endVal->getType()->isIntegerTy() )
			{
				unsigned startBits = startVal->getType()->getIntegerBitWidth();
				unsigned endBits = endVal->getType()->getIntegerBitWidth();
				if ( startBits < endBits )
					startVal = mBuilder->CreateSExt( startVal, endVal->getType(), "range.promote" );
				else
					endVal = mBuilder->CreateSExt( endVal, startVal->getType(), "range.promote" );
			}

			// Create the loop variable alloca
			llvm::Type *iterType = startVal->getType();
			llvm::AllocaInst *iterAlloca = mBuilder->CreateAlloca(
				iterType, nullptr, forInStmt->mVariableName );
			mBuilder->CreateStore( startVal, iterAlloca );

			// Look up the VariableDefinition from the body's scope to register the alloca
			if ( forInStmt->mBody != nullptr )
			{
				Block *bodyBlock = dynamic_cast<Block*>( (Statement*)forInStmt->mBody );
				if ( bodyBlock != nullptr && bodyBlock->mScope != nullptr )
				{
					Symbol *iterSym = bodyBlock->mScope->findSymbol( forInStmt->mVariableName );
					if ( auto *iterVar = dynamic_cast<VariableDefinition*>( iterSym ) )
						mVariableMap[iterVar] = iterAlloca;
				}
			}

			llvm::BasicBlock *condBB = llvm::BasicBlock::Create( *mContext, "forin.cond", func );
			llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create( *mContext, "forin.body", func );
			llvm::BasicBlock *iterBB = llvm::BasicBlock::Create( *mContext, "forin.iter", func );
			llvm::BasicBlock *afterBB = llvm::BasicBlock::Create( *mContext, "forin.end", func );

			mBuilder->CreateBr( condBB );

			// Condition: i < end
			mBuilder->SetInsertPoint( condBB );
			llvm::Value *curVal = mBuilder->CreateLoad( iterType, iterAlloca, "cur" );
			llvm::Value *cond = mBuilder->CreateICmpSLT( curVal, endVal, "forin.cmp" );
			mBuilder->CreateCondBr( cond, bodyBB, afterBB );

			// Push loop targets: continue goes to iter, break goes to after
			mLoopStack.push_back( { iterBB, afterBB } );

			// Body
			mBuilder->SetInsertPoint( bodyBB );
			bool savedInsideLoop = mInsideLoop;
			mInsideLoop = true;
			if ( forInStmt->mBody != nullptr )
				genStatement( forInStmt->mBody );
			mInsideLoop = savedInsideLoop;
			if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
				mBuilder->CreateBr( iterBB );

			mLoopStack.pop_back();

			// Iteration: i = i + 1
			mBuilder->SetInsertPoint( iterBB );
			llvm::Value *nextVal = mBuilder->CreateAdd(
				mBuilder->CreateLoad( iterType, iterAlloca, "i" ),
				llvm::ConstantInt::get( iterType, 1 ), "inc" );
			mBuilder->CreateStore( nextVal, iterAlloca );
			mBuilder->CreateBr( condBB );

			// After
			mBuilder->SetInsertPoint( afterBB );
			return;
		}
	}

	// Array-based for-in: for x in arrayExpr { ... }
	if ( forInStmt->mIterableExpression != nullptr &&
		 isArrayType( forInStmt->mIterableExpression ) )
	{
		llvm::Function *func = mBuilder->GetInsertBlock()->getParent();

		// Generate the array expression
		llvm::Value *arrVal = genExpression( forInStmt->mIterableExpression );
		if ( arrVal == nullptr )
			return;

		// Get the array length
		llvm::Value *lenVal = mBuilder->CreateCall(
			getOrDeclareArrayLength(), { arrVal }, "arr.len" );

		// Determine element type from the array's type annotation
		llvm::Type *elemType = llvm::Type::getInt32Ty( *mContext ); // default
		Type *elemQType = nullptr;
		if ( auto *ve = dynamic_cast<VariableExpression*>(
				 (Expression*)forInStmt->mIterableExpression ) )
		{
			Type *varType = ve->mVariable->getVariableType();
			if ( varType != nullptr && varType->getNumTypeParams() > 0 )
				elemQType = varType->getTypeParam( 0 );
		}
		else if ( auto *fa = dynamic_cast<FieldAccessExpression*>(
				 (Expression*)forInStmt->mIterableExpression ) )
		{
			// Field iterable (for k in counts.keys): getFieldType is instance-
			// aware, so a generic struct's Array<K> field resolves to the
			// concrete element type (Array<string> for a Map<string,int>).
			Type *fieldType = getFieldType( fa );
			if ( fieldType != nullptr && fieldType->getNumTypeParams() > 0 )
				elemQType = fieldType->getTypeParam( 0 );
		}
		if ( elemQType != nullptr )
			elemType = getLLVMType( elemQType );

		// Create the loop counter and element variable
		llvm::Type *i64Type = llvm::Type::getInt64Ty( *mContext );
		llvm::AllocaInst *counterAlloca = mBuilder->CreateAlloca(
			i64Type, nullptr, "forin.counter" );
		mBuilder->CreateStore( llvm::ConstantInt::get( i64Type, 0 ), counterAlloca );

		llvm::AllocaInst *elemAlloca = mBuilder->CreateAlloca(
			elemType, nullptr, forInStmt->mVariableName );

		// Register the loop variable in the variable map
		if ( forInStmt->mBody != nullptr )
		{
			Block *bodyBlock = dynamic_cast<Block*>( (Statement*)forInStmt->mBody );
			if ( bodyBlock != nullptr && bodyBlock->mScope != nullptr )
			{
				Symbol *iterSym = bodyBlock->mScope->findSymbol( forInStmt->mVariableName );
				if ( auto *iterVar = dynamic_cast<VariableDefinition*>( iterSym ) )
				{
					mVariableMap[iterVar] = elemAlloca;

					// Update the loop variable's AST type to the actual element type
					// so that isStringType/isArrayType can resolve it correctly
					// (elemQType covers variable AND field iterables, instance-mapped)
					if ( elemQType != nullptr )
						iterVar->setType( elemQType );
				}
			}
		}

		llvm::BasicBlock *condBB = llvm::BasicBlock::Create( *mContext, "forin.cond", func );
		llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create( *mContext, "forin.body", func );
		llvm::BasicBlock *iterBB = llvm::BasicBlock::Create( *mContext, "forin.iter", func );
		llvm::BasicBlock *afterBB = llvm::BasicBlock::Create( *mContext, "forin.end", func );

		mBuilder->CreateBr( condBB );

		// Condition: counter < length
		mBuilder->SetInsertPoint( condBB );
		llvm::Value *curCounter = mBuilder->CreateLoad( i64Type, counterAlloca, "cur" );
		llvm::Value *cond = mBuilder->CreateICmpSLT( curCounter, lenVal, "forin.cmp" );
		mBuilder->CreateCondBr( cond, bodyBB, afterBB );

		// Body: get element from array
		mBuilder->SetInsertPoint( bodyBB );

		// Push loop targets
		mLoopStack.push_back( { iterBB, afterBB } );

		// Load current element: __blang_array_get(arr, counter, &elem)
		llvm::Value *curIdx = mBuilder->CreateLoad( i64Type, counterAlloca, "idx" );
		llvm::Function *getFn = getOrDeclareArrayGet();
		mBuilder->CreateCall( getFn, { arrVal, curIdx, elemAlloca } );

		bool savedInsideLoop = mInsideLoop;
		mInsideLoop = true;
		if ( forInStmt->mBody != nullptr )
			genStatement( forInStmt->mBody );
		mInsideLoop = savedInsideLoop;

		if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
			mBuilder->CreateBr( iterBB );

		mLoopStack.pop_back();

		// Iteration: counter = counter + 1
		mBuilder->SetInsertPoint( iterBB );
		llvm::Value *nextVal = mBuilder->CreateAdd(
			mBuilder->CreateLoad( i64Type, counterAlloca, "i" ),
			llvm::ConstantInt::get( i64Type, 1 ), "inc" );
		mBuilder->CreateStore( nextVal, counterAlloca );
		mBuilder->CreateBr( condBB );

		// After
		mBuilder->SetInsertPoint( afterBB );
		return;
	}

	// Fallback: generate collection expression and execute body once
	if ( forInStmt->mIterableExpression != nullptr )
		genExpression( forInStmt->mIterableExpression );
	if ( forInStmt->mBody != nullptr )
		genStatement( forInStmt->mBody );
}
