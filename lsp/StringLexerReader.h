#ifndef BLANG_LSP_STRING_LEXER_READER_H_
#define BLANG_LSP_STRING_LEXER_READER_H_

// modules-v2-lsp U1: the in-memory LexerReader moved into the shared frontend
// (../StringLexerReader.h, namespace QLang) so ProjectCompile can lex in-memory
// inputs. This alias keeps existing `lsp::StringLexerReader` users compiling.

#include "../StringLexerReader.h"

namespace lsp
{
using StringLexerReader = QLang::StringLexerReader;
}

#endif // BLANG_LSP_STRING_LEXER_READER_H_
