#include "CodeGen.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"

#include <iostream>

using namespace QLang;
using namespace std;

llvm::Value *CodeGen::genEnumConstruct( EnumConstructExpression *expr )
{
	EnumDefinition *enumDef = expr->mEnumDef;
	int variantIdx = expr->mVariantIndex;

	if ( !enumHasPayload( enumDef ) )
	{
		// Plain enum without payload — just return the tag as i32
		return llvm::ConstantInt::get(
			llvm::Type::getInt32Ty( *mContext ), variantIdx );
	}

	// Tagged union enum: { i32 tag, [N x i8] payload }
	llvm::StructType *enumType = getOrCreateEnumType( enumDef );

	// Alloca the enum struct
	llvm::AllocaInst *alloca = mBuilder->CreateAlloca( enumType, nullptr, "enum.tmp" );

	// Store the tag
	llvm::Value *tagPtr = mBuilder->CreateStructGEP( enumType, alloca, 0, "enum.tag.ptr" );
	mBuilder->CreateStore(
		llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), variantIdx ),
		tagPtr );

	// Store the payload (if this variant has associated types)
	auto &variant = enumDef->mVariants[variantIdx];
	if ( !variant.mAssociatedTypes.empty() && !expr->mArgs.empty() )
	{
		llvm::Value *payloadPtr = mBuilder->CreateStructGEP(
			enumType, alloca, 1, "enum.payload.ptr" );

		// For each associated type, store the argument into the payload area
		uint64_t offset = 0;
		llvm::DataLayout dl( mModule.get() );

		for ( size_t i = 0; i < variant.mAssociatedTypes.size() && i < expr->mArgs.size(); i++ )
		{
			llvm::Value *argVal = genExpression( expr->mArgs[i] );
			if ( argVal == nullptr )
				continue;

			llvm::Type *argType = argVal->getType();

			// Boxed enum payload: a payload whose declared type names an enum is
			// stored as a POINTER to a heap-allocated copy, so recursive enums
			// (enum Expr { add(Expr, Expr), ... }) have finite layout. The box
			// carries a generated dtor that releases the boxed value's own
			// refcounted payloads when the box's refcount hits zero. A GENERIC
			// slot (built-in Option<T>/Result<T,E>) declares the erased param
			// name, so it is boxed whenever the VALUE being stored is an enum
			// struct — that is how `Result.ok(tree)` fits an arbitrarily large
			// enum into the pointer-sized erased slot.
			{
				EnumDefinition *childEd = nullptr;
				string assocName = variant.mAssociatedTypes[i]->getName();
				auto boxIt = mEnumDefMap.find( assocName );
				if ( boxIt != mEnumDefMap.end() )
					childEd = boxIt->second;
				if ( childEd == nullptr )
				{
					if ( auto *argSt = llvm::dyn_cast<llvm::StructType>( argType ) )
					{
						if ( argSt->hasName() &&
							 argSt->getName().str().substr( 0, 5 ) == "enum." )
						{
							auto it2 = mEnumDefMap.find(
								argSt->getName().str().substr( 5 ) );
							if ( it2 != mEnumDefMap.end() )
								childEd = it2->second;
						}
					}
				}
				if ( childEd != nullptr && argType->isStructTy() )
				{
					llvm::StructType *childTy = getOrCreateEnumType( childEd );
					llvm::Value *sizeVal = llvm::ConstantInt::get(
						llvm::Type::getInt64Ty( *mContext ),
						dl.getTypeAllocSize( childTy ) );
					llvm::Function *boxDtor = getOrGenEnumBoxDtor( childEd );
					llvm::Value *box;
					if ( boxDtor != nullptr )
						box = mBuilder->CreateCall( getOrDeclareRcAllocDtor(),
							{ sizeVal, boxDtor }, "enum.box" );
					else
						box = mBuilder->CreateCall( getOrDeclareRcAlloc(),
							{ sizeVal }, "enum.box" );
					mBuilder->CreateStore( argVal, box );

					// Copying from an existing owner (variable/field/index
					// source) shares the inner refcounted payloads with the
					// source, whose own release still runs — give the box its
					// own references. A fresh temp (nested construct, call
					// result) transfers ownership outright.
					Expression *srcExpr = (Expression *)expr->mArgs[i];
					bool srcOwner =
						dynamic_cast<VariableExpression*>( srcExpr ) != nullptr ||
						dynamic_cast<FieldAccessExpression*>( srcExpr ) != nullptr ||
						dynamic_cast<IndexExpression*>( srcExpr ) != nullptr;
					if ( srcOwner )
					{
						llvm::Function *pr = getOrGenEnumPayloadRetain( childEd );
						if ( pr != nullptr )
							mBuilder->CreateCall( pr, { box } );
					}

					llvm::Value *boxOffVal = llvm::ConstantInt::get(
						llvm::Type::getInt64Ty( *mContext ), offset );
					llvm::Type *payloadArrTy = enumType->getElementType( 1 );
					llvm::Value *boxSlot = mBuilder->CreateGEP(
						payloadArrTy, payloadPtr,
						{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ),
						  boxOffVal },
						"enum.payload.box" );
					mBuilder->CreateStore( box, boxSlot );
					offset += 8;
					continue;
				}
			}

			// GEP into the payload byte array at the current offset
			llvm::Value *offsetVal = llvm::ConstantInt::get(
				llvm::Type::getInt64Ty( *mContext ), offset );
			llvm::Type *payloadArrType = enumType->getElementType( 1 );
			llvm::Value *bytePtr = mBuilder->CreateGEP(
				payloadArrType, payloadPtr,
				{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ), offsetVal },
				"enum.payload.byte" );

			// Store the value through the byte pointer
			mBuilder->CreateStore( argVal, bytePtr );

			// If storing a refcounted payload (string/Array/struct) into the enum,
			// the enum takes ownership — untrack the temporary so scope cleanup
			// does not release it and leave the enum holding a dangling pointer
			// (a use-after-free at the match/recv site). Key on the ARGUMENT's
			// actual type: for a generic enum (built-in Option<T>/Result<T,E>) the
			// declared associated type is a generic param (T/E), not
			// "string"/"Array", so keying on the declared type would miss it.
			if ( argType->isPointerTy() )
			{
				// The argument's source decides how the enum gets its reference,
				// mirroring struct-literal field stores:
				//   - a temporary (call result, literal): transfer ownership by
				//     untracking it, so scope cleanup leaves it to the enum;
				//   - an existing owner (variable / field access): RETAIN, because
				//     the source's own scope release still runs — without the
				//     retain, e.g. `return Result.ok(a)` for a local Array leaves
				//     the enum pointing at freed memory (use-after-free at the
				//     match/`?` unwrap site).
				Expression *argExpr = (Expression *)expr->mArgs[i];
				bool srcIsExistingOwner =
					dynamic_cast<VariableExpression*>( argExpr ) != nullptr ||
					dynamic_cast<FieldAccessExpression*>( argExpr ) != nullptr;

				if ( isStringType( expr->mArgs[i] ) )
				{
					if ( srcIsExistingOwner )
						mBuilder->CreateCall( getOrDeclareStringRetain(), { argVal } );
					else
						untrackTempString( argVal );
				}
				else if ( isArrayType( expr->mArgs[i] ) )
				{
					if ( srcIsExistingOwner )
						mBuilder->CreateCall( getOrDeclareArrayRetain(), { argVal } );
					else
						untrackTempArray( argVal );
				}
				else
				{
					std::string payloadTypeName;
					if ( Type *rt = argExpr->getResolvedType() )
						payloadTypeName = rt->getName();
					if ( ( payloadTypeName.empty() || payloadTypeName == "var" ) &&
						 i < variant.mAssociatedTypes.size() )
						payloadTypeName = variant.mAssociatedTypes[i]->getName();
					if ( isUserStructType( payloadTypeName ) )
					{
						if ( srcIsExistingOwner )
							mBuilder->CreateCall( getOrDeclareRcRetain(), { argVal } );
						else
							untrackTempStruct( argVal );
					}
				}
			}

			uint64_t typeSize = dl.getTypeAllocSize( argType );
			if ( typeSize == 0 ) typeSize = 4; // fallback
			offset += typeSize;
		}
	}

	// Load and return the enum value
	return mBuilder->CreateLoad( enumType, alloca, "enum.val" );
}

