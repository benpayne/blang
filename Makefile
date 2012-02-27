LLVM_FLAGS=$(shell llvm-config --cxxflags )

CFLAGS=-DYYERROR_VERBOSE -DYYDEBUG=0 -g -Wall ${LLVM_FLAGS}
CXXFLAGS=${CFLAGS}
YFLAGS=-d
LDFLAGS=-g $(shell llvm-config --ldflags --libs core ) -lfl -ly 

all : bcc

parser.cpp lexer.cpp : parser.yy lexer.l
	bison -d -o parser parser.yy
	bison parser.yy
	mv parser.tab.cc parser.cpp
	flex -t lexer.l > lexer.cpp

bcc : lexer.o parser.o parse_helpers.o
	g++ $(LDFLAGS) lexer.o parser.o parse_helpers.o -o $@

#llvm_test : llvm_test.cpp
#	g++ $(CXXFLAGS) $(LDFLAGS) llvm_test.cpp -o $@

clean:
	rm -f parser.h lexer.cpp parser.cpp lexer.o parser.o bcc llvm_test

# DO NOT DELETE

parse_helpers.o: Symbol.h Scope.h parse_helpers.h
