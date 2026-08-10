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
		Sema( Scope *scope, DiagnosticEngine &diag, const std::string &moduleId = "" )
			: mScope( scope ), mDiag( diag ), mModuleId( moduleId ) {}

		// U6b-3 (DC9/KI-23): identity (source-file basename) of the module being
		// analyzed — the USE-SITE module. A field access on a struct whose defining
		// module differs is a private-field reach-in across a module boundary.
		std::string mModuleId;

		void visitFunction( FunctionDefinition *func );
		void visitStruct( StructDefinition *structDef );

		// A declaration without a body is an INTERFACE form: it is what a .bmod
		// carries so a consumer can resolve an imported type's constructor and
		// methods. In ordinary source it would silently codegen an empty
		// function returning zero, so it is rejected here, located, in all build
		// modes. Protocol requirements are bodyless by design and never reach
		// this check.
		void checkBodylessMember( FunctionDefinition *func, const std::string &ownerName );

		// The compiler reserves the "__" symbol family for names it synthesizes
		// (__<Struct>_dtor, __<Struct>_new, __enum_<Name>_box_dtor, ...). A
		// source declaration that mangles into that family could collide with
		// one of them, so it is rejected. `extern fn` is exempt: it names a
		// foreign C symbol rather than creating a BLang one.
		void checkReservedName( const std::string &name, const SourceLocation &loc,
			const std::string &kind );

		// P9 (design record): an exported declaration may only reference
		// exported types. Enforced at the LIBRARY build, located at the
		// offending declaration.
		//
		// Without this the emitter writes a reference to a type it never
		// declares, the library exits 0, and the failure lands on the CONSUMER
		// as a syntax error inside a generated file they never wrote. That is
		// the wrong error, in the wrong file, at the wrong time, shown to the
		// wrong person.
		//
		// `what` names the offending position for the diagnostic
		// ("parameter of pub fn 'x'", "field of @json struct 'T'", ...).
		void checkExportedTypeRef( const Type *type, const SourceLocation &loc,
			const std::string &what );
		void checkExportedSignature( FunctionDefinition *func,
			const std::string &what );
		bool isExportedDataContract( StructDefinition *structDef ) const;
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
		// Builtin to_json(value): require a @json-annotated struct argument
		// (located error, all build modes).
		void validateToJsonArg( CallExpression *call );

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

		// U1 (diagnostics): unused-local-variable lint, per function. Collect
		// declared locals and every referenced/assigned variable name; warn on a
		// local never mentioned again. Name-based and conservative — a name used
		// anywhere in the function suppresses the warning, so it only ever misses
		// warnings, never false-positives a genuinely used variable.
		std::vector<VariableDefinition*> mLocalDecls;
		std::set<std::string> mReferencedNames;

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

		// Owns the synthesized `int` type assigned to range-loop variables
		// (setVariableType stores a SmartPtr, but keep an owner for clarity).
		SmartPtr<Type> mIntType;
	};

} // namespace QLang

#endif // BLANG_SEMA_H_
