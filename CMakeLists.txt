cmake_minimum_required (VERSION 2.6)
project (BLang)

add_subdirectory(jhcommon)

add_definitions(-DPLATFORM_DARWIN)
add_definitions(-DJH_VERBOSE_LOGGING)

add_executable(lexerTest LexerTest.cpp FileLexer.cpp LexerReader.cpp )
target_link_libraries(lexerTest jhcommon )

add_executable(lexerTest2 LexerTest2.cpp FileLexer.cpp LexerReader.cpp )
target_link_libraries(lexerTest2 jhcommon )

add_executable(qcc qcc.cpp FileLexer.cpp LexerReader.cpp
	QBlock.cpp 
	QExpression.cpp 
	QFunctionDefinition.cpp
	QReturnStatement.cpp
	QStatement.cpp
	QType.cpp 
	QVariableDefinition.cpp  )	
target_link_libraries(qcc jhcommon )

include_directories(${JHCOMMON_SOURCE_DIR}/include)
link_directories(${JHCOMMON_BINARY_DIR}/src)




