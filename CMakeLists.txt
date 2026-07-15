cmake_minimum_required(VERSION 3.16)
project(BLang LANGUAGES C CXX)

# Platform detection
if(APPLE)
	add_compile_definitions(PLATFORM_DARWIN)
elseif(UNIX)
	add_compile_definitions(PLATFORM_LINUX)
elseif(WIN32)
	add_compile_definitions(PLATFORM_WINDOWS)
endif()

# Optional LLVM integration for code generation
option(BLANG_ENABLE_LLVM "Enable LLVM code generation (auto-detected by default)" ON)
if(NOT BLANG_ENABLE_LLVM)
	message(STATUS "LLVM disabled by BLANG_ENABLE_LLVM=OFF -- building in parse-only mode")
	set(LLVM_FOUND FALSE)
endif()
if(BLANG_ENABLE_LLVM)
	find_package(LLVM CONFIG QUIET)
endif()
if(LLVM_FOUND)
	message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
	include_directories(${LLVM_INCLUDE_DIRS})
	add_definitions(${LLVM_DEFINITIONS})
	llvm_map_components_to_libnames(llvm_libs
		support
		core
		irreader
		passes
	)
else()
	message(STATUS "LLVM not found -- building without code generation support")
endif()

# Optional sanitizer instrumentation (opt-in; OFF by default so normal/CI
# non-sanitizer builds are byte-for-byte unchanged). Comma-separated list, e.g.
#   cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined
# Flags are added to BOTH compile and link lines and, because this block precedes
# every target definition, apply uniformly to the compiler (qcc/bcc/lexer) and
# the C runtime libraries. UBSan is made FATAL (-fno-sanitize-recover) so real UB
# fails the build's test run rather than printing and continuing.
set(BLANG_SANITIZE "" CACHE STRING
	"Comma-separated sanitizers to instrument with (e.g. address,undefined). Empty = off.")
if(BLANG_SANITIZE)
	message(STATUS "Sanitizers enabled: ${BLANG_SANITIZE}")
	add_compile_options(-fsanitize=${BLANG_SANITIZE} -fno-sanitize-recover=all
		-fno-omit-frame-pointer -g)
	add_link_options(-fsanitize=${BLANG_SANITIZE})
endif()

