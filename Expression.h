
#ifndef BLANG_EXPRESSION_H_
#define BLANG_EXPRESSION_H_

#include <string>
#include <map>
#include <vector>

#include "RefCount.h"

#include "Type.h"

class Lexer;

namespace QLang
{
	class Expression : public Statement
	{
	public:
		static Expression *Parse( Lexer &l, Scope *scope, char terminal = ';' );
		
		static Expression *ParseLValue( Lexer &l, Scope *scope );
		static Expression *ParseRValue( Lexer &l, Scope *scope );

	protected:
	};
	
	class WhileStatement : public Statement
	{
	public:

		static WhileStatement *Parse( Lexer &l, Scope *scope );

	protected:
		WhileStatement() {}	
		
	private:
		SmartPtr<Expression> mLoopExpression;
		SmartPtr<Statement> mLoopStatement;
	};

	class ForStatement : public Statement
	{
	public:

		static ForStatement *Parse( Lexer &l, Scope *scope );

	protected:
		ForStatement() {}	

	private:
		SmartPtr<Expression> mInitialExpression;
		SmartPtr<Expression> mTestExpression;
		SmartPtr<Expression> mIterationExpression;
		SmartPtr<Statement> mStatement;
	};

	class IfStatement : public Statement
	{
	public:

		static IfStatement *Parse( Lexer &l, Scope *scope );

	protected:
		IfStatement() {}	

	private:
		SmartPtr<Expression> mIfExpression;
		SmartPtr<Statement> mStatement;
		SmartPtr<Statement> mElseStatement;
	};

	class ReturnStatement : public Statement
	{
	public:

		static ReturnStatement *Parse( Lexer &l, Scope *scope );

	protected:
		ReturnStatement() {}	

	private:
		SmartPtr<Expression> mExpression;
	};
	
	class BinaryExpression : public Expression
	{
	public:
		
		BinaryExpression *ParseAssignment( Lexer &l, Scope *scope );
		BinaryExpression *ParseArithmatic( Lexer &l, Scope *scope );
		
	private:
		
	};
	
	class ConstExpression : public Expression
	{
	public:
		static ConstExpression *Parse( Lexer &l, Scope *scope );

	protected:
		ConstExpression() {}
		
	private:
		SmartPtr<Type> mType;
	};

	class ConstInteger : public ConstExpression
	{
	public:
		ConstInteger( int64_t value ) : mValue( value ) {}
		
	private:
		int64_t mValue;
	};

	class ConstFloat : public ConstExpression
	{
	public:
		ConstFloat( double value ) : mValue( value ) {}
		
	private:
		double mValue;
	};
	
	class ConstString : public ConstExpression
	{
	public:
		ConstString( std::string value ) : mValue( value ) {}

	private:
		std::string mValue;
	};
	
	class ConstChar : public ConstExpression
	{
	public:
		static ConstChar *Parse( Lexer &l, Scope *scope );

		
	protected:
		ConstChar() {}
		
	private:
		uint16_t mValue;
	};
	
	class VariableDeclaration : public Statement
	{
	public:
		static VariableDeclaration *Parse( Lexer &l, Scope *s );
		
	private:
		struct DeclData {
			SmartPtr<VariableDefinition> mVaribale;
			SmartPtr<Expression> mInitialValue;
		};
		
		std::vector<DeclData> mVariables;
	};
	
	class VariableExpression : public Expression
	{
	public:
		VariableExpression( VariableDefinition *def ) : mVariable( def ) {}

		static VariableExpression *Parse( Lexer &l, Scope *scope );
		
	protected:
		VariableExpression() {}
		
	private:
		SmartPtr<VariableDefinition> mVariable;
	};

	class CallExpression : public Expression
	{
	public:
		static CallExpression *Parse( Lexer &l, Scope *scope );

	protected:
		CallExpression( FunctionDefinition *def ) : mFunction( def ) {}
		
	private:
		SmartPtr<FunctionDefinition> mFunction;
		std::vector<SmartPtr<Expression> > mParams;
	};
	
	class Block : public Statement
	{
	public:
		static Block *Parse( Lexer &l, Scope *block_scope );
		
	private:
		SmartPtr<Scope> mScope;
		std::vector<SmartPtr<Statement> > mStatementList;
	};

	class AssignmentExpression : public Expression
	{
	public:
		AssignmentExpression( std::string operation, VariableDefinition *var, Expression *value ) :
			mOperation( operation ), mVariable( var ), mValue( value ) {}
	
	private:
		std::string mOperation;
		SmartPtr<VariableDefinition> mVariable;
		SmartPtr<Expression> mValue;
	};
	
	class OperationsExpression : public Expression
	{
	public:
		OperationsExpression( std::string operation, Expression *op1, Expression *op2 ) :
			mOperation( operation ), mOp1( op1 ), mOp2( op2 ) {}
	
	private:
		std::string mOperation;
		SmartPtr<Expression> mOp1;
		SmartPtr<Expression> mOp2;
	};
};

#endif // BLANG_EXPRESSION_H_
