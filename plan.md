# Plan: Modernize BLang Frameworks and Tooling

## Summary

Upgrade BLang from LLVM 3.x-era tooling to modern LLVM (21+), modernize the CMake
build system, and update C++ standards. The active QLang parser (`qcc`) does not use
LLVM and requires no changes — all LLVM work is in the legacy `BLang` namespace files.

There are two strategic options for LLVM modernization:
- **Option A**: Update the 4 legacy files to compile against modern LLVM
- **Option B**: Skip legacy migration entirely and build a new LLVM backend for the QLang AST

This plan covers **Option A** (modernize existing code) plus a foundation step for
Option B, plus CMake/tooling modernization that benefits both paths. The owner can
decide whether to pursue both or just one approach.

---

## Step 1: Modernize CMakeLists.txt

**File:** `CMakeLists.txt`

Changes:
1. Raise `cmake_minimum_required` from `VERSION 2.6` to `VERSION 3.16`
2. Set C++ standard to C++17 (`set(CMAKE_CXX_STANDARD 17)`, `set(CMAKE_CXX_STANDARD_REQUIRED ON)`)
3. Add LLVM discovery via `find_package(LLVM REQUIRED CONFIG)` and `llvm_map_components_to_libnames`
4. Replace global `add_definitions(-DPLATFORM_DARWIN)` with platform-detected `target_compile_definitions`:
   - `if(APPLE)` → `PLATFORM_DARWIN`
   - `if(UNIX AND NOT APPLE)` → `PLATFORM_LINUX`
5. Replace global `include_directories()` with per-target `target_include_directories()`
6. Remove `link_directories()` (unnecessary with modern target-based linking)
7. Add LLVM include dirs, definitions, and link libraries to any target that needs them
   (the legacy Bison/Flex target, if re-added; not needed for `qcc`, `lexerTest`, `lexerTest2`)

---

## Step 2: Update LLVM Header Paths (4 files)

All LLVM IR headers moved from `llvm/` to `llvm/IR/` in LLVM 3.3. Two other headers
were reorganized into different subdirectories or removed.

### `Symbol.h` (BLang namespace)
- `#include "llvm/Type.h"` → `#include "llvm/IR/Type.h"`
- `#include "llvm/Value.h"` → `#include "llvm/IR/Value.h"`

### `Scope.h` (BLang namespace)
- `#include "llvm/BasicBlock.h"` → `#include "llvm/IR/BasicBlock.h"`
- `#include "llvm/Function.h"` → `#include "llvm/IR/Function.h"`

### `parse_helpers.h`
- `#include "llvm/Value.h"` → `#include "llvm/IR/Value.h"`

### `parse_helpers.cpp`
- `#include "llvm/Module.h"` → `#include "llvm/IR/Module.h"`
- `#include "llvm/Function.h"` → `#include "llvm/IR/Function.h"`
- `#include "llvm/DerivedTypes.h"` → `#include "llvm/IR/DerivedTypes.h"`
- `#include "llvm/LLVMContext.h"` → `#include "llvm/IR/LLVMContext.h"`
- `#include "llvm/PassManager.h"` → **Remove** (legacy pass manager removed)
- `#include "llvm/Analysis/Verifier.h"` → `#include "llvm/IR/Verifier.h"`
- `#include "llvm/Assembly/PrintModulePass.h"` → **Remove** (use `Module::print()` directly)
- `#include "llvm/Support/IRBuilder.h"` → `#include "llvm/IR/IRBuilder.h"`
- Add `#include "llvm/IR/Constants.h"` (for `ConstantInt`, `ConstantArray`, `ConstantDataArray`)

---

## Step 3: Migrate to Opaque Pointers in `parse_helpers.cpp`

Typed pointers were removed in LLVM 17. All pointer types are now opaque (`ptr`).
The element type must come from the compiler's own symbol table rather than from LLVM.

### 3a. `PointerType::get()` signature change
- **Lines 44, 61:** `PointerType::get(Type::getInt8Ty(gContext), 0)` → `PointerType::get(gContext, 0)`

### 3b. `CreateLoad` requires explicit type parameter
- **Line 320:** `gBuilder->CreateLoad(l)` → `gBuilder->CreateLoad(pointeeType, l)`
- **Line 325:** `gBuilder->CreateLoad(r)` → `gBuilder->CreateLoad(pointeeType, r)`
- **Line 438:** `gBuilder->CreateLoad(r)` → `gBuilder->CreateLoad(pointeeType, r)`

The pointee type must be retrieved from the BLang `Symbol` object associated with
the variable being loaded. This requires threading the `Symbol*` through to the
load call sites, or storing type information alongside the `Value*`.

### 3c. Remove `PointerType::getElementType()` usage
- **Lines 441-442:** Replace `lPrt->getElementType()` with the type from the symbol table:
  ```cpp
  // OLD:
  PointerType *lPrt = (PointerType*)l->getType();
  IntegerType *lType = (IntegerType*)lPrt->getElementType();
  // NEW:
  IntegerType *lType = cast<IntegerType>(symbolForL->getType());
  ```

### 3d. Pointer type checking
- **Lines 318, 323, 431, 436:** `l->getType()->getTypeID() == Type::PointerTyID` still
  works with opaque pointers, but prefer `l->getType()->isPointerTy()` for clarity.

---

## Step 4: Update `getOrInsertFunction` Return Type

