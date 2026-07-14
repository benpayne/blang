#include "DiagnosticEngine.h"

namespace QLang
{

void DiagnosticEngine::report( const Diagnostic &diag )
{
	// Canonical line (always first): <file>:<line>:<col>: error: <message>.
	// The location prefix is added here, never stored in diag.message.
	const SourceLocation &loc = diag.location;
	mOut << loc.file << ":" << loc.line << ":" << loc.col
		<< ": error: " << diag.message << std::endl;

	// Notes render as subsequent "note:" lines. None are produced in U2.
	for ( const Note &note : diag.notes )
	{
		const SourceLocation &nloc = note.location;
		mOut << nloc.file << ":" << nloc.line << ":" << nloc.col
			<< ": note: " << note.message << std::endl;
	}

	mHasErrors = true;
}

void DiagnosticEngine::error( const SourceLocation &loc, const std::string &message )
{
	Diagnostic diag;
	diag.severity = Severity::Error;
	diag.location = loc;
	diag.message = message;
	report( diag );
}

void DiagnosticEngine::reportCompileError( const CompileError &err )
{
	Diagnostic diag;
	diag.severity = Severity::Error;
	diag.location = err.getLocation();
	diag.message = err.getMessage();
	report( diag );

	// Compiler-internal throw-site coordinates are for compiler developers and
	// appear only under --debug-compiler, after the canonical error line.
	if ( mDebugCompiler )
	{
		mOut << "[debug] thrown at " << err.getInternalFile() << ":"
			<< err.getInternalLine() << std::endl;
	}
}

} // namespace QLang
