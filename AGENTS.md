# AGENTS.md

## Cursor Cloud specific instructions

BLang is a single product: the C++17 compiler toolchain for the BLang language
(driver `bcc` + parser/IR generator `qcc` + `libblang_*` runtime libs). There
are no long-running services; "run the app" means compile a `.b` file to a
native binary and execute it. Standard build/test/run commands live in
`CLAUDE.md` (see "Build System", "Testing"); prefer those over duplicating here.

Build with LLVM codegen (parse-only mode can't produce binaries):

```bash
mkdir -p build && cd build
cmake .. -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
make -j$(nproc)
```

The update script only refreshes apt dependencies; it intentionally does NOT
build. Run the `cmake`/`make` step yourself after startup (the `build/`
directory is not persisted). Verify with `./run_tests.sh` (parse/fail suite,
needs only `qcc`) and `./test_codegen.sh` (end-to-end qcc→llc→cc→run, needs the
LLVM build). Compile+run a single program with
`./build/bcc file.b -o out && ./out`.

### Non-obvious gotchas

- The `llvm-18` apt packages register clang as the default `cc`/`c++`
  alternative, and clang-18 here targets the gcc-14 toolchain. `libstdc++-14-dev`
  MUST be installed or every link fails with `cannot find -lstdc++` (this
  affects both the CMake C++ compiler check and `bcc`'s link step, since `bcc`
  bakes in whatever `cc` resolves to at configure time). It is included in the
  update script.
- `bcc` expects `qcc` in the same directory and needs `llc-18`/`llc` + `cc` on
  PATH; tool paths (`cc`, `llc`, runtime lib paths, host arch) are baked into
  `bcc` at CMake configure time, so re-run `cmake` if the toolchain moves.
- LLVM 18, SQLite3, and libuv are present; libpq/PostgreSQL is absent so that DB
  backend stays disabled (SQLite is the tested backend) — this is expected.
