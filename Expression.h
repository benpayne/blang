
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

		// Typed AST (U3, design decision 3): the single authoritative record of
		// this expression's resolved type. The semantic pass (Sema) fills it for
		// expressions whose type is determinable from resolution; codegen reads
		// it instead of re-deriving on the paths U3 migrates. nullptr means the
		// type is not yet determined (a leaf a later unit will resolve/check) —
		// never a fabricated default.
		void setResolvedType( Type *t ) { mResolvedType = t; }
		Type *getResolvedType() const
		{
			return const_cast<Type *>( static_cast<const Type *>( mResolvedType ) );
		}

	protected:
		SmartPtr<Type> mResolvedType;  // NEW U3 slot; default nullptr
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
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
		friend class SQLGen;
	};

	class ConstFloat : public ConstExpression
	{
	public:
		ConstFloat( double value ) : mValue( value ) {}

	private:
		double mValue;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
		friend class SQLGen;
	};

	class ConstString : public ConstExpression
	{
	public:
		ConstString( std::string value ) : mValue( value ) {}

	private:
		std::string mValue;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
	};

	class CallExpression : public Expression
	{
	public:
		CallExpression( FunctionDefinition *def ) : mFunction( def ) {}

		static CallExpression *Parse( Lexer &l, Scope *scope );

		void addParam( Expression *param ) { mParams.push_back( param ); }
		void addTypeArg( Type *typeArg ) { mTypeArgs.push_back( typeArg ); }

		// Override the LLVM function name for namespace-mangled calls (e.g. "sys__args")
		void setMangledName( const std::string &name ) { mMangledName = name; }
		const std::string &getMangledName() const { return mMangledName; }

		FunctionDefinition *getFunction() { return mFunction; }

	private:
		SmartPtr<FunctionDefinition> mFunction;
		std::vector<SmartPtr<Type>> mTypeArgs;
		std::vector<SmartPtr<Expression> > mParams;
		std::string mMangledName;  // namespace-mangled name, empty if not mangled
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
	};
	
	class Block : public Statement
	{
	public:
		static Block *Parse( Lexer &l, Scope *block_scope );

	private:
		SmartPtr<Scope> mScope;
		std::vector<SmartPtr<Statement> > mStatementList;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
	};

	class FieldAssignmentExpression : public Expression
	{
	public:
		FieldAssignmentExpression( std::string operation, Expression *object,
			const std::string &fieldName, Expression *value ) :
			mOperation( operation ), mObject( object ), mFieldName( fieldName ), mValue( value ) {}

	private:
		std::string mOperation;
		SmartPtr<Expression> mObject;
		std::string mFieldName;
		SmartPtr<Expression> mValue;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
	};

	class IndexAssignmentExpression : public Expression
	{
	public:
		IndexAssignmentExpression( std::string operation, Expression *object,
			Expression *index, Expression *value ) :
			mOperation( operation ), mObject( object ), mIndex( index ), mValue( value ) {}

	private:
		std::string mOperation;
		SmartPtr<Expression> mObject;
		SmartPtr<Expression> mIndex;
		SmartPtr<Expression> mValue;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
	};

	class BreakStatement : public Statement
	{
	public:

		static BreakStatement *Parse( Lexer &l, Scope *scope );

	protected:
		BreakStatement() {}

	private:
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
	};

	class ContinueStatement : public Statement
	{
	public:

		static ContinueStatement *Parse( Lexer &l, Scope *scope );

	protected:
		ContinueStatement() {}

	private:
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
	};

	class SpawnStatement : public Expression
	{
	public:
		static SpawnStatement *Parse( Lexer &l, Scope *scope );

	protected:
		SpawnStatement() {}

	private:
		SmartPtr<Block> mBody;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
	};

	class WaitStatement : public Statement
	{
	public:
		static WaitStatement *Parse( Lexer &l, Scope *scope );

	protected:
		WaitStatement() {}

	private:
		SmartPtr<Expression> mExpr;   // the Task expression to wait on
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
	};

	class WaitAllStatement : public Statement
	{
	public:
		static WaitAllStatement *Parse( Lexer &l, Scope *scope );

	protected:
		WaitAllStatement() {}

	private:
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
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
		int mLine = 0;         // source line of the assert (for failure reports)
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
	};

	class FieldAccessExpression : public Expression
	{
	public:
		FieldAccessExpression( Expression *object, const std::string &fieldName ) :
			mObject( object ), mFieldName( fieldName ) {}

		static FieldAccessExpression *Parse( Lexer &l, Scope *scope );

		Expression *getObject() { return mObject; }
		const std::string &getFieldName() const { return mFieldName; }

		// The struct field this access resolved to (stamped by Sema; null when
		// the base type is not a resolvable struct). Non-owning: the struct
		// definition owns its fields.
		VariableDefinition *getResolvedField() { return mResolvedField; }

	private:
		SmartPtr<Expression> mObject;
		std::string mFieldName;
		VariableDefinition *mResolvedField = nullptr;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
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

		void addTypeArg( Type *typeArg ) { mTypeArgs.push_back( typeArg ); }

		static StructLiteralExpression *Parse( Lexer &l, Scope *scope, const std::string &typeName );

	private:
		std::string mTypeName;
		std::vector<SmartPtr<Type>> mTypeArgs;
		std::vector<std::string> mFieldNames;
		std::vector<SmartPtr<Expression>> mFieldValues;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
	};

	class IndexExpression : public Expression
	{
	public:
		IndexExpression( Expression *object, Expression *index ) :
			mObject( object ), mIndex( index ) {}

		Expression *getObject() { return mObject; }
		Expression *getIndex() { return mIndex; }

	private:
		SmartPtr<Expression> mObject;
		SmartPtr<Expression> mIndex;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
	};

	class MethodCallExpression : public Expression
	{
	public:
		MethodCallExpression( Expression *object, const std::string &methodName ) :
			mObject( object ), mMethodName( methodName ) {}

		void addArg( Expression *arg ) { mArgs.push_back( arg ); }

		Expression *getObject() { return mObject; }
		const std::string &getMethodName() const { return mMethodName; }

		// The struct method this call resolved to (stamped by Sema; null for
		// builtin string/array methods, channel ops, and unresolvable bases).
		// Non-owning: the struct definition owns its methods.
		FunctionDefinition *getResolvedMethod() { return mResolvedMethod; }

	private:
		SmartPtr<Expression> mObject;
		std::string mMethodName;
		FunctionDefinition *mResolvedMethod = nullptr;
		std::vector<SmartPtr<Expression>> mArgs;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
	};

	class ConstructExpression : public Expression
	{
	public:
		ConstructExpression( StructDefinition *structDef ) : mStructDef( structDef ) {}
		void addArg( Expression *arg ) { mArgs.push_back( arg ); }
		void addTypeArg( Type *typeArg ) { mTypeArgs.push_back( typeArg ); }

	private:
		SmartPtr<StructDefinition> mStructDef;
		std::vector<SmartPtr<Type>> mTypeArgs;
		std::vector<SmartPtr<Expression>> mArgs;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
	};

	struct MatchArm
	{
		std::string mPattern;
		// Variables bound by destructuring, in payload order: ok(value) binds
		// one, pair(a, b) binds two — one per variant associated type.
		std::vector<std::string> mBindingNames;
		SmartPtr<Block> mBody;    // statement-form arm: pattern { statements... }
		SmartPtr<Expression> mValue; // expression-form arm: pattern { expression }
		SmartPtr<Scope> mScope;   // expression-form arm scope (holds the binding)
		bool mIsWildcard = false;
		bool mPatternIsString = false; // pattern was a string literal ("start")
	};

	class MatchExpression : public Expression
	{
	public:

		// exprMode: value-producing match (parsed from expression position) —
		// each arm body is a single expression, and the match yields the
		// selected arm's value. Statement position keeps block-bodied arms.
		static MatchExpression *Parse( Lexer &l, Scope *scope, bool exprMode = false );

	protected:
		MatchExpression() {}

	private:
		SmartPtr<Expression> mSubject;
		std::vector<MatchArm> mArms;
		bool mExprMode = false;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
	};

	class TryExpression : public Expression
	{
	public:
		TryExpression( Expression *operand ) : mOperand( operand ) {}

	private:
		SmartPtr<Expression> mOperand;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
	};

	class AwaitExpression : public Expression
	{
	public:
		AwaitExpression( Expression *operand ) : mOperand( operand ) {}

	private:
		SmartPtr<Expression> mOperand;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
	};

	class FunctionRefExpression : public Expression
	{
	public:
		FunctionRefExpression( FunctionDefinition *func ) : mFunction( func ) {}

	private:
		SmartPtr<FunctionDefinition> mFunction;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
	};

	class LambdaExpression : public Expression
	{
	public:
		static LambdaExpression *Parse( Lexer &l, Scope *scope );

	protected:
		LambdaExpression() {}

	private:
		SmartPtr<Type> mReturnType;  // nullptr = void
		std::vector<SmartPtr<VariableDefinition>> mParameters;
		SmartPtr<Block> mBody;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
	};

	class IndirectCallExpression : public Expression
	{
	public:
		IndirectCallExpression( VariableDefinition *fnVar ) : mFnVariable( fnVar ) {}

		void addParam( Expression *param ) { mParams.push_back( param ); }

	private:
		SmartPtr<VariableDefinition> mFnVariable;
		std::vector<SmartPtr<Expression>> mParams;
		friend class CodeGen;
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
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
		friend class LocationDumper;
		friend class Sema;
		friend class SQLGen;
	};
};

#endif // BLANG_EXPRESSION_H_
