#ifndef BLANG_LSP_STDLIB_LOCATION_H_
#define BLANG_LSP_STDLIB_LOCATION_H_

// modules-v2-lsp U2: where blangd finds the stdlib (design D3; spec FR-006..009;
// specs/042-lsp-stdlib-context/contracts/stdlib-discovery.md).
//
// Precedence: initializationOptions.stdlibPath > BLANG_STDLIB_DIR >
// <blangd-executable-dir>/stdlib. The FIRST route that is SET is selected and is
// final: an unusable selected route never falls through to a later one (that
// would silently use a different stdlib than the one configured). A directory is
// usable iff it holds every base module the shared selection always combines
// (BlangStdlib::baseModuleNames()), so "usable" cannot disagree with bcc.
//
// Security (constitution Quality Gate 7): the path is untrusted. It is used only
// as a prefix for stat/access; nothing is executed, no network is touched.

#include <string>

namespace lsp
{

struct StdlibLocation
{
	enum class Route { kInitOptions, kEnv, kRelative };
	Route route = Route::kRelative;
	std::string path;      // as given (routes 1/2) or exeDir + "/stdlib"; never normalized
	bool usable = false;
	std::string missing;   // "" when usable; else "directory" or the first absent "<name>.b"
};

// Pure: precedence + usability. An empty `initOpt`/`env` means unset.
StdlibLocation locateStdlib( const std::string &initOpt, const std::string &env,
	const std::string &exeDir );

// "initializationOptions.stdlibPath" | "BLANG_STDLIB_DIR" | "blangd-relative".
const char *routeName( StdlibLocation::Route route );

// The fixed stdlib-not-found diagnostic text (research R8).
std::string stdlibNotFoundMessage( const StdlibLocation &loc );

} // namespace lsp

#endif // BLANG_LSP_STDLIB_LOCATION_H_
