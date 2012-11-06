cmake_minimum_required (VERSION 2.6)
project (BLang)

add_subdirectory(jhcommon)

add_definitions(-DPLATFORM_DARWIN)

add_executable(lexerTest LexerTest.cpp FileLexer.cpp LexerReader.cpp )
target_link_libraries(lexerTest jhcommon )

include_directories(${JHCOMMON_SOURCE_DIR}/include)
link_directories(${JHCOMMON_BINARY_DIR}/src)