void CodeGen::trackEnumArgTemp( Expression *argExpr, llvm::Value *argVal,
	Type *declParamType )
{
	if ( argExpr == nullptr || argVal == nullptr )
		return;
	auto *st = llvm::dyn_cast<llvm::StructType>( argVal->getType() );
	if ( st == nullptr || !st->hasName() )
		return;
	string stName = st->getName().str();
	if ( stName.substr( 0, 5 ) != "enum." )
		return;

	// Only rvalue temps own an unreleased payload: a variable/field/index
	// source's payload is already released where the variable was declared.
	if ( dynamic_cast<VariableExpression*>( argExpr ) != nullptr ||
		 dynamic_cast<FieldAccessExpression*>( argExpr ) != nullptr ||
		 dynamic_cast<IndexExpression*>( argExpr ) != nullptr )
		return;

	auto it = mEnumDefMap.find( stName.substr( 5 ) );
	if ( it == mEnumDefMap.end() )
		return;
	EnumDefinition *ed = it->second;

	// Concrete instantiation: the callee's declared parameter type carries the
	// type arguments (Option<string>); fall back to the argument's
	// Sema-resolved type (a call result's function return type).
	Type *concrete = declParamType;
	if ( concrete == nullptr || concrete->getNumTypeParams() == 0 )
	{
		Type *rt = argExpr->getResolvedType();
		if ( rt != nullptr )
			concrete = rt;
	}

	if ( !enumHasRefcountedPayload( ed, concrete ) || mEnumScopeStack.empty() )
		return;

	llvm::AllocaInst *tmp = mBuilder->CreateAlloca( st, nullptr, "enumarg.tmp" );
	mBuilder->CreateStore( argVal, tmp );
	mEnumScopeStack.back().push_back( { tmp, ed, concrete } );
}

llvm::Function *CodeGen::getOrGenEnumBoxDtor( EnumDefinition *enumDef )
{
	if ( enumDef == nullptr || !enumHasRefcountedPayload( enumDef, nullptr ) )
		return nullptr;

	string dtorName = "__enum_" + enumDef->getName() + "_box_dtor";
	llvm::Function *fn = mModule->getFunction( dtorName );
	if ( fn != nullptr && !fn->empty() )
		return fn;

	llvm::Type *ptrType = llvm::PointerType::get( *mContext, 0 );
	if ( fn == nullptr )
	{
		llvm::FunctionType *ft = llvm::FunctionType::get(
			llvm::Type::getVoidTy( *mContext ), { ptrType }, false );
		fn = llvm::Function::Create(
			ft, llvm::Function::InternalLinkage, dtorName, mModule.get() );
	}

	// Save/restore builder state while generating the dtor body
	llvm::BasicBlock *savedBB = mBuilder->GetInsertBlock();
	llvm::BasicBlock::iterator savedPt;
	bool hadInsertPoint = ( savedBB != nullptr );
	if ( hadInsertPoint )
		savedPt = mBuilder->GetInsertPoint();

	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create( *mContext, "entry", fn );
	mBuilder->SetInsertPoint( entryBB );
	emitEnumPayloadReleaseFromPtr( fn->getArg( 0 ), enumDef, nullptr );
	mBuilder->CreateRetVoid();

	if ( hadInsertPoint )
		mBuilder->SetInsertPoint( savedBB, savedPt );

	return fn;
}

llvm::Function *CodeGen::getOrGenEnumPayloadRetain( EnumDefinition *enumDef )
{
	if ( enumDef == nullptr || !enumHasRefcountedPayload( enumDef, nullptr ) )
		return nullptr;

	string fnName = "__enum_" + enumDef->getName() + "_payload_retain";
	llvm::Function *fn = mModule->getFunction( fnName );
	if ( fn != nullptr && !fn->empty() )
		return fn;

	llvm::Type *ptrType = llvm::PointerType::get( *mContext, 0 );
	if ( fn == nullptr )
	{
		llvm::FunctionType *ft = llvm::FunctionType::get(
			llvm::Type::getVoidTy( *mContext ), { ptrType }, false );
		fn = llvm::Function::Create(
			ft, llvm::Function::InternalLinkage, fnName, mModule.get() );
	}

	llvm::BasicBlock *savedBB = mBuilder->GetInsertBlock();
	llvm::BasicBlock::iterator savedPt;
	bool hadInsertPoint = ( savedBB != nullptr );
	if ( hadInsertPoint )
		savedPt = mBuilder->GetInsertPoint();

	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create( *mContext, "entry", fn );
	mBuilder->SetInsertPoint( entryBB );

	llvm::StructType *enumType = getOrCreateEnumType( enumDef );
	llvm::Type *payloadArrType = enumType->getElementType( 1 );
	llvm::DataLayout dl( mModule.get() );
	llvm::Value *selfPtr = fn->getArg( 0 );

	llvm::Value *tagPtr = mBuilder->CreateStructGEP( enumType, selfPtr, 0, "eret.tag.ptr" );
	llvm::Value *tag = mBuilder->CreateLoad(
		llvm::Type::getInt32Ty( *mContext ), tagPtr, "eret.tag" );

	llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create( *mContext, "eret.done", fn );
	llvm::SwitchInst *sw = mBuilder->CreateSwitch( tag, mergeBB, enumDef->mVariants.size() );

	for ( size_t vi = 0; vi < enumDef->mVariants.size(); vi++ )
	{
		auto &variant = enumDef->mVariants[vi];
		bool hasRef = false;
		for ( auto &at : variant.mAssociatedTypes )
		{
			string atn = resolveVariantPayloadType( (Type *)at, enumDef, nullptr )->getName();
			if ( atn == "string" || atn == "Array" || atn == "Buffer" ||
				 isUserStructType( atn ) || mEnumDefMap.count( atn ) != 0 )
			{
				hasRef = true;
				break;
			}
		}
		if ( !hasRef )
			continue;

		llvm::BasicBlock *variantBB = llvm::BasicBlock::Create(
			*mContext, "eret." + variant.mName, fn );
		sw->addCase(
			llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), vi ),
			variantBB );
		mBuilder->SetInsertPoint( variantBB );

		llvm::Value *payloadPtr = mBuilder->CreateStructGEP(
			enumType, selfPtr, 1, "eret.payload" );

		// Same offset walk as the release path.
		uint64_t off = 0;
		for ( auto &at : variant.mAssociatedTypes )
		{
			Type *resolved = resolveVariantPayloadType( (Type *)at, enumDef, nullptr );
			string atn = resolved->getName();
			bool isGenericSlot = false;
			for ( auto &gp : enumDef->mGenericParams )
			{
				if ( gp.mName == ( (Type *)at )->getName() )
				{
					isGenericSlot = true;
					break;
				}
			}
			uint64_t slot;
			if ( isGenericSlot || mEnumDefMap.count( atn ) != 0 )
				slot = 8;
			else
			{
				slot = dl.getTypeAllocSize( getLLVMType( resolved ) );
				if ( slot == 0 ) slot = 4;
			}

			llvm::Value *bytePtr = mBuilder->CreateGEP(
				payloadArrType, payloadPtr,
				{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ),
				  llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), (int64_t)off ) },
				"eret.payload.byte" );

			llvm::Function *retainFn = nullptr;
			if ( atn == "string" )
				retainFn = getOrDeclareStringRetain();
			else if ( atn == "Array" )
				retainFn = getOrDeclareArrayRetain();
			else if ( atn == "Buffer" )
				retainFn = getOrDeclareBufferRetain();
			else if ( isUserStructType( atn ) || mEnumDefMap.count( atn ) != 0 )
				retainFn = getOrDeclareRcRetain();
			if ( retainFn != nullptr )
			{
				llvm::Value *val = mBuilder->CreateLoad(
					llvm::PointerType::get( *mContext, 0 ), bytePtr, "eret.val" );
				mBuilder->CreateCall( retainFn, { val } );
			}
			off += slot;
		}

		mBuilder->CreateBr( mergeBB );
	}

	mBuilder->SetInsertPoint( mergeBB );
	mBuilder->CreateRetVoid();

	if ( hadInsertPoint )
		mBuilder->SetInsertPoint( savedBB, savedPt );

	return fn;
}

