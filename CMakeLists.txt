cmake_minimum_required(VERSION 3.16)
project(BLang LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(jhcommon)

# Platform detection
if(APPLE)
	add_compile_definitions(PLATFORM_DARWIN)
elseif(UNIX)
	add_compile_definitions(PLATFORM_LINUX)
elseif(WIN32)
	add_compile_definitions(PLATFORM_WINDOWS)
endif()

# Optional LLVM integration (for future code generation backend)
find_package(LLVM CONFIG QUIET)
if(LLVM_FOUND)
	message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
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
target_link_libraries(lexerTest PRIVATE jhcommon)
target_include_directories(lexerTest PRIVATE ${JHCOMMON_SOURCE_DIR}/include)

add_executable(lexerTest2 LexerTest2.cpp FileLexer.cpp LexerReader.cpp)
target_link_libraries(lexerTest2 PRIVATE jhcommon)
target_include_directories(lexerTest2 PRIVATE ${JHCOMMON_SOURCE_DIR}/include)

# Main compiler
add_executable(qcc
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
target_link_libraries(qcc PRIVATE jhcommon)
target_include_directories(qcc PRIVATE ${JHCOMMON_SOURCE_DIR}/include)
target_compile_definitions(qcc PRIVATE JH_VERBOSE_LOGGING)
