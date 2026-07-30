#ifndef DIAGNOSTIC_ENGINE_H_
#define DIAGNOSTIC_ENGINE_H_

#include <iostream>
#include <string>
#include <vector>

#include "SourceLocation.h"
#include "RefCount.h"        // SmartPtr — CompilerHelpers.h's operator<< needs it
#include "CompilerHelpers.h"

namespace QLang
{

// Diagnostic severities. Warning < Error so `severity >= Error` gates the error
// flag / exit code; `-Werror` promotes Warning to Error at finish() time. Room
// is kept for a future Note severity without changing the API.
enum class Severity { Warning, Error };

// A secondary located remark attached to a diagnostic (e.g. a future
// "moved here" note). The notes vector is currently always empty; present so
// the API does not need to change later.
struct Note
{
	SourceLocation location;
	std::string message;
};

struct Diagnostic
{
	Severity severity = Severity::Error;
	SourceLocation location;
	std::string message;          // body only — no location prefix, no C++ coords
	std::string code = "error";   // stable machine-readable class slug (e.g.
	                              // "syntax", "sema", "unused-variable")
	std::vector<Note> notes;
};

// The single diagnostic reporting path, owned by the compiler driver (one
// instance per process, shared across modules in --combine). U1 makes it a
// COLLECTOR: report() buffers diagnostics; finish() renders them all once, as
// human text (default) or a JSON array (--json). Both the parser (via
// reportCompileError) and the semantic pass (via error()/warning()) buffer here,
// so the user-facing format — and the machine-readable schema — are defined in
// exactly one place, and one compile can report many errors.
class DiagnosticEngine
{
public:
	DiagnosticEngine() : mOut( std::cerr ) {}
	explicit DiagnosticEngine( std::ostream &out ) : mOut( out ) {}

	// Buffer one diagnostic. Deduplicates by (file,line,col,code) and caps the
	// total (kMaxDiags) so panic-mode recovery cannot spew a phantom cascade.
	void report( const Diagnostic &diag );

	// Convenience builders. `error` defaults to the "sema" class (its callers are
	// the semantic pass); `warning` requires an explicit class slug.
	void error( const SourceLocation &loc, const std::string &message,
	            const std::string &code = "sema" );
	void warning( const SourceLocation &loc, const std::string &message,
	              const std::string &code );

	// Adapter used by the top-level parse-catch: buffers a CompileError's located
	// message body under the "syntax" class. Under --debug-compiler the C++
	// throw-site coordinates are attached (rendered by finish() in human mode).
	void reportCompileError( const CompileError &err );

	// Render all buffered diagnostics once (human text or JSON). Idempotent: a
	// second call is a no-op. Applies -Werror promotion. Called by the driver
	// after parse+sema of all files, before codegen.
	void finish();

	// True if, after -Werror promotion, any buffered diagnostic is an error.
	bool hasErrors() const;

	void setDebugCompiler( bool enabled ) { mDebugCompiler = enabled; }
	void setJson( bool enabled ) { mJson = enabled; }
	void setWerror( bool enabled ) { mWerror = enabled; }

	// Number of buffered diagnostics (test/introspection helper).
	std::size_t diagnosticCount() const { return mDiags.size(); }

	// Read the buffered diagnostics programmatically (the LSP server publishes
	// these directly instead of rendering via finish()).
	const std::vector<Diagnostic> &diagnostics() const { return mDiags; }

private:
	static const std::size_t kMaxDiags = 50;

	// Effective severity after -Werror promotion.
	Severity effectiveSeverity( const Diagnostic &d ) const;
	void renderHuman();
	void renderJson();

	std::ostream &mOut;
	std::vector<Diagnostic> mDiags;
	bool mDebugCompiler = false;
	bool mJson = false;
	bool mWerror = false;
	bool mFinished = false;
	bool mTruncated = false;      // set when kMaxDiags was exceeded
	std::string mDebugTail;       // --debug-compiler throw-site line, if any
};

} // namespace QLang

#endif // DIAGNOSTIC_ENGINE_H_