# Lexer test targets
add_executable(lexerTest LexerTest.cpp FileLexer.cpp LexerReader.cpp)
set_target_properties(lexerTest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

add_executable(lexerTest2 LexerTest2.cpp FileLexer.cpp LexerReader.cpp)
set_target_properties(lexerTest2 PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# Main compiler source files
set(QCC_SOURCES
	qcc.cpp
	FileLexer.cpp
	LexerReader.cpp
	QBlock.cpp
	QBreakContinue.cpp
	QEnumDefinition.cpp
	QExpression.cpp
	QFieldAccessExpression.cpp
	QForInStatement.cpp
	QFunctionDefinition.cpp
	QImplBlock.cpp
	QMatchExpression.cpp
	QProtocolDefinition.cpp
	QReturnStatement.cpp
	QStatement.cpp
	QStructDefinition.cpp
	QType.cpp
	QVariableDefinition.cpp
	QSpawnStatement.cpp
	QEventHandler.cpp
	QTestBlock.cpp
	QAssertStatement.cpp
	QQueryExpression.cpp
	QLambdaExpression.cpp
	BmodEmitter.cpp
	LocationDumper.cpp
	DiagnosticEngine.cpp
	Sema.cpp
	FormatString.cpp)

# Add LLVM code generation when available
if(LLVM_FOUND)
	list(APPEND QCC_SOURCES
		CodeGen.cpp CGStatements.cpp CGExpressions.cpp CGTypes.cpp
		CGStruct.cpp CGEnum.cpp CGLambda.cpp CGRuntime.cpp)
endif()

add_executable(qcc ${QCC_SOURCES})
# NOTE: JH_VERBOSE_LOGGING is intentionally NOT defined on qcc. It would enable
# logging.h's LOG/TRACE_BEGIN macros, which fprintf compiler-developer trace
# ([TRACE] …, "Saving position", …) to stderr on every parse — incompatible
# with U2's quiet-by-default contract (FR-007 / Gate D). Compiler developers
# who want that trace can build with -DJH_VERBOSE_LOGGING.
set_target_properties(qcc PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

if(LLVM_FOUND)
	target_compile_definitions(qcc PRIVATE BLANG_HAS_LLVM)
	target_link_libraries(qcc ${llvm_libs} blang_sqlgen)
endif()

# ---------------------------------------------------------------------------
# Bounded parser fuzzing (libFuzzer) — opt-in, clang-only (U5, REQ-006).
# Configure a dedicated clang build:
#   cmake -S . -B build-fuzz -DBLANG_ENABLE_LLVM=OFF -DBLANG_FUZZ=ON \
#         -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
#   cmake --build build-fuzz --target fuzz_parse
# Uses the PARSE-ONLY compiler sources (QCC_SOURCES here has no CodeGen because
# the fuzz build sets BLANG_ENABLE_LLVM=OFF) with qcc's main() compiled out.
# ---------------------------------------------------------------------------
option(BLANG_FUZZ "Build the libFuzzer parser target fuzz_parse (requires clang)" OFF)
if(BLANG_FUZZ)
	if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		message(FATAL_ERROR "BLANG_FUZZ requires clang (libFuzzer). Reconfigure with -DCMAKE_CXX_COMPILER=clang++.")
	endif()
	add_executable(fuzz_parse ${QCC_SOURCES} fuzz/fuzz_parse.cpp)
	set_target_properties(fuzz_parse PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
	target_include_directories(fuzz_parse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	target_compile_definitions(fuzz_parse PRIVATE BLANG_FUZZ_HARNESS)
	target_compile_options(fuzz_parse PRIVATE -fsanitize=fuzzer,address -fno-omit-frame-pointer -g)
	target_link_options(fuzz_parse PRIVATE -fsanitize=fuzzer,address)
	if(LLVM_FOUND)
		# The parser needs no LLVM; keep fuzz_parse lean. (Normally the fuzz build
		# is configured BLANG_ENABLE_LLVM=OFF so this branch is not taken.)
		target_compile_definitions(fuzz_parse PRIVATE BLANG_HAS_LLVM)
		target_link_libraries(fuzz_parse ${llvm_libs} blang_sqlgen)
	endif()
endif()

# BLang runtime library (ARC, thread pool, channels, async)
add_library(blang_runtime STATIC runtime/blang_runtime.c)
set_target_properties(blang_runtime PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(blang_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime)
find_package(Threads REQUIRED)
target_link_libraries(blang_runtime Threads::Threads)

# Also build a standalone .o for linking into BLang programs via bcc
add_library(blang_runtime_obj OBJECT runtime/blang_runtime.c)
set_target_properties(blang_runtime_obj PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(blang_runtime_obj PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime)

# BLang string runtime library
add_library(blang_string STATIC runtime/blang_string.c)
set_target_properties(blang_string PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(blang_string PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime)

# BLang array runtime library
add_library(blang_array STATIC runtime/blang_array.c)
set_target_properties(blang_array PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(blang_array PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime)

# BLang buffer runtime library (byte buffer for I/O)
add_library(blang_buffer STATIC runtime/blang_buffer.c)
set_target_properties(blang_buffer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(blang_buffer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime)

# BLang JSON runtime library
add_library(blang_json STATIC runtime/blang_json.c)
set_target_properties(blang_json PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(blang_json PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime)

# BLang networking runtime library (TCP sockets + poll-based Selector)
add_library(blang_net STATIC runtime/blang_net.c)
set_target_properties(blang_net PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(blang_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime)
target_link_libraries(blang_net blang_string blang_buffer)

# BLang filesystem runtime library (file I/O + directory ops)
add_library(blang_fs STATIC runtime/blang_fs.c)
set_target_properties(blang_fs PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(blang_fs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime)
target_link_libraries(blang_fs blang_string blang_buffer blang_array)

# BLang sys runtime library (argc/argv capture for sys.args)
add_library(blang_sys STATIC runtime/blang_sys.c)
set_target_properties(blang_sys PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(blang_sys PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime)
target_link_libraries(blang_sys blang_string blang_array)

# BLang test-runner driver (behind `bcc test`): fork-isolated test dispatch.
add_library(blang_testrunner STATIC runtime/blang_testrunner.c)
set_target_properties(blang_testrunner PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(blang_testrunner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime)

# BLang database runtime library
add_library(blang_db STATIC runtime/blang_db.c)
set_target_properties(blang_db PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(blang_db PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime)

# Check for libuv (event loop for async/await)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
	pkg_check_modules(LIBUV QUIET libuv)
endif()
if(LIBUV_FOUND)
	message(STATUS "Found libuv: ${LIBUV_VERSION}")
	target_compile_definitions(blang_runtime PRIVATE BLANG_HAS_LIBUV)
	target_compile_definitions(blang_runtime_obj PRIVATE BLANG_HAS_LIBUV)
	target_include_directories(blang_runtime PRIVATE ${LIBUV_INCLUDE_DIRS})
	target_include_directories(blang_runtime_obj PRIVATE ${LIBUV_INCLUDE_DIRS})
	target_link_libraries(blang_runtime ${LIBUV_LIBRARIES})
else()
	message(STATUS "libuv not found -- async/await will use pthread fallback")
endif()

# Check for SQLite3
if(PkgConfig_FOUND)
	pkg_check_modules(SQLITE3 QUIET sqlite3)
endif()
if(SQLITE3_FOUND)
	message(STATUS "Found SQLite3: ${SQLITE3_VERSION}")
	target_compile_definitions(blang_db PRIVATE BLANG_HAS_SQLITE)
	target_include_directories(blang_db PRIVATE ${SQLITE3_INCLUDE_DIRS})
	target_link_libraries(blang_db ${SQLITE3_LIBRARIES})
else()
	message(STATUS "SQLite3 not found -- database runtime will use stubs")
endif()

# SHA-256 library (standalone C, used by BuildCache)
add_library(blang_sha256 STATIC sha256.c)
set_target_properties(blang_sha256 PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

# Build cache (artifact caching for incremental builds)
add_library(blang_buildcache STATIC BuildCache.cpp)
set_target_properties(blang_buildcache PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(blang_buildcache blang_sha256)

# SQL generation and schema migration (C++ library for qcc/bcc)
add_library(blang_sqlgen STATIC SQLGen.cpp SchemaMigration.cpp)
set_target_properties(blang_sqlgen PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(blang_sqlgen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# bcc - BLang compiler driver (orchestrates qcc -> llc -> cc pipeline)
add_executable(bcc bcc.cpp ProjectConfig.cpp BuildCache.cpp sha256.c)
set_target_properties(bcc PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# Bake tool paths into bcc at build time so it works regardless of runtime PATH/arch
target_compile_definitions(bcc PRIVATE BCC_CC_PATH="${CMAKE_C_COMPILER}")
target_compile_definitions(bcc PRIVATE BCC_HOST_ARCH="${CMAKE_SYSTEM_PROCESSOR}")
target_compile_definitions(bcc PRIVATE BCC_RUNTIME_LIB="${CMAKE_BINARY_DIR}/libblang_runtime.a")
target_compile_definitions(bcc PRIVATE BCC_STRING_LIB="${CMAKE_BINARY_DIR}/libblang_string.a")
target_compile_definitions(bcc PRIVATE BCC_ARRAY_LIB="${CMAKE_BINARY_DIR}/libblang_array.a")
target_compile_definitions(bcc PRIVATE BCC_BUFFER_LIB="${CMAKE_BINARY_DIR}/libblang_buffer.a")
target_compile_definitions(bcc PRIVATE BCC_JSON_LIB="${CMAKE_BINARY_DIR}/libblang_json.a")
target_compile_definitions(bcc PRIVATE BCC_NET_LIB="${CMAKE_BINARY_DIR}/libblang_net.a")
target_compile_definitions(bcc PRIVATE BCC_FS_LIB="${CMAKE_BINARY_DIR}/libblang_fs.a")
target_compile_definitions(bcc PRIVATE BCC_SYS_LIB="${CMAKE_BINARY_DIR}/libblang_sys.a")
target_compile_definitions(bcc PRIVATE BCC_TESTRUNNER_LIB="${CMAKE_BINARY_DIR}/libblang_testrunner.a")
# Ensure the test-runner driver lib is built before bcc links against it via `bcc test`.
add_dependencies(bcc blang_testrunner)
message(STATUS "bcc: C compiler = ${CMAKE_C_COMPILER}")
message(STATUS "bcc: host arch = ${CMAKE_SYSTEM_PROCESSOR}")

# Copy stdlib .b files alongside executables for --combine auto-include
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/stdlib/buffer.b ${CMAKE_BINARY_DIR}/stdlib/buffer.b COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/stdlib/net.b ${CMAKE_BINARY_DIR}/stdlib/net.b COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/stdlib/fs.b ${CMAKE_BINARY_DIR}/stdlib/fs.b COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/stdlib/sys.b ${CMAKE_BINARY_DIR}/stdlib/sys.b COPYONLY)

if(LIBUV_FOUND)
	target_compile_definitions(bcc PRIVATE BCC_HAS_LIBUV)
endif()

if(LLVM_FOUND)
	find_program(LLC_EXECUTABLE NAMES llc-18 llc HINTS ${LLVM_TOOLS_BINARY_DIR} NO_DEFAULT_PATH)
	if(NOT LLC_EXECUTABLE)
		find_program(LLC_EXECUTABLE NAMES llc-18 llc HINTS ${LLVM_TOOLS_BINARY_DIR})
	endif()
	if(LLC_EXECUTABLE)
		message(STATUS "bcc: llc = ${LLC_EXECUTABLE}")
		target_compile_definitions(bcc PRIVATE BCC_LLC_PATH="${LLC_EXECUTABLE}")
	endif()
endif()

# ---------------------------------------------------------------------------
# Runtime C unit tests (CTest). Dependency-free assert harness in runtime/tests/
# (test_util.h). One add_test() per behavior so `ctest -N` reports each case.
# These build in every configuration, so a BLANG_SANITIZE build (build-asan)
# runs them under ASan/UBSan automatically.
# ---------------------------------------------------------------------------
enable_testing()

add_executable(test_array runtime/tests/test_array.c)
target_link_libraries(test_array blang_array blang_string)
set_target_properties(test_array PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

add_executable(test_string runtime/tests/test_string.c)
target_link_libraries(test_string blang_string blang_array)
set_target_properties(test_string PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

add_executable(test_buffer runtime/tests/test_buffer.c)
target_link_libraries(test_buffer blang_buffer blang_array blang_string)
set_target_properties(test_buffer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

add_executable(test_json runtime/tests/test_json.c)
target_link_libraries(test_json blang_json)
set_target_properties(test_json PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

add_executable(test_fs runtime/tests/test_fs.c)
target_link_libraries(test_fs blang_fs blang_buffer blang_array blang_string)
set_target_properties(test_fs PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

add_executable(test_net runtime/tests/test_net.c)
target_link_libraries(test_net blang_net blang_runtime blang_string blang_buffer blang_array Threads::Threads)
set_target_properties(test_net PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

# One add_test per case (name must match the case table in each test_*.c).
set(ARRAY_CASES  create_empty push_length get set pop pop_empty insert_remove concat clear grow get_oob set_oob get_null)
set(STRING_CASES create_len empty equals concat substring to_upper to_lower trim contains starts_ends index_of compare to_int replace char_at_oob)
set(BUFFER_CASES create append_byte from_string append_string get_set to_string slice index_of clear get_oob)
set(JSON_CASES   decode_int decode_string decode_bool decode_error object_roundtrip array_build object_get_missing)
set(FS_CASES     write_read seek size remove mkdir_list)
set(NET_CASES    connect_refused connect_bad_host selector_lifecycle close_invalid)

foreach(c ${ARRAY_CASES})
	add_test(NAME array_${c} COMMAND test_array ${c})
endforeach()
foreach(c ${STRING_CASES})
	add_test(NAME string_${c} COMMAND test_string ${c})
endforeach()
foreach(c ${BUFFER_CASES})
	add_test(NAME buffer_${c} COMMAND test_buffer ${c})
endforeach()
foreach(c ${JSON_CASES})
	add_test(NAME json_${c} COMMAND test_json ${c})
endforeach()
foreach(c ${FS_CASES})
	add_test(NAME fs_${c} COMMAND test_fs ${c})
endforeach()
foreach(c ${NET_CASES})
	add_test(NAME net_${c} COMMAND test_net ${c})
endforeach()
