#include "DiagnosticEngine.h"

#include <sstream>
#include <cstdio>

namespace QLang
{

void DiagnosticEngine::report( const Diagnostic &diag )
{
	// Dedupe by (file,line,col,code): panic-mode recovery and repeated passes can
	// re-surface the same problem; one buffered copy is enough.
	for ( const Diagnostic &existing : mDiags )
	{
		if ( existing.location.file == diag.location.file &&
			 existing.location.line == diag.location.line &&
			 existing.location.col == diag.location.col &&
			 existing.code == diag.code )
			return;
	}

	// Cap the buffer so recovery cannot spew an unbounded phantom cascade.
	if ( mDiags.size() >= kMaxDiags )
	{
		mTruncated = true;
		return;
	}

	mDiags.push_back( diag );
}

void DiagnosticEngine::error( const SourceLocation &loc, const std::string &message,
                              const std::string &code )
{
	Diagnostic diag;
	diag.severity = Severity::Error;
	diag.location = loc;
	diag.message = message;
	diag.code = code;
	report( diag );
}

void DiagnosticEngine::warning( const SourceLocation &loc, const std::string &message,
                                const std::string &code )
{
	Diagnostic diag;
	diag.severity = Severity::Warning;
	diag.location = loc;
	diag.message = message;
	diag.code = code;
	report( diag );
}

void DiagnosticEngine::reportCompileError( const CompileError &err )
{
	Diagnostic diag;
	diag.severity = Severity::Error;
	diag.location = err.getLocation();
	diag.message = err.getMessage();
	diag.code = "syntax";
	report( diag );

	// Compiler-internal throw-site coordinates are for compiler developers and
	// appear only under --debug-compiler, rendered by finish() (human mode).
	if ( mDebugCompiler && mDebugTail.empty() )
	{
		std::ostringstream oss;
		oss << "[debug] thrown at " << err.getInternalFile() << ":"
			<< err.getInternalLine();
		mDebugTail = oss.str();
	}
}

Severity DiagnosticEngine::effectiveSeverity( const Diagnostic &d ) const
{
	if ( mWerror && d.severity == Severity::Warning )
		return Severity::Error;
	return d.severity;
}

bool DiagnosticEngine::hasErrors() const
{
	for ( const Diagnostic &d : mDiags )
	{
		if ( effectiveSeverity( d ) == Severity::Error )
			return true;
	}
	return false;
}

static const char *severityLabel( Severity s )
{
	return ( s == Severity::Error ) ? "error" : "warning";
}

// Minimal JSON string escaping for the fields we emit (message/file/code).
static std::string jsonEscape( const std::string &in )
{
	std::string out;
	out.reserve( in.size() + 2 );
	for ( char c : in )
	{
		switch ( c )
		{
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if ( static_cast<unsigned char>( c ) < 0x20 )
				{
					char buf[8];
					std::snprintf( buf, sizeof( buf ), "\\u%04x", c );
					out += buf;
				}
				else
				{
					out += c;
				}
		}
	}
	return out;
}

void DiagnosticEngine::renderHuman()
{
	for ( const Diagnostic &d : mDiags )
	{
		const SourceLocation &loc = d.location;
		// Human label reflects the diagnostic's own severity; -Werror changes the
		// exit code (hasErrors), not the printed label.
		mOut << loc.file << ":" << loc.line << ":" << loc.col
			<< ": " << severityLabel( d.severity ) << ": " << d.message << std::endl;

		for ( const Note &note : d.notes )
		{
			const SourceLocation &nloc = note.location;
			mOut << nloc.file << ":" << nloc.line << ":" << nloc.col
				<< ": note: " << note.message << std::endl;
		}
	}

	if ( mTruncated )
		mOut << "note: too many diagnostics; further reports suppressed" << std::endl;

	if ( !mDebugTail.empty() )
		mOut << mDebugTail << std::endl;
}

void DiagnosticEngine::renderJson()
{
	// A single JSON array; the sole content written to mOut in --json mode so a
	// `2>&1 | python3 -c json.load` consumer parses cleanly.
	mOut << "[";
	for ( std::size_t i = 0; i < mDiags.size(); i++ )
	{
		const Diagnostic &d = mDiags[i];
		if ( i > 0 )
			mOut << ",";
		mOut << "{"
			<< "\"severity\":\"" << severityLabel( d.severity ) << "\","
			<< "\"file\":\"" << jsonEscape( d.location.file ) << "\","
			<< "\"line\":" << d.location.line << ","
			<< "\"col\":" << d.location.col << ","
			<< "\"message\":\"" << jsonEscape( d.message ) << "\","
			<< "\"code\":\"" << jsonEscape( d.code ) << "\""
			<< "}";
	}
	mOut << "]" << std::endl;
}

void DiagnosticEngine::finish()
{
	if ( mFinished )
		return;
	mFinished = true;

	if ( mJson )
		renderJson();
	else
		renderHuman();
}

} // namespace QLang
