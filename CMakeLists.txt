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
	QQueryExpression.cpp)

# Add LLVM code generation when available
if(LLVM_FOUND)
	list(APPEND QCC_SOURCES CodeGen.cpp)
endif()

add_executable(qcc ${QCC_SOURCES})
target_compile_definitions(qcc PRIVATE JH_VERBOSE_LOGGING)
set_target_properties(qcc PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

if(LLVM_FOUND)
	target_compile_definitions(qcc PRIVATE BLANG_HAS_LLVM)
	target_link_libraries(qcc ${llvm_libs} blang_sqlgen)
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

# BLang JSON runtime library
add_library(blang_json STATIC runtime/blang_json.c)
set_target_properties(blang_json PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(blang_json PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime)

# BLang database runtime library
add_library(blang_db STATIC runtime/blang_db.c)
set_target_properties(blang_db PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(blang_db PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime)

# Check for SQLite3
find_package(PkgConfig QUIET)
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

# SQL generation and schema migration (C++ library for qcc/bcc)
add_library(blang_sqlgen STATIC SQLGen.cpp SchemaMigration.cpp)
set_target_properties(blang_sqlgen PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(blang_sqlgen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# bcc - BLang compiler driver (orchestrates qcc -> llc -> cc pipeline)
add_executable(bcc bcc.cpp)
set_target_properties(bcc PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# Bake tool paths into bcc at build time so it works regardless of runtime PATH/arch
target_compile_definitions(bcc PRIVATE BCC_CC_PATH="${CMAKE_C_COMPILER}")
target_compile_definitions(bcc PRIVATE BCC_HOST_ARCH="${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "bcc: C compiler = ${CMAKE_C_COMPILER}")
message(STATUS "bcc: host arch = ${CMAKE_SYSTEM_PROCESSOR}")

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
