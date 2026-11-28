#ifndef BLANG_STDLIB_SELECT_H_
#define BLANG_STDLIB_SELECT_H_

// modules-v2-lsp U1: the stdlib-set selection rule, shared by bcc (which links
// this file directly and does NOT link the parser) and the frontend (blangd,
// the project_compile ctest). Moved verbatim out of bcc.cpp
// (parseImports/resolveStdlibFiles) so the compiler and the editor cannot pick a
// different stdlib set for the same program. Dependency-free: standard library
// plus POSIX access(2). No parser, no LLVM.

#include <set>
#include <string>
#include <vector>

namespace BlangStdlib
{

// The leading identifier of each line that begins (after whitespace) with
// "import ", byte-for-byte bcc's former parseImports rule, applied to TEXT:
//   1. per std::getline line, truncate at the first "//" (even inside a string);
//   2. skip isspace( (unsigned char)c );
//   3. require line.compare( i, 7, "import " ) == 0 — a literal `import` and ONE
//      ASCII space (a tab after `import` does not match);
//   4. skip isspace again;
//   5. take the maximal isalnum || '_' run; insert it if non-empty.
// Block comments are not recognized; only the leading identifier of a dotted
// path is taken ("env.x" -> "env").
std::set<std::string> scanImports( const std::string &sourceText );

// bcc's former resolveStdlibFiles rule: the base modules
// {sys, buffer, collections, fs, net} always, then the gated
// {timer, math, time, random, env, cli} in that order when imported, then any
// other imported name — each only if <stdlibDir>/<name>.b exists (access F_OK).
// Candidate paths are `stdlibDir + "/" + name + ".b"` with no normalization, so
// stdlibDir == exeDir + "/stdlib" yields bcc's historical strings exactly.
std::vector<std::string> selectStdlibFiles( const std::string &stdlibDir,
	const std::set<std::string> &imports );

// The base modules every program is combined with, in dependency-safe order
// (sys, buffer, collections, fs, net). The ONE copy of that list: the selection
// above iterates it, and blangd's stdlib-usability check (lsp/StdlibLocation)
// reads it, so "usable" can never disagree with what bcc always combines.
const std::vector<std::string> &baseModuleNames();

// Directory of the running executable: readlink(/proc/self/exe), else argv0's
// directory, else ".". Moved verbatim from bcc's getExeDir (modules-v2-lsp U2) so
// bcc (qcc + stdlib lookup) and blangd (its blangd-relative stdlib route) locate
// "next to the binary" by one rule. A filesystem read only — never a process.
std::string executableDir( const char *argv0 );

} // namespace BlangStdlib

#endif // BLANG_STDLIB_SELECT_H_