// ---- Match codegen (Task 53) ----

llvm::Value *CodeGen::genMatchExpression( MatchExpression *expr )
{
	// Snapshot the temp-string stack so we can release ONLY the temporaries
	// created while evaluating the subject (e.g. string-literal args to a
	// subject call like `match env.get("X")`), not any pre-existing live temps
	// from an enclosing expression (which the arms may still need). See the
	// flush below.
	size_t tempMarkBeforeSubject = mTempStrings.size();
	llvm::Value *subject = genExpression( expr->mSubject );
	if ( subject == nullptr )
		return nullptr;

	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();
	llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create( *mContext, "matchend", func );

	// Determine if this is a tagged union enum match
	bool isEnumStruct = false;
	EnumDefinition *matchedEnum = nullptr;
	llvm::StructType *enumStructType = nullptr;
	llvm::Value *tagVal = nullptr;
	llvm::AllocaInst *subjectAlloca = nullptr;

	if ( auto *st = llvm::dyn_cast<llvm::StructType>( subject->getType() ) )
	{
		// Check if this struct type corresponds to a known enum
		string stName = st->hasName() ? st->getName().str() : "";
		// Enum types are named "enum.EnumName"
		if ( stName.substr( 0, 5 ) == "enum." )
		{
			string enumName = stName.substr( 5 );
			auto enumIt = mEnumDefMap.find( enumName );
			if ( enumIt != mEnumDefMap.end() )
			{
				isEnumStruct = true;
				matchedEnum = enumIt->second;
				enumStructType = st;

				// Store the subject in an alloca so we can GEP into it
				subjectAlloca = mBuilder->CreateAlloca( st, nullptr, "match.enum" );
				mBuilder->CreateStore( subject, subjectAlloca );

				// If the subject is a TEMPORARY enum value (an rvalue such as a
				// function/method call result — e.g. `match divide(a,b)` or
				// `match ch.recv()`), nothing else owns or scope-releases it. When
				// its variant carries a refcounted payload (string/Array/Buffer/
				// struct — e.g. Result<int,string>.err), register this alloca on the
				// enum scope stack so the payload is released at scope/return exit
				// via emitEnumPayloadRelease, exactly as an enum *variable* is.
				// Without this the payload leaks. A variable/field subject is NOT
				// registered here — it is already tracked at its own declaration, so
				// registering again would double-free.
				bool subjectIsTemporary =
					dynamic_cast<VariableExpression*>( (Expression*)expr->mSubject ) == nullptr &&
					dynamic_cast<FieldAccessExpression*>( (Expression*)expr->mSubject ) == nullptr;
				if ( subjectIsTemporary && !mEnumScopeStack.empty() )
				{
					// Concrete instantiation of the subject (e.g. Result<int,string>)
					// so a generic-param payload (built-in Option/Result) resolves to
					// its concrete refcounted type for release.
					Type *subjConcrete = ( (Expression*)expr->mSubject )->getResolvedType();
					if ( enumHasRefcountedPayload( matchedEnum, subjConcrete ) )
						mEnumScopeStack.back().push_back(
							{ subjectAlloca, matchedEnum, subjConcrete } );
				}

				// Extract the tag: GEP to field 0, load i32
				llvm::Value *tagPtr = mBuilder->CreateStructGEP(
					st, subjectAlloca, 0, "match.tag.ptr" );
				tagVal = mBuilder->CreateLoad(
					llvm::Type::getInt32Ty( *mContext ), tagPtr, "match.tag" );
			}
		}
	}

	// The subject expression is fully evaluated and (for an enum) copied into
	// subjectAlloca, so any temporary strings created while evaluating it — e.g.
	// string-literal arguments to a subject call like `match env.get("X")` —
	// must be released HERE, in this single pre-branch block, so they are freed
	// on every arm path. Deferring to the arms let the first arm's
	// releaseTempStrings() consume them, leaking on the other arms / fall-through
	// (surfaced by codegen_env.b: the arg literal leaked on the none path).
	//
	// SURGICAL: release only the temps created DURING subject evaluation
	// (indices >= tempMarkBeforeSubject), never pre-existing temps from an
	// enclosing expression (e.g. a sibling string-literal arg in `foo("lit",
	// match ...)`), which the arms/rest of the statement may still use. Only for
	// the enum case, where the subject value is already copied into
	// subjectAlloca; the payload string (some(v)) is untracked at construction
	// and released via mEnumScopeStack, so it is not among these temps.
	if ( isEnumStruct &&
	     mBuilder->GetInsertBlock()->getTerminator() == nullptr )
	{
		for ( size_t k = tempMarkBeforeSubject; k < mTempStrings.size(); k++ )
			mBuilder->CreateCall( getOrDeclareStringRelease(), { mTempStrings[k] } );
		if ( mTempStrings.size() > tempMarkBeforeSubject )
			mTempStrings.resize( tempMarkBeforeSubject );
	}

	// Find the default (wildcard) arm, or create a default that falls through
	int wildcardIdx = -1;
	for ( size_t i = 0; i < expr->mArms.size(); i++ )
	{
		if ( expr->mArms[i].mIsWildcard )
		{
			wildcardIdx = static_cast<int>( i );
			break;
		}
	}

	// Create basic blocks for each arm
	std::vector<llvm::BasicBlock*> armBBs;
	for ( size_t i = 0; i < expr->mArms.size(); i++ )
	{
		string name = "match.arm." + to_string( i );
		armBBs.push_back( llvm::BasicBlock::Create( *mContext, name, func ) );
	}

	// Default block: either the wildcard arm or the merge block
	llvm::BasicBlock *defaultBB = ( wildcardIdx >= 0 ) ? armBBs[wildcardIdx] : mergeBB;

	// Determine the switch value: either the subject itself (i32) or the extracted tag
	llvm::Value *switchVal = isEnumStruct ? tagVal : subject;

	// Build switch for integer/tag subjects
	if ( switchVal != nullptr && switchVal->getType()->isIntegerTy() )
	{
		// Count actual cases for the switch
		int numCases = 0;
		for ( size_t i = 0; i < expr->mArms.size(); i++ )
		{
			if ( !expr->mArms[i].mIsWildcard )
				numCases++;
		}

		llvm::SwitchInst *switchInst = mBuilder->CreateSwitch(
			switchVal, defaultBB, numCases );

		for ( size_t i = 0; i < expr->mArms.size(); i++ )
		{
			if ( expr->mArms[i].mIsWildcard )
				continue;

			const string &pattern = expr->mArms[i].mPattern;
			int64_t patternVal = 0;

			// Try to parse as integer
			bool isNumeric = !pattern.empty() &&
				( isdigit( pattern[0] ) || pattern[0] == '-' );
			if ( isNumeric )
			{
				patternVal = stoll( pattern );
			}
			else
			{
				// Named pattern (e.g., "ok", "err", "none") — use variant index
				bool found = false;

				// If we know the specific enum, search only in it
				if ( matchedEnum != nullptr )
				{
					for ( size_t v = 0; v < matchedEnum->mVariants.size(); v++ )
					{
						if ( matchedEnum->mVariants[v].mName == pattern )
						{
							patternVal = static_cast<int64_t>( v );
							found = true;
							break;
						}
					}
				}

				if ( !found )
				{
					// Search all registered enums
					for ( auto &ep : mEnumDefMap )
					{
						for ( size_t v = 0; v < ep.second->mVariants.size(); v++ )
						{
							if ( ep.second->mVariants[v].mName == pattern )
							{
								patternVal = static_cast<int64_t>( v );
								found = true;
								break;
							}
						}
						if ( found )
							break;
					}
				}
				if ( !found )
				{
					// Use arm index as fallback
					patternVal = static_cast<int64_t>( i );
				}
			}

			switchInst->addCase(
				llvm::ConstantInt::get(
					llvm::cast<llvm::IntegerType>( switchVal->getType() ),
					patternVal ),
				armBBs[i] );
		}
	}
	else if ( subject->getType()->isPointerTy() &&
			  isStringType( (Expression *)expr->mSubject ) )
	{
		// String subject: chain __blang_string_equals_cstr checks against each
		// string-literal pattern, in arm order; no match falls to the default.
		// (Previously ANY non-integer subject silently branched to the default
		// arm — the design spec's own `match command { "start" {...} ... }`
		// example always took the wildcard.)
		std::vector<size_t> strArms;
		for ( size_t i = 0; i < expr->mArms.size(); i++ )
		{
			if ( !expr->mArms[i].mIsWildcard && expr->mArms[i].mPatternIsString )
				strArms.push_back( i );
		}
		llvm::Function *eqFn = getOrDeclareStringEqualsCstr();
		for ( size_t k = 0; k < strArms.size(); k++ )
		{
			size_t i = strArms[k];
			const string &pat = expr->mArms[i].mPattern;
			llvm::Value *litPtr = mBuilder->CreateGlobalStringPtr( pat, "match.pat" );
			llvm::Value *litLen = llvm::ConstantInt::get(
				llvm::Type::getInt64Ty( *mContext ), (int64_t)pat.size() );
			llvm::Value *isEq = mBuilder->CreateCall(
				eqFn, { subject, litPtr, litLen }, "match.streq" );
			llvm::BasicBlock *noMatchBB = ( k + 1 < strArms.size() )
				? llvm::BasicBlock::Create( *mContext, "match.strnext", func )
				: defaultBB;
			mBuilder->CreateCondBr( isEq, armBBs[i], noMatchBB );
			if ( k + 1 < strArms.size() )
				mBuilder->SetInsertPoint( noMatchBB );
		}
		if ( strArms.empty() )
			mBuilder->CreateBr( defaultBB );
	}
	else
	{
		// Non-integer, non-string subject — just branch to default
		mBuilder->CreateBr( defaultBB );
	}

	// Expression-mode result slot. The alloca (and its zero-init) go into the
	// function's ENTRY block so they dominate every arm store and the merge
	// load; the concrete LLVM type is taken from the first arm value produced.
	llvm::AllocaInst *resultAlloca = nullptr;
	llvm::Type *resultTy = nullptr;
	bool resIsString = false, resIsArray = false, resIsStruct = false;
	string resKindName;
	if ( expr->mExprMode && expr->getResolvedType() != nullptr )
		resKindName = resolvedTypeName( expr->getResolvedType() );

	// Generate code for each arm body
	for ( size_t i = 0; i < expr->mArms.size(); i++ )
	{
		mBuilder->SetInsertPoint( armBBs[i] );

		// The binding lives in the arm's block scope (statement form) or in the
		// arm's own scope (expression form).
		Scope *armScope = ( expr->mArms[i].mBody != nullptr )
			? (Scope *)expr->mArms[i].mBody->mScope
			: (Scope *)expr->mArms[i].mScope;

		// If the arm has bindings, extract the payload value(s) from the enum
		// struct. Payloads are laid out sequentially in the payload byte array
		// (construction advances by DataLayout alloc size per argument), so
		// binding j loads at the sum of the preceding bindings' sizes.
		if ( !expr->mArms[i].mBindingNames.empty() )
		{
			if ( isEnumStruct && matchedEnum != nullptr && subjectAlloca != nullptr )
			{
				// Find the variant for this arm's pattern
				int variantIdx = -1;
				for ( size_t v = 0; v < matchedEnum->mVariants.size(); v++ )
				{
					if ( matchedEnum->mVariants[v].mName == expr->mArms[i].mPattern )
					{
						variantIdx = static_cast<int>( v );
						break;
					}
				}

				// GEP to the payload area (field 1)
				llvm::Value *payloadPtr = mBuilder->CreateStructGEP(
					enumStructType, subjectAlloca, 1, "match.payload.ptr" );
				llvm::Type *payloadArrType = enumStructType->getElementType( 1 );
				llvm::DataLayout dl( mModule.get() );
				uint64_t payloadOffset = 0;

				for ( size_t bi = 0; bi < expr->mArms[i].mBindingNames.size(); bi++ )
				{
					const string &bname = expr->mArms[i].mBindingNames[bi];

					llvm::Type *bindType = llvm::Type::getInt32Ty( *mContext ); // default
					// Prefer the binding variable's Sema-resolved type: for a generic
					// enum (built-in Option<T>/Result<T,E>) Sema substitutes the concrete
					// type argument from the subject (e.g. err payload -> string), which
					// the raw variant associated type ("T"/"E") does not carry. Fall back
					// to the variant's declared associated type for non-generic enums.
					Type *bindQType = nullptr;
					if ( armScope != nullptr )
					{
						Symbol *bs = armScope->findSymbol( bname );
						if ( auto *bv = dynamic_cast<VariableDefinition*>( bs ) )
							bindQType = bv->getVariableType();
					}
					// Boxed enum payload: the declared associated type names an
					// enum, so the slot holds a POINTER to the child value.
					string bindQName;
					if ( bindQType != nullptr && bindQType->getName() != "var" )
						bindQName = bindQType->getName();
					else if ( variantIdx >= 0 &&
						 bi < matchedEnum->mVariants[variantIdx].mAssociatedTypes.size() )
						bindQName = matchedEnum->mVariants[variantIdx]
							.mAssociatedTypes[bi]->getName();
					bool isBoxedEnum = ( mEnumDefMap.count( bindQName ) != 0 );

					if ( isBoxedEnum )
					{
						bindType = getOrCreateEnumType( mEnumDefMap[bindQName] );
					}
					else if ( bindQType != nullptr && bindQType->getName() != "var" )
					{
						bindType = getLLVMType( bindQType );
					}
					else if ( variantIdx >= 0 &&
						 bi < matchedEnum->mVariants[variantIdx].mAssociatedTypes.size() )
					{
						bindType = getLLVMType(
							matchedEnum->mVariants[variantIdx].mAssociatedTypes[bi] );
					}

					// GEP to this payload's byte offset and load as the expected type
					llvm::Value *bytePtr = mBuilder->CreateGEP(
						payloadArrType, payloadPtr,
						{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ),
						  llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ),
							(int64_t)payloadOffset ) },
						"match.payload.byte" );

					llvm::Value *payloadVal;
					if ( isBoxedEnum )
					{
						// Load the box pointer, then copy the boxed enum value
						// out — the binding is a by-value BORROW of the box's
						// contents (the box stays owned by the subject).
						llvm::Value *boxPtr = mBuilder->CreateLoad(
							llvm::PointerType::get( *mContext, 0 ), bytePtr,
							"match.payload.boxptr" );
						payloadVal = mBuilder->CreateLoad(
							bindType, boxPtr, "match.payload.boxed" );
					}
					else
					{
						payloadVal = mBuilder->CreateLoad(
							bindType, bytePtr, "match.payload.val" );
					}

					// Create alloca for the binding variable
					llvm::AllocaInst *bindAlloca = mBuilder->CreateAlloca(
						bindType, nullptr, bname );
					mBuilder->CreateStore( payloadVal, bindAlloca );

					// Register in variable map
					if ( armScope != nullptr )
					{
						Symbol *bindSym = armScope->findSymbol( bname );
						if ( auto *bindVar = dynamic_cast<VariableDefinition*>( bindSym ) )
							mVariableMap[bindVar] = bindAlloca;
					}

					// Advance by this payload's size — must mirror the
					// construction walk (boxed enum slots are pointer-sized).
					if ( isBoxedEnum )
						payloadOffset += 8;
					else
					{
						uint64_t sz = dl.getTypeAllocSize( bindType );
						if ( sz == 0 ) sz = 4;
						payloadOffset += sz;
					}
				}
			}
			else
			{
				// Non-enum binding: store the subject value directly
				const string &bname = expr->mArms[i].mBindingNames[0];
				llvm::Type *bindType = subject->getType();
				llvm::AllocaInst *bindAlloca = mBuilder->CreateAlloca(
					bindType, nullptr, bname );
				mBuilder->CreateStore( subject, bindAlloca );

				// Register in variable map
				if ( armScope != nullptr )
				{
					Symbol *bindSym = armScope->findSymbol( bname );
					if ( auto *bindVar = dynamic_cast<VariableDefinition*>( bindSym ) )
						mVariableMap[bindVar] = bindAlloca;
				}
			}
		}

		if ( expr->mExprMode )
		{
			// Expression-form arm: evaluate the single expression, hand the
			// result slot an OWNED reference (mirroring the return-statement
			// policy: retain borrowed sources — variable/field/index reads —
			// and take over ownership of fresh temps by untracking them), then
			// store and fall through to the merge.
			size_t sMark = mTempStrings.size();
			size_t aMark = mTempArrays.size();
			size_t tMark = mTempStructs.size();

			llvm::Value *armVal = ( expr->mArms[i].mValue != nullptr )
				? genExpression( expr->mArms[i].mValue ) : nullptr;
			if ( armVal != nullptr )
			{
				if ( resultTy == nullptr )
				{
					resultTy = armVal->getType();
					llvm::IRBuilder<> eb( &func->getEntryBlock(),
						func->getEntryBlock().begin() );
					resultAlloca = eb.CreateAlloca( resultTy, nullptr, "match.result" );
					eb.CreateStore( llvm::Constant::getNullValue( resultTy ), resultAlloca );
				}

				Expression *ve = (Expression *)expr->mArms[i].mValue;
				bool borrowed =
					dynamic_cast<VariableExpression*>( ve ) != nullptr ||
					dynamic_cast<FieldAccessExpression*>( ve ) != nullptr ||
					dynamic_cast<IndexExpression*>( ve ) != nullptr;
				if ( resKindName == "string" || isStringType( ve ) )
				{
					resIsString = true;
					if ( borrowed )
						mBuilder->CreateCall( getOrDeclareStringRetain(), { armVal } );
					else
						untrackTempString( armVal );
				}
				else if ( resKindName == "Array" || isArrayType( ve ) )
				{
					resIsArray = true;
					if ( borrowed )
						mBuilder->CreateCall( getOrDeclareArrayRetain(), { armVal } );
					else
						untrackTempArray( armVal );
				}
				else if ( isUserStructType( resKindName ) )
				{
					resIsStruct = true;
					if ( borrowed )
						mBuilder->CreateCall( getOrDeclareRcRetain(), { armVal } );
					else
						untrackTempStruct( armVal );
				}

				if ( armVal->getType() != resultTy &&
					 armVal->getType()->isIntegerTy() && resultTy->isIntegerTy() )
					armVal = mBuilder->CreateIntCast( armVal, resultTy, true, "match.arm.cast" );
				if ( armVal->getType() == resultTy )
					mBuilder->CreateStore( armVal, resultAlloca );
			}

			// Release temps created while evaluating THIS arm, inside this
			// arm's block — deferring to the statement end would emit release
			// calls whose operands do not dominate them (defined only in this
			// arm's basic block).
			if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
			{
				for ( size_t k = sMark; k < mTempStrings.size(); k++ )
					mBuilder->CreateCall( getOrDeclareStringRelease(), { mTempStrings[k] } );
				for ( size_t k = aMark; k < mTempArrays.size(); k++ )
					mBuilder->CreateCall( getOrDeclareArrayRelease(), { mTempArrays[k] } );
				for ( size_t k = tMark; k < mTempStructs.size(); k++ )
					mBuilder->CreateCall( getOrDeclareRcRelease(), { mTempStructs[k] } );
			}
			if ( mTempStrings.size() > sMark ) mTempStrings.resize( sMark );
			if ( mTempArrays.size() > aMark ) mTempArrays.resize( aMark );
			if ( mTempStructs.size() > tMark ) mTempStructs.resize( tMark );
		}
		else if ( expr->mArms[i].mBody != nullptr )
			genBlock( expr->mArms[i].mBody );

		if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
			mBuilder->CreateBr( mergeBB );
	}

	// Move the merge block to the end of the function. It was created before the
	// arm blocks, so without this it sits in the middle of the block list; for
	// main() the function finalizer resets the insert point to llvmFunc->back()
	// (an arm), leaving matchend unterminated when the match is the function's
	// tail and every arm returns. Making matchend the last block lets the
	// finalizer add the implicit return to it (or following statements flow into
	// it normally when the match is not the tail).
	mergeBB->moveAfter( &func->back() );

	// Continue at merge
	mBuilder->SetInsertPoint( mergeBB );

	// Expression mode: yield the selected arm's value. The loaded value is an
	// OWNED reference (each arm retained/transferred into the slot), so hand it
	// to the enclosing statement as a tracked temporary — a binding untracks
	// and takes ownership; a discarding statement releases it at statement end.
	if ( expr->mExprMode )
	{
		if ( resultAlloca == nullptr )
			return nullptr;
		llvm::Value *resVal = mBuilder->CreateLoad( resultTy, resultAlloca, "match.value" );
		if ( resIsString )
			trackTempString( resVal );
		else if ( resIsArray )
			trackTempArray( resVal );
		else if ( resIsStruct )
			trackTempStruct( resVal );
		return resVal;
	}
	return nullptr;
}

