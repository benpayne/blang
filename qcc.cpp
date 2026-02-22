#include <assert.h>

#include <iostream>
#include <fstream>
#include <sstream>

#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

#ifdef BLANG_HAS_LLVM
#include "CodeGen.h"
#include "llvm/Support/raw_ostream.h"
#endif

using namespace QLang;
using namespace std;

Scope *gScope;

string CompileError::getMessage() const
{
	stringstream s;
	s << "Compiler Error in " << mFilename << ":" << mLineno << endl;
	s << mMessage << " at line: " << mLexer.getLineNumber();
	return s.str();
}

Module *Module::Parse( Lexer &l, Scope *s )
{
	Module *mod = new Module();
	SmartPtr<FunctionDefinition> def;
	try {
		while( !l.isEOF() )
		{
			// Peek past any trailing whitespace/comments to check for real EOF
			int nextSym = l.peekSymbol();
			if ( nextSym == -1 )
				break;

			if ( nextSym == Lexer::KEYWORD_STRUCT )
			{
				SmartPtr<StructDefinition> structDef = StructDefinition::Parse( l, s );
				continue;
			}

			if ( nextSym == Lexer::KEYWORD_PROTOCOL )
			{
				SmartPtr<ProtocolDefinition> protoDef = ProtocolDefinition::Parse( l, s );
				continue;
			}

			def = FunctionDefinition::Parse( l, s );
			mod->mFunctionList.push_back( def );
			cout << *def << endl;
		}
	} catch( CompileError &err ) {
		cerr << err.getMessage() << endl;
		return nullptr;
	}

	return mod;
}

WhileStatement *WhileStatement::Parse( Lexer &l, Scope *scope )
{
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_WHILE )
	{
		COMPILE_ERROR( l, "Internal Compiler Error" );
	}

	sym = l.getSymbol();
	if ( sym != '(' )
	{
		COMPILE_ERROR( l, "Expected \'(\'" );
	}

	WhileStatement *statement = new WhileStatement;
	
	statement->mLoopExpression = Expression::Parse( l, scope, ')' );
	
	if ( statement->mLoopExpression == nullptr )
	{
		COMPILE_ERROR( l, "Expected Expression in while statement" );
	}
	
	Scope *loop_scope = new Scope( Scope::kScope_Loop );
	loop_scope->setParent( scope );
	
	if ( l.peekSymbol() == '{' )
		statement->mLoopStatement = Block::Parse( l, loop_scope );
	else
		statement->mLoopStatement = Statement::Parse( l, loop_scope );

	return statement;
}

IfStatement *IfStatement::Parse( Lexer &l, Scope *scope )
{
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_IF )
	{
		COMPILE_ERROR( l, "Internal Compiler Error" );
	}

	sym = l.getSymbol();
	if ( sym != '(' )
	{
		COMPILE_ERROR( l, "Expected \'(\'" );
	}

	IfStatement *statement = new IfStatement;
	
	statement->mIfExpression = Expression::Parse( l, scope, ')' );
	
	if ( statement->mIfExpression == nullptr )
	{
		COMPILE_ERROR( l, "Expected Expression in while statement" );
	}
	
	Scope *if_scope = new Scope( Scope::kScope_IfElse );
	if_scope->setParent( scope );
	
	if ( l.peekSymbol() == '{' )
		statement->mStatement = Block::Parse( l, if_scope );
	else
		statement->mStatement = Statement::Parse( l, if_scope );
	
	if ( l.peekSymbol() == Lexer::KEYWORD_ELSE )
	{
		Scope *else_scope = new Scope( Scope::kScope_IfElse );
		else_scope->setParent( scope );
		
		sym = l.getSymbol();
		if ( l.peekSymbol() == '{' )
			statement->mElseStatement = Block::Parse( l, else_scope );
		else
			statement->mElseStatement = Statement::Parse( l, else_scope );
	}

	return statement;
}

ForStatement *ForStatement::Parse( Lexer &l, Scope *scope )
{
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_FOR )
	{
		COMPILE_ERROR( l, "Internal Compiler Error" );
	}

	sym = l.getSymbol();
	if ( sym != '(' )
	{
		COMPILE_ERROR( l, "Expected \'(\'" );
	}

	ForStatement *statement = new ForStatement;

	statement->mInitialExpression = Expression::Parse( l, scope );
	if ( statement->mInitialExpression == nullptr )
	{
		COMPILE_ERROR( l, "Expected Expression in for statement" );
	}

	statement->mTestExpression = Expression::Parse( l, scope );
	if ( statement->mTestExpression == nullptr )
	{
		COMPILE_ERROR( l, "Expected Expression in for statement" );
	}

	statement->mIterationExpression = Expression::Parse( l, scope, ')' );
	if ( statement->mIterationExpression == nullptr )
	{
		COMPILE_ERROR( l, "Expected Expression in for statement" );
	}

	Scope *for_scope = new Scope( Scope::kScope_Loop );
	for_scope->setParent( scope );

	if ( l.peekSymbol() == '{' )
		statement->mStatement = Block::Parse( l, for_scope );
	else
		statement->mStatement = Statement::Parse( l, for_scope );

	return statement;
}

int main( int argc, char *argv[] )
{
	if ( argc < 2 )
	{
		std::cerr << argv[ 0 ] << " [filename]" << std::endl;
		return -1;
	}

	gScope = new Scope( Scope::kScope_Global );
	gScope->addType( new Type( "int" ) );
	gScope->addType( new Type( "char" ) );
	gScope->addType( new Type( "string" ) );
	gScope->addType( new Type( "bool" ) );
	LexerReader reader( argv[ 1 ] );
	Lexer l( &reader );
	
	SmartPtr<Module> mod = Module::Parse( l, gScope );

	if ( mod == nullptr )
	{
		return -1;
	}
	else
	{
		cout << "Completed parse" << endl;
	}

#ifdef BLANG_HAS_LLVM
	// Code generation
	QLang::CodeGen codegen( argv[1] );

	if ( !codegen.generate( mod ) )
	{
		cerr << "Code generation failed" << endl;
		return -1;
	}

	if ( !codegen.verify() )
	{
		cerr << "Module verification failed" << endl;
		return -1;
	}

	// Print IR to stdout
	codegen.print( llvm::outs() );

	// Also write to .ll file
	std::string inputFile = argv[1];
	std::string outputFile = inputFile;
	size_t dot = outputFile.rfind( '.' );
	if ( dot != std::string::npos )
		outputFile = outputFile.substr( 0, dot );
	outputFile += ".ll";

	std::error_code ec;
	llvm::raw_fd_ostream outFile( outputFile, ec );
	if ( !ec )
	{
		codegen.print( outFile );
		cout << "Wrote IR to " << outputFile << endl;
	}
	else
	{
		cerr << "Failed to write " << outputFile << ": " << ec.message() << endl;
	}
#endif

	return 0;
}
