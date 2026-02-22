
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

		// Precedence-climbing expression parser (no terminal handling)
		static Expression *ParseExpr( Lexer &l, Scope *scope, int minPrec = 0 );
		static Expression *ParsePrimary( Lexer &l, Scope *scope );

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
		friend class CodeGen;
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
		friend class CodeGen;
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
		friend class CodeGen;
	};

	class ReturnStatement : public Statement
	{
	public:

		static ReturnStatement *Parse( Lexer &l, Scope *scope );

	protected:
		ReturnStatement() {}

	private:
		SmartPtr<Expression> mExpression;
		friend class CodeGen;
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
		friend class CodeGen;
	};

	class ConstFloat : public ConstExpression
	{
	public:
		ConstFloat( double value ) : mValue( value ) {}

	private:
		double mValue;
		friend class CodeGen;
	};
	
	class ConstString : public ConstExpression
	{
	public:
		ConstString( std::string value ) : mValue( value ) {}

	private:
		std::string mValue;
		friend class CodeGen;
	};
	
	class ConstChar : public ConstExpression
	{
	public:
		ConstChar( const std::string &value ) : mValue( value ) {}

		static ConstChar *Parse( Lexer &l, Scope *scope );


	protected:
		ConstChar() {}

	private:
		std::string mValue;
		friend class CodeGen;
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
		friend class CodeGen;
	};
	
	class VariableExpression : public Expression
	{
	public:
		VariableExpression( VariableDefinition *def ) : mVariable( def ) {}

		static VariableExpression *Parse( Lexer &l, Scope *scope );

		VariableDefinition *getVariable() { return mVariable; }

	protected:
		VariableExpression() {}
		
	private:
		SmartPtr<VariableDefinition> mVariable;
		friend class CodeGen;
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
		friend class CodeGen;
	};
	
	class Block : public Statement
	{
	public:
		static Block *Parse( Lexer &l, Scope *block_scope );

	private:
		SmartPtr<Scope> mScope;
		std::vector<SmartPtr<Statement> > mStatementList;
		friend class CodeGen;
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
		friend class CodeGen;
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
		friend class CodeGen;
	};

	class UnaryExpression : public Expression
	{
	public:
		UnaryExpression( std::string operation, Expression *operand ) :
			mOperation( operation ), mOperand( operand ) {}

	private:
		std::string mOperation;
		SmartPtr<Expression> mOperand;
		friend class CodeGen;
	};

	class BreakStatement : public Statement
	{
	public:

		static BreakStatement *Parse( Lexer &l, Scope *scope );

	protected:
		BreakStatement() {}

	private:
		friend class CodeGen;
	};

	class ContinueStatement : public Statement
	{
	public:

		static ContinueStatement *Parse( Lexer &l, Scope *scope );

	protected:
		ContinueStatement() {}

	private:
		friend class CodeGen;
	};

	class FieldAccessExpression : public Expression
	{
	public:

		static FieldAccessExpression *Parse( Lexer &l, Scope *scope );

	protected:
		FieldAccessExpression() {}

	private:
		SmartPtr<Expression> mObject;
		std::string mFieldName;
		friend class CodeGen;
	};

	struct MatchArm
	{
		std::string mPattern;
		SmartPtr<Block> mBody;
	};

	class MatchExpression : public Expression
	{
	public:

		static MatchExpression *Parse( Lexer &l, Scope *scope );

	protected:
		MatchExpression() {}

	private:
		SmartPtr<Expression> mSubject;
		std::vector<MatchArm> mArms;
		friend class CodeGen;
	};
};

#endif // BLANG_EXPRESSION_H_