// ---- Try operator codegen (Task 54) ----

EnumDefinition *CodeGen::resolveExpressionEnumDef( Expression *expr )
{
	// Resolve the QLang-level type name of the expression to find its EnumDefinition.
	std::string typeName;

	if ( auto *call = dynamic_cast<CallExpression*>( expr ) )
	{
		if ( call->mFunction != nullptr && call->mFunction->getReturnType() != nullptr )
			typeName = call->mFunction->getReturnType()->getName();
	}
	else if ( auto *varExpr = dynamic_cast<VariableExpression*>( expr ) )
	{
		if ( varExpr->mVariable != nullptr && varExpr->mVariable->getVariableType() != nullptr )
			typeName = varExpr->mVariable->getVariableType()->getName();
	}
	else if ( auto *methodCall = dynamic_cast<MethodCallExpression*>( expr ) )
	{
		(void)methodCall;
		// Method return types are harder to resolve statically; fall through
	}

	if ( !typeName.empty() )
	{
		auto it = mEnumDefMap.find( typeName );
		if ( it != mEnumDefMap.end() )
			return it->second;
	}

	return nullptr;
}

llvm::Value *CodeGen::genTryExpression( TryExpression *expr )
{
	// Generate the operand expression (e.g., might_fail())
	llvm::Value *result = genExpression( expr->mOperand );
	if ( result == nullptr )
		return nullptr;

	// Try to resolve the operand's enum definition
	EnumDefinition *enumDef = resolveExpressionEnumDef( expr->mOperand );

	// If we can't determine the enum type, fall back to pass-through
	if ( enumDef == nullptr || !enumHasPayload( enumDef ) )
		return result;

	// Find success variant (ok/some) and error variant (err/none)
	int successIdx = -1;
	int errorIdx = -1;
	for ( size_t i = 0; i < enumDef->mVariants.size(); i++ )
	{
		const std::string &vname = enumDef->mVariants[i].mName;
		if ( vname == "ok" || vname == "some" )
			successIdx = static_cast<int>( i );
		else if ( vname == "err" || vname == "none" )
			errorIdx = static_cast<int>( i );
	}

	// If we can't identify the variants, pass through
	if ( successIdx < 0 || errorIdx < 0 )
		return result;

	llvm::StructType *enumType = getOrCreateEnumType( enumDef );

	// Store the result in an alloca so we can GEP into it
	llvm::AllocaInst *enumAlloca = mBuilder->CreateAlloca( enumType, nullptr, "try.enum" );
	mBuilder->CreateStore( result, enumAlloca );

	// Extract the tag
	llvm::Value *tagPtr = mBuilder->CreateStructGEP( enumType, enumAlloca, 0, "try.tag.ptr" );
	llvm::Value *tagVal = mBuilder->CreateLoad(
		llvm::Type::getInt32Ty( *mContext ), tagPtr, "try.tag" );

	// Compare tag to the success index
	llvm::Value *isSuccess = mBuilder->CreateICmpEQ(
		tagVal,
		llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), successIdx ),
		"try.is_ok" );

	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();
	llvm::BasicBlock *okBB = llvm::BasicBlock::Create( *mContext, "try.ok", func );
	llvm::BasicBlock *errBB = llvm::BasicBlock::Create( *mContext, "try.err", func );
	llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create( *mContext, "try.merge", func );

	mBuilder->CreateCondBr( isSuccess, okBB, errBB );

	// ---- Error branch: propagate the error by returning early ----
	mBuilder->SetInsertPoint( errBB );

	// An early return via `?` must run the same scope cleanup a normal `return`
	// runs, or every refcounted local live at the failing `?` leaks (e.g. a token
	// Array built before the failing step). The operand's own enum is usually an
	// untracked temporary (a call result), so cleanup leaves the forwarded error
	// untouched. But if the operand is a tracked local enum VARIABLE, cleanup
	// WOULD release its payload — retain the forwarded payload first so the
	// caller's copy stays valid.
	{
		if ( dynamic_cast<VariableExpression*>( (Expression*)expr->mOperand ) != nullptr )
		{
			Type *errDeclared = enumDef->mVariants[errorIdx].mAssociatedTypes.empty()
				? nullptr : (Type *)enumDef->mVariants[errorIdx].mAssociatedTypes[0];
			Type *operandQ = ( (Expression *)expr->mOperand )->getResolvedType();
			Type *errConcrete = ( errDeclared != nullptr )
				? resolveVariantPayloadType( errDeclared, enumDef, operandQ )
				: nullptr;
			if ( errConcrete != nullptr )
			{
				std::string en = errConcrete->getName();
				llvm::Function *retainFn = nullptr;
				if ( en == "string" )      retainFn = getOrDeclareStringRetain();
				else if ( en == "Array" )  retainFn = getOrDeclareArrayRetain();
				else if ( en == "Buffer" ) retainFn = getOrDeclareBufferRetain();
				else if ( isUserStructType( en ) ) retainFn = getOrDeclareRcRetain();
				if ( retainFn != nullptr )
				{
					llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
					llvm::Type *payloadArrTy = enumType->getElementType( 1 );
					llvm::Value *pPtr = mBuilder->CreateStructGEP(
						enumType, enumAlloca, 1, "try.err.retain.payload" );
					llvm::Value *pByte = mBuilder->CreateGEP( payloadArrTy, pPtr,
						{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ),
						  llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ) },
						"try.err.retain.byte" );
					llvm::Value *pv = mBuilder->CreateLoad( ptrTy, pByte, "try.err.retain.val" );
					mBuilder->CreateCall( retainFn, { pv } );
				}
			}
		}

		releaseTempStrings();
		releaseTempArrays();
		emitScopeStackReleases();
	}

	// Determine the current function's return type at the QLang level
	// to construct a matching error enum value for early return.
	llvm::Type *funcRetType = func->getReturnType();

	if ( funcRetType->isStructTy() )
	{
		// The current function returns an enum struct — forward the whole value.
		// The operand's enum value is the error, so just return it as-is
		// (compatible if same enum, or at least same layout).
		llvm::Value *errVal = mBuilder->CreateLoad( enumType, enumAlloca, "try.err.val" );

		// If the function's return struct type differs, we need to reconstruct.
		// For now, if they're the same type, return directly.
		if ( funcRetType == enumType )
		{
			mBuilder->CreateRet( errVal );
		}
		else
		{
			// Different return enum: extract error payload and re-wrap.
			// Construct a new enum of the function's return type with the error variant.
			EnumDefinition *retEnumDef = nullptr;
			if ( mCurrentFunction != nullptr && mCurrentFunction->getReturnType() != nullptr )
			{
				auto retIt = mEnumDefMap.find( mCurrentFunction->getReturnType()->getName() );
				if ( retIt != mEnumDefMap.end() )
					retEnumDef = retIt->second;
			}

			if ( retEnumDef != nullptr )
			{
				// Find the error variant in the return enum
				int retErrIdx = -1;
				for ( size_t i = 0; i < retEnumDef->mVariants.size(); i++ )
				{
					if ( retEnumDef->mVariants[i].mName == "err" ||
						 retEnumDef->mVariants[i].mName == "none" )
					{
						retErrIdx = static_cast<int>( i );
						break;
					}
				}

				if ( retErrIdx >= 0 )
				{
					llvm::StructType *retEnumType = getOrCreateEnumType( retEnumDef );
					llvm::AllocaInst *retAlloca = mBuilder->CreateAlloca(
						retEnumType, nullptr, "try.ret.enum" );

					// Set tag to error variant
					llvm::Value *retTagPtr = mBuilder->CreateStructGEP(
						retEnumType, retAlloca, 0, "try.ret.tag" );
					mBuilder->CreateStore(
						llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), retErrIdx ),
						retTagPtr );

					// Copy payload from source error to return error
					auto &srcErrVariant = enumDef->mVariants[errorIdx];
					if ( !srcErrVariant.mAssociatedTypes.empty() )
					{
						llvm::Type *payloadArrType = enumType->getElementType( 1 );
						llvm::Value *srcPayloadPtr = mBuilder->CreateStructGEP(
							enumType, enumAlloca, 1, "try.src.payload" );
						llvm::Value *srcByte = mBuilder->CreateGEP(
							payloadArrType, srcPayloadPtr,
							{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ),
							  llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ) },
							"try.src.byte" );
						llvm::Type *errPayloadType = getLLVMType( srcErrVariant.mAssociatedTypes[0] );
						llvm::Value *errPayload = mBuilder->CreateLoad(
							errPayloadType, srcByte, "try.err.payload" );

						llvm::Type *retPayloadArrType = retEnumType->getElementType( 1 );
						llvm::Value *retPayloadPtr = mBuilder->CreateStructGEP(
							retEnumType, retAlloca, 1, "try.ret.payload" );
						llvm::Value *retByte = mBuilder->CreateGEP(
							retPayloadArrType, retPayloadPtr,
							{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ),
							  llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ) },
							"try.ret.byte" );
						mBuilder->CreateStore( errPayload, retByte );
					}

					llvm::Value *retVal = mBuilder->CreateLoad(
						retEnumType, retAlloca, "try.ret.val" );
					mBuilder->CreateRet( retVal );
				}
				else
				{
					// Can't find error variant in return type — return null
					mBuilder->CreateRet( llvm::Constant::getNullValue( funcRetType ) );
				}
			}
			else
			{
				// Return type is not a known enum — return null
				mBuilder->CreateRet( llvm::Constant::getNullValue( funcRetType ) );
			}
		}
	}
	else
	{
		// Current function returns a non-enum type — shouldn't use ? but handle gracefully
		if ( funcRetType->isVoidTy() )
			mBuilder->CreateRetVoid();
		else
			mBuilder->CreateRet( llvm::Constant::getNullValue( funcRetType ) );
	}

	// ---- Success branch: extract the unwrapped payload value ----
	mBuilder->SetInsertPoint( okBB );

	llvm::Type *successPayloadType = llvm::Type::getInt32Ty( *mContext ); // default
	EnumDefinition *boxedPayloadEd = nullptr;
	if ( !enumDef->mVariants[successIdx].mAssociatedTypes.empty() )
	{
		// Resolve the DECLARED associated type against the operand's concrete
		// instantiation: for a generic enum (Result<Array<int>, string>) the
		// declared type is the erased param "T", which getLLVMType would map to
		// the i32 default — truncating a pointer payload (e.g. an unwrapped
		// Array) to 4 bytes and producing a wild pointer at the use site.
		Type *declared = enumDef->mVariants[successIdx].mAssociatedTypes[0];
		Type *operandQType = ( (Expression *)expr->mOperand )->getResolvedType();
		Type *concretePayload = resolveVariantPayloadType(
			declared, enumDef, operandQType );
		Type *effective = ( concretePayload != nullptr ) ? concretePayload : declared;
		// A concrete enum payload is BOXED — the slot holds a pointer to the
		// child value, not the value itself.
		auto bIt = mEnumDefMap.find( effective->getName() );
		if ( bIt != mEnumDefMap.end() )
		{
			boxedPayloadEd = bIt->second;
			successPayloadType = getOrCreateEnumType( boxedPayloadEd );
		}
		else
			successPayloadType = getLLVMType( effective );
	}

	// GEP to the payload area and load the success value
	llvm::Type *payloadArrType = enumType->getElementType( 1 );
	llvm::Value *payloadPtr = mBuilder->CreateStructGEP(
		enumType, enumAlloca, 1, "try.ok.payload.ptr" );
	llvm::Value *bytePtr = mBuilder->CreateGEP(
		payloadArrType, payloadPtr,
		{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ),
		  llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ) },
		"try.ok.byte" );
	llvm::Value *unwrapped;
	if ( boxedPayloadEd != nullptr )
	{
		// Unbox: load the box pointer and copy the child enum value out. The
		// operand enum is an abandoned temporary after the unwrap, so transfer
		// the box's contents to the copy: retain the copied value's own
		// payloads (children boxes/strings), then release the box itself — its
		// dtor drops the references the box held, leaving the copy as the
		// single owner.
		llvm::Value *boxPtr = mBuilder->CreateLoad(
			llvm::PointerType::get( *mContext, 0 ), bytePtr, "try.ok.boxptr" );
		unwrapped = mBuilder->CreateLoad(
			successPayloadType, boxPtr, "try.ok.boxed" );
		llvm::Function *pr = getOrGenEnumPayloadRetain( boxedPayloadEd );
		if ( pr != nullptr )
			mBuilder->CreateCall( pr, { boxPtr } );
		mBuilder->CreateCall( getOrDeclareRcRelease(), { boxPtr } );
	}
	else
		unwrapped = mBuilder->CreateLoad(
			successPayloadType, bytePtr, "try.ok.val" );

	mBuilder->CreateBr( mergeBB );

	// ---- Merge block: the unwrapped value flows through ----
	mBuilder->SetInsertPoint( mergeBB );

	// Create a phi node for the unwrapped value (only from okBB)
	llvm::PHINode *phi = mBuilder->CreatePHI( successPayloadType, 1, "try.unwrapped" );
	phi->addIncoming( unwrapped, okBB );

	return phi;
}

