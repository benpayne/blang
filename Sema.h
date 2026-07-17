#ifndef BLANG_SEMA_H_
#define BLANG_SEMA_H_

#include <string>
#include <set>
#include <map>

#include "Type.h"
#include "Expression.h"
#include "DiagnosticEngine.h"

namespace QLang
{
	// The semantic-analysis pass (U3). Runs between Module::Parse and code
	// generation, in ALL build configurations (no BLANG_HAS_LLVM guard) so
	// --parse-only becomes "parse + sema". It walks a parsed module's AST,
	// resolves struct field/method references against the base expression's
	// resolved type, annotates every determinable expression with its resolved
	// type (the single shared typed-AST representation codegen reads), and
	// reports located errors through the U2 DiagnosticEngine.
	//
	// Scope split (research R2): the parser already resolves variables and
	// functions eagerly, throwing located CompileErrors before sema runs. Sema
	// owns member (field/method) resolution and type annotation only; it never
	// re-reports the parser's var/func errors (FR-009). It adds no type-CHECKING
	// rule (arity/compat/coercion/ownership/concurrency) — those are U4–U7.
	class Sema
	{
	public:
		// Analyze one parsed, non-extern module. Returns true iff no semantic
		// diagnostic was reported (the driver skips codegen and exits non-zero
		// on false). Extern .bmod modules are not analyzed (contracts/sema-pass).
		static bool analyze( Module *module, Scope *scope, DiagnosticEngine &diag );

	private:
		Sema( Scope *scope, DiagnosticEngine &diag ) : mScope( scope ), mDiag( diag ) {}

		void visitFunction( FunctionDefinition *func );
		void visitStruct( StructDefinition *structDef );
		void visitStmt( Statement *stmt );

		// Resolve/annotate an expression bottom-up. Returns its resolved Type
		// (best-effort, from resolution only), or nullptr when the type is not
		// determinable in this unit. Also stamps the node via setResolvedType.
		Type *visitExpr( Expression *expr );

		// Member resolution helpers.
		void resolveFieldAccess( FieldAccessExpression *fa, Type *baseType );
		void resolveMethodCall( MethodCallExpression *mc, Type *baseType );

		// Database query/insert/update/delete field validation (located, all
		// build modes). tableStructFor resolves the `table struct` (located error
		// for unknown/non-table); checkTableField reports an unknown column;
		// validateTableSteps walks where/order/set pipeline steps.
		StructDefinition *tableStructFor( const std::string &tableName, Expression *node );
		void checkTableField( StructDefinition *table, const std::string &field,
			const SourceLocation &loc );
		void collectQueryFieldExprs( const Expression *e,
			std::vector<const QueryFieldExpression *> &out );
		void validateTableSteps( const std::string &tableName,
			const std::vector<QueryPipelineStep> &steps, Expression *node );

		// The concrete user struct a base type names, or nullptr when the base
		// is a builtin (string/Array/Buffer/…), a generic parameter, an enum, an
		// imported/namespaced symbol we cannot see, or otherwise not a concrete
		// struct in scope. A nullptr result means "leave unchecked" (FR-008/R5):
		// sema never fabricates an "unknown member" error on such bases.
		StructDefinition *structForType( Type *baseType );

		// U4 type-compatibility (members: use mScope to classify a type).
		bool isCheckableType( Type *t );
		bool typesCompatible( Type *from, Type *to );

		// U5 match/generics helpers.
		EnumDefinition *enumForType( Type *t );
		void checkConstraint( Type *arg, const std::string &constraint,
			const std::string &paramName, const SourceLocation &loc );

		// U7 spawn-capture helpers.
		bool isHeapType( Type *t );
		void collectSpawnRefs( Statement *stmt,
			std::set<VariableDefinition*> &refs,
			std::set<VariableDefinition*> &locals );

		Scope *mScope;
		DiagnosticEngine &mDiag;
		bool mReported = false;  // any sema diagnostic emitted this run
		FunctionDefinition *mCurrentFunc = nullptr;  // U4: enclosing fn for return checks
		// U6 ownership/move analysis (bounded flow analysis, per function).
		std::set<VariableDefinition*> mMoved;
		std::map<VariableDefinition*, int> mDeclLoopDepth;
		std::map<VariableDefinition*, int> mDeclSpawnDepth;
		int mLoopDepth = 0;
		int mSpawnDepth = 0;
	};

} // namespace QLang

#endif // BLANG_SEMA_H_