`Module::getOrInsertFunction` now returns `FunctionCallee` instead of `Constant*`
(changed in LLVM 9).

- **Line 63** (`init` function):
  ```cpp
  // OLD:
  gMod->getOrInsertFunction("printf", ft);
  // NEW (if you need the Function*):
  FunctionCallee fc = gMod->getOrInsertFunction("printf", ft);
  Function *printfFunc = cast<Function>(fc.getCallee());
  ```

- **Lines 191-193** (`endFunctionDef`):
  ```cpp
  // OLD:
  Constant* c = gMod->getOrInsertFunction(name, ft);
  Function* func = cast<Function>(c);
  // NEW:
  FunctionCallee fc = gMod->getOrInsertFunction(name, ft);
  Function* func = cast<Function>(fc.getCallee());
  ```

---

## Step 5: Update `CreateCall` Signature

`CreateCall` now takes `FunctionCallee` or `FunctionType*` + `Value*` instead of
just `Function*`.

- **Line 246:**
  ```cpp
  // OLD:
  Value* recur_1 = gBuilder->CreateCall(func, gArgs);
  // NEW:
  Value* recur_1 = gBuilder->CreateCall(func->getFunctionType(), func, gArgs);
  ```

---

## Step 6: Replace Legacy Pass Manager

The legacy `PassManager` has been removed from LLVM's optimization pipeline.

- **Lines 68-73** (`destroy` function):
  ```cpp
  // OLD:
  verifyModule(*gMod, PrintMessageAction);
  PassManager PM;
  PM.add(createPrintModulePass(&outs()));
  PM.run(*gMod);

  // NEW (simplest approach — no pass manager needed for just verifying + printing):
  if (verifyModule(*gMod, &errs())) {
      errs() << "Module verification failed\n";
  }
  gMod->print(outs(), nullptr);
  ```

---

## Step 7: Modernize C++ Usage Across Legacy Files

- Replace all `NULL` with `nullptr` in `parse_helpers.cpp`, `Symbol.h`, `Scope.h`
- Use range-based `for` loops where iterating function args (lines 195-202):
  ```cpp
  unsigned i = 1;
  for (auto &arg : func->args()) {
      arg.setName(gFunctionParams[i]->getName());
      gFunctionParams[i]->setValue(&arg);
      i++;
  }
  ```

---

## Step 8: Verify Build and Fix Compilation Errors

1. Ensure LLVM 18+ is installed (or set `LLVM_DIR` to point at the install)
2. Initialize the jhcommon submodule: `git submodule update --init`
3. Build: `mkdir build && cd build && cmake .. -DLLVM_DIR=/path/to/llvm/cmake && make`
4. Fix any remaining compilation errors from API changes not covered above
5. Test the legacy Bison/Flex path if a build target is added for it
6. Test the `qcc` target: `./qcc ../test.c` (should be unaffected)

---

## Step 9 (Optional): Remove or Archive Legacy Bison/Flex Files

The following files belong to the superseded Bison/Flex approach and are not compiled
into any current target. They can be removed or moved to an `archive/` directory:

- `parser.yy` — Bison grammar
- `parser.h` — Generated Bison token header
- `lexer.l` — Flex lexer spec (also contains an old `main()`)
- `parse_helpers.h` / `parse_helpers.cpp` — LLVM codegen tied to Bison actions
- `Symbol.h` (BLang namespace) — LLVM-dependent symbol class
- `Scope.h` (BLang namespace) — LLVM-dependent scope class
- `Lexer.h` — Abstract lexer interface (superseded by `FileLexer.h`)

**Note:** If these files are archived/removed, LLVM becomes entirely optional until
a new code generation backend is built for the QLang AST. The `qcc`, `lexerTest`,
and `lexerTest2` targets would continue to work with no LLVM dependency.

---

## Step 10 (Optional): Lay Groundwork for New QLang Code Generation Backend

Instead of (or in addition to) modernizing the old legacy code, add a new code
generation path that walks the QLang AST and emits LLVM IR using modern APIs.

1. Create `Codegen.h` / `Codegen.cpp` with a `CodegenVisitor` class
2. Add `codegen()` or `emit()` virtual methods to the `Statement` / `Expression` base classes
3. Use modern LLVM APIs from the start (opaque pointers, new pass manager, `FunctionCallee`)
4. Wire it into `qcc.cpp` after `Module::Parse` completes

This is likely the better long-term investment since the QLang AST is cleaner than
the Bison-coupled code generation approach.

---

## Files Modified (Summary)

| File | Change Type |
|---|---|
| `CMakeLists.txt` | Modernize build system, add LLVM integration, platform detection |
| `Symbol.h` | Update LLVM include paths |
| `Scope.h` | Update LLVM include paths |
| `parse_helpers.h` | Update LLVM include paths |
| `parse_helpers.cpp` | Update includes, opaque pointers, pass manager, API signatures, C++ modernization |

## No Changes Required

| File | Reason |
|---|---|
| `qcc.cpp`, `Q*.cpp` | Active QLang parser — no LLVM dependency |
| `Type.h`, `Expression.h` | QLang AST classes — no LLVM dependency |
| `FileLexer.h/cpp`, `LexerReader.cpp` | Hand-written lexer — no LLVM dependency |
| `CompilerHelpers.h` | Error handling — no LLVM dependency |
| `test.c`, `test_files/*` | Test source files — language-level, not compiler-level |
