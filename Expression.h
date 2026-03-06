
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
	class SQLGen;  // forward declaration for friend access

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

	class ForInStatement : public Statement
	{
	public:

		static ForInStatement *Parse( Lexer &l, Scope *scope );

	protected:
		ForInStatement() {}

	private:
		std::string mVariableName;
		std::string mSecondVariableName; // for key, value iteration
		SmartPtr<Expression> mIterableExpression; // nullptr for infinite loop
		SmartPtr<Statement> mBody;
		bool mIsInfinite = false;
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
		friend class SQLGen;
	};

	class ConstFloat : public ConstExpression
	{
	public:
		ConstFloat( double value ) : mValue( value ) {}

	private:
		double mValue;
		friend class CodeGen;
		friend class SQLGen;
	};

	class ConstString : public ConstExpression
	{
	public:
		ConstString( std::string value ) : mValue( value ) {}

	private:
		std::string mValue;
		friend class CodeGen;
		friend class SQLGen;
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
		CallExpression( FunctionDefinition *def ) : mFunction( def ) {}

		static CallExpression *Parse( Lexer &l, Scope *scope );

		void addParam( Expression *param ) { mParams.push_back( param ); }

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
		friend class SQLGen;
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

	class SpawnStatement : public Statement
	{
	public:
		static SpawnStatement *Parse( Lexer &l, Scope *scope );

	protected:
		SpawnStatement() {}

	private:
		SmartPtr<Block> mBody;
		friend class CodeGen;
	};

	class AssertStatement : public Statement
	{
	public:
		static AssertStatement *Parse( Lexer &l, Scope *scope );

	protected:
		AssertStatement() {}

	private:
		SmartPtr<Expression> mExpression;
		std::string mMessage;  // optional assertion message
		friend class CodeGen;
	};

	class EventHandler : public Statement
	{
	public:
		static EventHandler *Parse( Lexer &l, Scope *scope );

	protected:
		EventHandler() {}

	private:
		SmartPtr<Expression> mEventExpression;  // e.g., timer.every(1000)
		SmartPtr<Block> mBody;
		friend class CodeGen;
	};

	class FieldAccessExpression : public Expression
	{
	public:
		FieldAccessExpression( Expression *object, const std::string &fieldName ) :
			mObject( object ), mFieldName( fieldName ) {}

		static FieldAccessExpression *Parse( Lexer &l, Scope *scope );

	private:
		SmartPtr<Expression> mObject;
		std::string mFieldName;
		friend class CodeGen;
	};

	class StructLiteralExpression : public Expression
	{
	public:
		StructLiteralExpression( const std::string &typeName ) : mTypeName( typeName ) {}

		void addField( const std::string &name, Expression *value )
		{
			mFieldNames.push_back( name );
			mFieldValues.push_back( value );
		}

		static StructLiteralExpression *Parse( Lexer &l, Scope *scope, const std::string &typeName );

	private:
		std::string mTypeName;
		std::vector<std::string> mFieldNames;
		std::vector<SmartPtr<Expression>> mFieldValues;
		friend class CodeGen;
	};

	class ArrayLiteralExpression : public Expression
	{
	public:
		ArrayLiteralExpression() {}

		void addElement( Expression *elem ) { mElements.push_back( elem ); }

		static ArrayLiteralExpression *Parse( Lexer &l, Scope *scope );

	private:
		std::vector<SmartPtr<Expression>> mElements;
		friend class CodeGen;
	};

	class IndexExpression : public Expression
	{
	public:
		IndexExpression( Expression *object, Expression *index ) :
			mObject( object ), mIndex( index ) {}

	private:
		SmartPtr<Expression> mObject;
		SmartPtr<Expression> mIndex;
		friend class CodeGen;
	};

	class MethodCallExpression : public Expression
	{
	public:
		MethodCallExpression( Expression *object, const std::string &methodName ) :
			mObject( object ), mMethodName( methodName ) {}

		void addArg( Expression *arg ) { mArgs.push_back( arg ); }

	private:
		SmartPtr<Expression> mObject;
		std::string mMethodName;
		std::vector<SmartPtr<Expression>> mArgs;
		friend class CodeGen;
	};

	class RangeExpression : public Expression
	{
	public:
		RangeExpression( Expression *start, Expression *end ) :
			mStart( start ), mEnd( end ) {}

	private:
		SmartPtr<Expression> mStart;
		SmartPtr<Expression> mEnd;
		friend class CodeGen;
	};

	class StringInterpolation : public Expression
	{
	public:
		StringInterpolation() {}

		void addPart( Expression *part ) { mParts.push_back( part ); }

		static StringInterpolation *Parse( Lexer &l, Scope *scope, const std::string &rawString );

	private:
		std::vector<SmartPtr<Expression>> mParts;
		friend class CodeGen;
	};

	class EnumConstructExpression : public Expression
	{
	public:
		EnumConstructExpression( EnumDefinition *enumDef, int variantIndex )
			: mEnumDef( enumDef ), mVariantIndex( variantIndex ) {}

		void addArg( Expression *arg ) { mArgs.push_back( arg ); }

	private:
		SmartPtr<EnumDefinition> mEnumDef;
		int mVariantIndex;
		std::vector<SmartPtr<Expression>> mArgs;
		friend class CodeGen;
	};

	struct MatchArm
	{
		std::string mPattern;
		std::string mBindingName; // variable bound by destructuring (e.g., ok(value) binds "value")
		SmartPtr<Block> mBody;
		bool mIsWildcard = false;
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

	class TryExpression : public Expression
	{
	public:
		TryExpression( Expression *operand ) : mOperand( operand ) {}

	private:
		SmartPtr<Expression> mOperand;
		friend class CodeGen;
	};

	class AwaitExpression : public Expression
	{
	public:
		AwaitExpression( Expression *operand ) : mOperand( operand ) {}

	private:
		SmartPtr<Expression> mOperand;
		friend class CodeGen;
	};

	// Phase 3: Pipeline expression — a |> b desugars to b(a)
	class PipelineExpression : public Expression
	{
	public:
		PipelineExpression( Expression *input, Expression *transform ) :
			mInput( input ), mTransform( transform ) {}

	private:
		SmartPtr<Expression> mInput;
		SmartPtr<Expression> mTransform;
		friend class CodeGen;
	};

	// Phase 3: Query field reference — .field_name in query context
	class QueryFieldExpression : public Expression
	{
	public:
		QueryFieldExpression( const std::string &fieldName ) : mFieldName( fieldName ) {}

		const std::string &getFieldName() const { return mFieldName; }

	private:
		std::string mFieldName;
		friend class CodeGen;
	};

	// Phase 3: Query pipeline step types
	struct QueryPipelineStep
	{
		enum StepType { WHERE, ORDER_BY, LIMIT, JOIN, FIRST, SET };

		StepType mType;
		SmartPtr<Expression> mExpression;         // predicate/sort key/limit count/join condition
		std::string mJoinTable;                    // table name for join steps
		std::vector<std::pair<std::string, SmartPtr<Expression>>> mSetFields; // for set steps
	};

	// Phase 3: Query expression — query T |> where { ... } |> order_by { ... } |> limit(n)
	class QueryExpression : public Expression
	{
	public:
		QueryExpression( const std::string &tableName ) : mTableName( tableName ) {}

		static QueryExpression *Parse( Lexer &l, Scope *scope );

		void addStep( const QueryPipelineStep &step ) { mSteps.push_back( step ); }

	private:
		std::string mTableName;
		std::vector<QueryPipelineStep> mSteps;
		friend class CodeGen;
		friend class SQLGen;
	};

	// Phase 3: Insert expression — insert T { field: value, ... }
	class InsertExpression : public Expression
	{
	public:
		InsertExpression( const std::string &tableName ) : mTableName( tableName ) {}

		static InsertExpression *Parse( Lexer &l, Scope *scope );

		void addField( const std::string &name, Expression *value )
		{
			mFieldNames.push_back( name );
			mFieldValues.push_back( value );
		}

	private:
		std::string mTableName;
		std::vector<std::string> mFieldNames;
		std::vector<SmartPtr<Expression>> mFieldValues;
		friend class CodeGen;
		friend class SQLGen;
	};

	// Phase 3: Update expression — update T |> where { ... } |> set { ... }
	class UpdateExpression : public Expression
	{
	public:
		UpdateExpression( const std::string &tableName ) : mTableName( tableName ) {}

		static UpdateExpression *Parse( Lexer &l, Scope *scope );

		void addStep( const QueryPipelineStep &step ) { mSteps.push_back( step ); }

	private:
		std::string mTableName;
		std::vector<QueryPipelineStep> mSteps;
		friend class CodeGen;
		friend class SQLGen;
	};

	// Phase 3: Delete expression — delete T |> where { ... }
	class DeleteExpression : public Expression
	{
	public:
		DeleteExpression( const std::string &tableName ) : mTableName( tableName ) {}

		static DeleteExpression *Parse( Lexer &l, Scope *scope );

		void addStep( const QueryPipelineStep &step ) { mSteps.push_back( step ); }

	private:
		std::string mTableName;
		std::vector<QueryPipelineStep> mSteps;
		friend class CodeGen;
		friend class SQLGen;
	};
};

#endif // BLANG_EXPRESSION_H_