// ---- Array codegen ----

llvm::Value *CodeGen::genArrayLiteral( ArrayLiteralExpression *expr )
{
	int numElements = static_cast<int>( expr->mElements.size() );

	// Generate first element to determine type
	llvm::Value *firstElem = nullptr;
	llvm::Type *elemLLVMType = llvm::Type::getInt32Ty( *mContext ); // default
	int elemSize = 4;

	if ( numElements > 0 )
	{
		firstElem = genExpression( expr->mElements[0] );
		if ( firstElem != nullptr )
		{
			// Use type hint if available (e.g., Array<byte> = [10, 20, 30])
			if ( mArrayElemTypeHint != nullptr )
			{
				elemLLVMType = mArrayElemTypeHint;
				// Cast first element to match hint type
				if ( firstElem->getType() != elemLLVMType &&
					 elemLLVMType->isIntegerTy() && firstElem->getType()->isIntegerTy() )
				{
					bool isSigned = ( mArrayElemTypeNameHint != "byte" );
					firstElem = mBuilder->CreateIntCast( firstElem, elemLLVMType, isSigned, "elem.cast" );
				}
				mArrayElemTypeHint = nullptr;
			}
			else
			{
				elemLLVMType = firstElem->getType();
			}
			llvm::DataLayout dl( mModule.get() );
			elemSize = dl.getTypeAllocSize( elemLLVMType );
		}
	}
	else if ( mArrayElemTypeHint != nullptr )
	{
		// Empty array literal with type hint from variable declaration
		// (e.g., Array<string> keys = []; needs elem_size = 8 for pointers)
		elemLLVMType = mArrayElemTypeHint;
		llvm::DataLayout dl( mModule.get() );
		elemSize = dl.getTypeAllocSize( elemLLVMType );
		mArrayElemTypeHint = nullptr;
	}

	// Create BlangArray: __blang_array_create(elem_size, capacity)
	llvm::Function *createFn = getOrDeclareArrayCreate();
	llvm::Value *elemSizeVal = llvm::ConstantInt::get(
		llvm::Type::getInt32Ty( *mContext ), elemSize );
	llvm::Value *capVal = llvm::ConstantInt::get(
		llvm::Type::getInt64Ty( *mContext ), numElements > 0 ? numElements : 8 );
	llvm::Value *arr = mBuilder->CreateCall( createFn, { elemSizeVal, capVal }, "arr" );

	// Push elements
	if ( numElements > 0 )
	{
		llvm::Function *pushFn = getOrDeclareArrayPush();

		// The array owns a reference to each refcounted element (an elem_dtor is
		// set below). __blang_array_push does not retain, so each pushed element
		// (typically a released statement-temporary like a struct literal) must
		// be retained here or it is freed while the array still holds it — a
		// dangling pointer + double-free at array release. See emitArrayElemRetain.
		const std::string litElemType = mArrayElemTypeNameHint;

		// Push first element
		llvm::AllocaInst *tmpAlloca = mBuilder->CreateAlloca( elemLLVMType, nullptr, "arr.tmp" );
		mBuilder->CreateStore( firstElem, tmpAlloca );
		mBuilder->CreateCall( pushFn, { arr, tmpAlloca } );
		emitArrayElemRetain( firstElem, litElemType );

		// Push remaining elements
		for ( int i = 1; i < numElements; i++ )
		{
			llvm::Value *elemVal = genExpression( expr->mElements[i] );
			if ( elemVal == nullptr )
				continue;
			// Cast element to match array element type
			if ( elemVal->getType() != elemLLVMType &&
				 elemLLVMType->isIntegerTy() && elemVal->getType()->isIntegerTy() )
			{
				bool isSigned = ( mArrayElemTypeNameHint != "byte" );
				elemVal = mBuilder->CreateIntCast( elemVal, elemLLVMType, isSigned, "elem.cast" );
			}
			mBuilder->CreateStore( elemVal, tmpAlloca );
			mBuilder->CreateCall( pushFn, { arr, tmpAlloca } );
			emitArrayElemRetain( elemVal, litElemType );
		}
	}

	// Set element destructor for arrays with refcounted element types
	if ( !mArrayElemTypeNameHint.empty() )
	{
		emitArrayElemDtor( arr, mArrayElemTypeNameHint );
		mArrayElemTypeNameHint.clear();
	}

	return arr;
}

