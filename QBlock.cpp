#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"
#include "DiagnosticEngine.h"

#include "logging.h"

using namespace QLang;
using namespace std;

// The single diagnostic reporting path, installed by main() (qcc.cpp). Used here
// so a statement-level parse error can be buffered and recovered from.
extern QLang::DiagnosticEngine *gDiag;

// Panic-mode recovery after a statement parse error: skip to the next `;` (at
// brace depth 0, consumed) or the block-closing `}` (left for Block::Parse), so
// the rest of the function body still parses and reports its own errors.
static void resyncStatement( Lexer &l )
{
	int depth = 0;
	while ( !l.isEOF() )
	{
		int p = l.peekSymbol();
		if ( p == -1 )
			break;
		if ( depth == 0 && p == '}' )
			break;                       // leave the block terminator for the caller
		int sym = l.getSymbol();
		if ( sym == '{' )
			depth++;
		else if ( sym == '}' )
			depth--;                     // a nested close; depth>0 here
		else if ( sym == ';' && depth == 0 )
			break;                       // consumed the `;`, statement resynced
	}
}

Block *Block::Parse( Lexer &l, Scope *block_scope )
{
	SourceLocation loc = l.getTokenLocation();
	Block *block = new Block;
	block->setLocation( loc );
	block->mScope = block_scope;

	int sym = l.getSymbol();
	if ( sym != '{' )
	{
		COMPILE_ERROR( l, "expected \'{\'" );
	}

	while ( !l.isEOF() && l.peekSymbol() != '}' )
	{
		int pos_before = l.getCurrentPos();
		try {
			Statement *statement = Statement::Parse( l, block->mScope );

			if ( statement == nullptr && l.getCurrentPos() == pos_before )
			{
				COMPILE_ERROR( l, "Failed to parse statement" );
			}

			if ( statement != nullptr )
				block->mStatementList.push_back( statement );
		} catch ( CompileError &err ) {
			// Buffer the located diagnostic and resync to the next statement so
			// the remaining statements in this block still parse (multi-error).
			// The fallback is a collector; on the (degenerate) gDiag==null path
			// finish() it immediately or the diagnostic would be dropped.
			DiagnosticEngine fallback;
			DiagnosticEngine &eng = ( gDiag != nullptr ) ? *gDiag : fallback;
			eng.reportCompileError( err );
			if ( gDiag == nullptr )
				fallback.finish();
			resyncStatement( l );
		}
	}

	// A missing '}' (EOF reached before the block closed — e.g. an unterminated
	// block, or recovery that consumed to EOF) is a genuine error. The while loop
	// exits at EOF to avoid an infinite resync spin; report the missing brace here
	// so the diagnostic is not silently swallowed.
	if ( l.peekSymbol() != '}' )
		COMPILE_ERROR( l, "expected \'}\'" );
	l.getSymbol();   // consume '}'
	return block;
}
