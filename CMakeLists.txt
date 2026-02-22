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
find_package(LLVM CONFIG QUIET)
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
	QExpression.cpp
	QFunctionDefinition.cpp
	QReturnStatement.cpp
	QStatement.cpp
	QType.cpp
	QVariableDefinition.cpp)

# Add LLVM code generation when available
if(LLVM_FOUND)
	list(APPEND QCC_SOURCES CodeGen.cpp)
endif()

add_executable(qcc ${QCC_SOURCES})
target_compile_definitions(qcc PRIVATE JH_VERBOSE_LOGGING)
set_target_properties(qcc PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

if(LLVM_FOUND)
	target_compile_definitions(qcc PRIVATE BLANG_HAS_LLVM)
	target_link_libraries(qcc ${llvm_libs})
endif()