llvm::Value *CodeGen::genIndexExpression( IndexExpression *expr )
{
	llvm::Value *objVal = genExpression( expr->mObject );
	if ( objVal == nullptr )
		return nullptr;

	llvm::Value *idxVal = genExpression( expr->mIndex );
	if ( idxVal == nullptr )
		return nullptr;

	// Check if this is a string index (string[i] -> char)
	if ( isStringType( expr->mObject ) )
	{
		// Extend index to i64 if needed
		if ( !idxVal->getType()->isIntegerTy( 64 ) )
			idxVal = mBuilder->CreateSExt( idxVal,
				llvm::Type::getInt64Ty( *mContext ), "idx.ext" );
		llvm::Function *charAtFn = getOrDeclareStringCharAt();
		return mBuilder->CreateCall( charAtFn, { objVal, idxVal }, "char.at" );
	}

	// Check if this is an Array index
	if ( isArrayType( expr->mObject ) )
	{
		// Extend index to i64 if needed
		if ( !idxVal->getType()->isIntegerTy( 64 ) )
			idxVal = mBuilder->CreateSExt( idxVal,
				llvm::Type::getInt64Ty( *mContext ), "idx.ext" );

		// Determine element type from the Array<T> type annotation
		llvm::Type *elemType = llvm::Type::getInt32Ty( *mContext ); // default
		if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)expr->mObject ) )
		{
			Type *varType = ve->mVariable->getVariableType();
			if ( varType != nullptr && varType->getNumTypeParams() > 0 )
			{
				Type *elemTypeParam = varType->getTypeParam( 0 );
				// Check substitution map for generic type params
				string elemTypeName = elemTypeParam->getName();
				auto subIt = mTypeSubstitution.find( elemTypeName );
				if ( subIt != mTypeSubstitution.end() )
					elemType = getLLVMType( subIt->second );
				else
					elemType = getLLVMType( elemTypeParam );
			}
		}
		else if ( auto *fa = dynamic_cast<FieldAccessExpression*>( (Expression*)expr->mObject ) )
		{
			// Field access on struct (e.g., self.keys[i])
			Type *fieldType = getFieldType( fa );
			if ( fieldType != nullptr && fieldType->getNumTypeParams() > 0 )
			{
				Type *elemTypeParam = fieldType->getTypeParam( 0 );
				string elemTypeName = elemTypeParam->getName();
				auto subIt = mTypeSubstitution.find( elemTypeName );
				if ( subIt != mTypeSubstitution.end() )
					elemType = getLLVMType( subIt->second );
				else
					elemType = getLLVMType( elemTypeParam );
			}
		}

		llvm::AllocaInst *outAlloca = mBuilder->CreateAlloca(
			elemType, nullptr, "arr.out" );
		llvm::Function *getFn = getOrDeclareArrayGet();
		mBuilder->CreateCall( getFn, { objVal, idxVal, outAlloca } );
		return mBuilder->CreateLoad( elemType, outAlloca, "arr.val" );
	}

	// Fallback: old-style stack-allocated array (for backward compat)
	if ( auto *alloca = llvm::dyn_cast<llvm::AllocaInst>( objVal ) )
	{
		llvm::Type *allocatedType = alloca->getAllocatedType();
		if ( auto *arrType = llvm::dyn_cast<llvm::ArrayType>( allocatedType ) )
		{
			llvm::Value *zero = llvm::ConstantInt::get(
				llvm::Type::getInt32Ty( *mContext ), 0 );
			llvm::Value *elemPtr = mBuilder->CreateGEP(
				arrType, alloca, { zero, idxVal }, "arr.idx" );
			return mBuilder->CreateLoad( arrType->getElementType(), elemPtr, "arr.val" );
		}
	}

	return nullptr;
}
