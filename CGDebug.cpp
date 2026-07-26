// CGDebug.cpp — DWARF debug info emission (U3, epic 001-toolchain-and-stdlib).
//
// All helpers are no-ops when mDebugInfo is false, so a non-`-g` build produces
// byte-identical IR to pre-U3. Design: docs/epics/001-toolchain-and-stdlib/
// design-debug-info.md; spec: specs/025-debug-info/spec.md.

#include "CodeGen.h"

#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Module.h"

using namespace QLang;

// Split a source path into (directory, filename) and return a cached DIFile.
// --combine gives each .b its own DIFile so line tables name the correct source.
llvm::DIFile *CodeGen::getOrCreateDIFile( const std::string &path )
{
	if ( !mDebugInfo || mDIBuilder == nullptr )
		return nullptr;

	auto it = mDIFileCache.find( path );
	if ( it != mDIFileCache.end() )
		return it->second;

	std::string dir;
	std::string file = path;
	size_t slash = path.find_last_of( '/' );
	if ( slash != std::string::npos )
	{
		dir = path.substr( 0, slash );
		file = path.substr( slash + 1 );
	}
	if ( dir.empty() )
		dir = ".";

	llvm::DIFile *dif = mDIBuilder->createFile( file, dir );
	mDIFileCache[path] = dif;
	return dif;
}

// Attach a DISubprogram to an LLVM function and make it the current scope.
// A minimal DISubroutineType (unspecified params) is verifier-legal for v1;
// richer parameter types are a documented nice-to-have (spec §B).
llvm::DISubprogram *CodeGen::createDISubprogram( llvm::Function *llvmFunc,
	const SourceLocation &loc, const std::string &name )
{
	if ( !mDebugInfo || mDIBuilder == nullptr || llvmFunc == nullptr )
		return nullptr;

	// A function whose location is unset (synthetic) is anchored on the compile
	// unit's primary file at line 0 rather than dropped, so every emitted
	// function carries a subprogram (keeps emission verifier-clean under -O).
	std::string filePath = loc.isSet() ? loc.file : "";
	llvm::DIFile *dif = filePath.empty()
		? ( mDICompileUnit ? mDICompileUnit->getFile() : nullptr )
		: getOrCreateDIFile( filePath );
	if ( dif == nullptr )
		return nullptr;

	unsigned line = loc.isSet() ? loc.line : 0;

	// Minimal subroutine type: one null element == unspecified return/params.
	llvm::SmallVector<llvm::Metadata*, 1> eltTys;
	eltTys.push_back( nullptr );
	llvm::DISubroutineType *subTy = mDIBuilder->createSubroutineType(
		mDIBuilder->getOrCreateTypeArray( eltTys ) );

	llvm::DISubprogram *sp = mDIBuilder->createFunction(
		dif, name, llvmFunc->getName(), dif, line, subTy, line,
		llvm::DINode::FlagPrototyped,
		llvm::DISubprogram::SPFlagDefinition );

	llvmFunc->setSubprogram( sp );
	mCurrentDISubprogram = sp;
	return sp;
}

// Set the IRBuilder's current DebugLoc from a source location, scoped to the
// function currently being generated. ARC/scope-exit instructions emitted while
// this is set inherit a valid location (verifier-clean under -O, spec §F).
void CodeGen::applyDebugLoc( const SourceLocation &loc )
{
	if ( !mDebugInfo || mCurrentDISubprogram == nullptr )
		return;

	unsigned line = loc.isSet() ? loc.line : 0;
	unsigned col = loc.isSet() ? loc.col : 0;
	mBuilder->SetCurrentDebugLocation(
		llvm::DILocation::get( *mContext, line, col, mCurrentDISubprogram ) );
}

// Reset builder debug state on leaving a function so no stale scope leaks into
// the next function's instructions.
void CodeGen::clearDebugLoc()
{
	if ( !mDebugInfo )
		return;
	mBuilder->SetCurrentDebugLocation( llvm::DebugLoc() );
	mCurrentDISubprogram = nullptr;
}

// Finalize all debug metadata. MUST run after codegen and before print() —
// unfinalized DWARF metadata is invalid and the verifier rejects it (spec §E).
void CodeGen::finalizeDebugInfo()
{
	if ( !mDebugInfo || mDIBuilder == nullptr || mDebugFinalized )
		return;
	mDIBuilder->finalize();
	mDebugFinalized = true;
}
