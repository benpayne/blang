#ifndef PARSE_HELPERS_H_
#define PARSE_HELPERS_H_

#define SCOPE_GLOBAL 	0
#define SCOPE_MODULE 	1
#define SCOPE_ANONYMOUS	2
#define SCOPE_NAMESPACE	3
#define SCOPE_CLASS		4
#define SCOPE_FUNCTION	5
#define SCOPE_IF		6

#include "llvm/Value.h"


struct ConstData {
	unsigned long long uvalue;
	long long value;
	int width;
	bool _signed;
};

void init( const char *filename );
void destroy();
void getSymbol( const char *name );
llvm::Value *declareVariable( const char *name );
void getType( const char *name );
void addType( const char *name );
void pushScope( int type, const char *name );
void popScope();
void startFunction( const char *ret_type, const char *name );
void addFunctionParam( const char *type, const char *name );
void endFunctionDef();
void endFunction();

void startFunctionCall();
void addFunctionCallParam( llvm::Value *v );
llvm::Value *endFunctionCall( const char *name );

llvm::Value *getVariableValue( const char *name );

llvm::Value *getArrayLookupValue( llvm::Value *array, llvm::Value *index );

llvm::Value *getConstantNumberValue( ConstData *data );
llvm::Value *getConstantStringValue( const char *str );

llvm::Value *getBinaryExpression( const char *op, llvm::Value *l, llvm::Value *r );
llvm::Value *getAssignExpression( const char *op, llvm::Value *l, llvm::Value *r );

void startIf( llvm::Value *expression );
void endIf();
void startElse( llvm::Value *expression );

void addReturn( llvm::Value *val );

void assignDecl( const char *op, llvm::Value *r );

#endif // PARSE_HELPERS_H_
