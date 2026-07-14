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

// The only severity emitted in U2. The enum exists so a future diagnostics
// epic can add Warning/Note without changing the engine API. No behavior keys
// off severity yet beyond rendering the literal "error:".
enum class Severity { Error };

// A secondary located remark attached to a diagnostic (e.g. a future
// "moved here" note). Defined but unused in U2 — the notes vector is always
// empty. Present so the API does not need to change later.
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
	std::vector<Note> notes;      // empty in U2
};

// The single diagnostic reporting path, owned by the compiler driver (one
// instance per process, shared across modules in --combine). Both the parser
// and the future semantic pass (U3+) render through this class so the
// user-facing format is defined in exactly one place.
class DiagnosticEngine
{
public:
	DiagnosticEngine() : mOut( std::cerr ) {}
	explicit DiagnosticEngine( std::ostream &out ) : mOut( out ) {}

	// Render one diagnostic in the canonical form
	// "<file>:<line>:<col>: error: <message>" and set the error flag.
	void report( const Diagnostic &diag );

	// Convenience: build an Error diagnostic with no notes and report it.
	void error( const SourceLocation &loc, const std::string &message );

	// Adapter used by the top-level parse-catch: renders a CompileError's
	// located message body; under --debug-compiler it also emits the C++
	// throw-site coordinates.
	void reportCompileError( const CompileError &err );

	bool hasErrors() const { return mHasErrors; }
	void setDebugCompiler( bool enabled ) { mDebugCompiler = enabled; }

private:
	std::ostream &mOut;
	bool mHasErrors = false;
	bool mDebugCompiler = false;
};

} // namespace QLang

#endif // DIAGNOSTIC_ENGINE_H_
