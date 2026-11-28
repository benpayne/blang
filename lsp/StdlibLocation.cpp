#include "StdlibLocation.h"

#include <sys/stat.h>
#include <unistd.h>

#include "../StdlibSelect.h"

namespace lsp
{

StdlibLocation locateStdlib( const std::string &initOpt, const std::string &env,
	const std::string &exeDir )
{
	StdlibLocation loc;
	if ( !initOpt.empty() )
	{
		loc.route = StdlibLocation::Route::kInitOptions;
		loc.path = initOpt;
	}
	else if ( !env.empty() )
	{
		loc.route = StdlibLocation::Route::kEnv;
		loc.path = env;
	}
	else
	{
		loc.route = StdlibLocation::Route::kRelative;
		loc.path = exeDir + "/stdlib";
	}

	// Only the selected path is evaluated — no fall-through to another route.
	struct stat st;
	if ( stat( loc.path.c_str(), &st ) != 0 || !S_ISDIR( st.st_mode ) ||
		 access( loc.path.c_str(), R_OK | X_OK ) != 0 )
	{
		loc.missing = "directory";
		return loc;
	}
	for ( const std::string &name : BlangStdlib::baseModuleNames() )
	{
		std::string file = loc.path + "/" + name + ".b";
		if ( access( file.c_str(), R_OK ) != 0 )
		{
			loc.missing = name + ".b";
			return loc;
		}
	}
	loc.usable = true;
	return loc;
}

const char *routeName( StdlibLocation::Route route )
{
	switch ( route )
	{
	case StdlibLocation::Route::kInitOptions:
		return "initializationOptions.stdlibPath";
	case StdlibLocation::Route::kEnv:
		return "BLANG_STDLIB_DIR";
	case StdlibLocation::Route::kRelative:
		break;
	}
	return "blangd-relative";
}

std::string stdlibNotFoundMessage( const StdlibLocation &loc )
{
	return std::string( "stdlib not found: " ) + routeName( loc.route ) + " '" + loc.path +
		"' is not a usable stdlib directory (missing " + loc.missing +
		"); set initializationOptions.stdlibPath or BLANG_STDLIB_DIR, or install "
		"stdlib/ next to blangd — this file is checked without the stdlib";
}

} // namespace lsp
