#include "ProjectContext.h"

#include <fstream>
#include <map>
#include <sstream>

#include <sys/stat.h>

#include "../BmodEmitter.h"
#include "../Expression.h"
#include "../ProjectLayout.h"

namespace lsp
{

using namespace QLang;

bool readThroughOverlay( const Overlay &overlay, const std::string &path, std::string &out )
{
	if ( const std::string *buf = overlay.textForPath( BlangProject::canonicalPath( path ) ) )
	{
		out = *buf;
		return true;
	}
	struct stat st;
	if ( stat( path.c_str(), &st ) != 0 || !S_ISREG( st.st_mode ) )
		return false;   // missing, a directory, a device: not a readable file
	std::ifstream in( path, std::ios::binary );
	if ( !in.is_open() )
		return false;
	std::ostringstream ss;
	ss << in.rdbuf();
	out = ss.str();
	return true;
}

namespace
{

// One dependency as the walk folds it: its interface (the exact .bmod text its
// own bcc compile would emit) plus where bcc would have put that .bmod.
struct DepIface
{
	std::string origin;     // canonical dir (bcc's --bmod-origin value)
	std::string name;       // [project] name == import name == .bmod basename
	std::string bmodPath;   // <depDir>/<name>.bmod — the path bcc's qcc line carries
	std::string text;       // the emitted interface
	bool ok = false;        // false: the dependency is unresolvable (compile failed)
};

std::string baseName( const std::string &p )
{
	size_t s = p.rfind( '/' );
	return s == std::string::npos ? p : p.substr( s + 1 );
}

// blangd's visitor over the SHARED walk (audit addendum H1). It never stops the
// walk: an unresolvable dependency becomes one setup diagnostic and is skipped
// (bcc's visitor, by contrast, prints and aborts). Interfaces are derived
// post-order and memoized by origin for THIS compile only (no reuse across
// reparses — design D5).
class BlangdVisitor : public BlangProject::WalkVisitor<DepIface>
{
public:
	BlangdVisitor( const Overlay &overlay, const std::string &rootCanon, ProjectContextResult &out )
		: mOverlay( overlay ), mRootCanon( rootCanon ), mOut( out ) {}

	ProjectConfig *load( const std::string &dir ) override
	{
		std::string tomlPath = dir + "/blang.toml";
		std::string text;
		if ( !readThroughOverlay( mOverlay, tomlPath, text ) )
			return nullptr;
		std::vector<ConfigIssue> ignored;   // a dependency's own toml lines: not this document's
		return ProjectConfig::loadFromText( tomlPath, text, &ignored );
	}

	int onRootLoadError( const std::string & ) override
	{
		return 0;   // unreachable: blangd only walks from a root it already loaded
	}

	int onDepError( const BlangProject::WalkNode &parent, const Dependency &dep,
		const BlangProject::DepDir &dd ) override
	{
		using E = BlangProject::DepDir::Error;
		SetupDiagnostic s;
		s.importName = dep.name;
		s.declaredAt = parent.dir + "/blang.toml:" + std::to_string( dep.line );
		switch ( dd.error )
		{
		case E::kNoBlangToml:
			s.message = "dependency '" + dep.name + "' cannot be resolved: '" + dd.dir +
				"' has no blang.toml (declared at " + s.declaredAt + ")";
			s.declaredAt.clear();   // already in the message
			break;
		case E::kUnfetched:
			s.message = "git dependency '" + dep.name + "' is not fetched (expected a checkout at '" +
				dd.dir + "'); run 'bcc build' to fetch it";
			break;
		case E::kUnpinned:
		case E::kNoPathOrGit:
			s.message = dd.message;   // bcc's exact text
			break;
		case E::kFetchFailed:   // unreachable: blangd never passes a fetcher
		case E::kNone:
			return 0;
		}
		mOut.unresolvedNames.insert( dep.name );
		mOut.setup.push_back( s );
		return 0;   // skip it; keep walking
	}

	int onCycle( const BlangProject::WalkNode &node ) override
	{
		std::string msg = "circular dependency: ";
		bool started = false;
		for ( const auto &d : *node.onPath )
		{
			if ( d == node.canonDir )
				started = true;
			if ( started )
				msg += baseName( d ) + " -> ";
		}
		msg += baseName( node.canonDir );
		for ( const auto &s : mOut.setup )
			if ( s.message == msg )
				return 0;
		SetupDiagnostic s;
		s.message = msg;
		mOut.setup.push_back( s );
		return 0;
	}

	std::string originOf( const DepIface &a ) const override { return a.origin; }

	int leave( const BlangProject::WalkNode &node, const std::vector<DepIface> &closure,
		const std::vector<DepIface> &, DepIface &self ) override
	{
		if ( node.canonDir == mRootCanon )
			return 0;   // the root derives nothing — the document compile is the caller's (N2)
		auto memo = mMemo.find( node.canonDir );
		if ( memo != mMemo.end() )
		{
			self = memo->second;
			return 0;
		}
		self = derive( node, closure );
		mMemo[ node.canonDir ] = self;
		return 0;
	}

private:
	// D-3: the dependency's own bcc lib compile (non-combine, no stdlib, its
	// closure interfaces, its canonical origin), then its interface emitted IN
	// MEMORY by the one exporter, slicing generic bodies from the exact parsed
	// text (audit R1/H3).
	DepIface derive( const BlangProject::WalkNode &node, const std::vector<DepIface> &closure )
	{
		DepIface self;
		self.origin = node.canonDir;
		self.name = node.config->getName();
		self.bmodPath = node.dir + "/" + self.name + ".bmod";

		CompilePlan plan;
		plan.combine = false;
		plan.identity = IdentityPolicy::kCanonical;
		plan.moduleOrigin = node.canonDir;
		for ( const auto &src : BlangProject::discoverSourceFiles( node.dir ) )
		{
			CompileInput in;
			in.path = src;
			in.kind = CompileInput::Kind::kSource;
			in.inMemory = true;
			readThroughOverlay( mOverlay, src, in.text );
			plan.inputs.push_back( in );
		}
		for ( const auto &c : closure )
		{
			if ( !c.ok )
				continue;
			CompileInput in;
			in.path = c.bmodPath;
			in.kind = CompileInput::Kind::kInterface;
			in.inMemory = true;
			in.text = c.text;
			in.origin = c.origin;
			plan.inputs.push_back( in );
		}
		// The provider covers EVERY input of this run, keyed exactly by
		// CompileInput::path (the string SourceLocation.file carries).
		std::map<std::string, const std::string *> byPath;
		for ( const auto &in : plan.inputs )
			byPath[ in.path ] = &in.text;
		BmodEmitter::SourceTextProvider provider = [&byPath]( const std::string &p ) -> const std::string * {
			auto it = byPath.find( p );
			return it != byPath.end() ? it->second : nullptr;
		};

		DiagnosticEngine engine;
		std::vector<std::string> missing;
		bool failed = false;
		auto run = std::make_shared<Run>( runPlan( plan, engine, [&]( Run &r ) {
			failed = r.ctx->hadError() || engine.hasErrors();
			if ( failed )
				return;
			// Emitted inside the run's alias scope (analyze L3).
			std::vector<Module *> mods;
			for ( std::size_t ri : r.ctx->moduleIndices() )
			{
				Module *m = const_cast<Module *>( (const Module *)r.ctx->record( ri ).module );
				if ( m != nullptr && !m->isExtern() )
					mods.push_back( m );
			}
			std::ostringstream os;
			BmodEmitter::emit( mods, os, r.ctx->bmodEmitScope(), &provider, &missing );
			self.text = os.str();
		} ) );
		mOut.depRuns.push_back( run );

		SetupDiagnostic s;
		s.importName = self.name;
		if ( failed )
		{
			std::string first = "unknown error";
			for ( const auto &d : engine.diagnostics() )
				if ( d.severity == Severity::Error )
				{
					first = d.location.file + ":" + std::to_string( d.location.line ) + ":" +
						std::to_string( d.location.col ) + ": " + d.message;
					break;
				}
			s.message = "dependency '" + self.name + "' failed to compile: " + first;
		}
		else if ( !missing.empty() )
		{
			s.message = "internal error: no source text for '" + missing.front() +
				"' while deriving the interface of '" + self.name + "'";
		}
		else
		{
			self.ok = true;
			return self;
		}
		mOut.unresolvedNames.insert( self.name );
		mOut.setup.push_back( s );
		return self;
	}

	const Overlay &mOverlay;
	std::string mRootCanon;
	ProjectContextResult &mOut;
	std::map<std::string, DepIface> mMemo;
};

} // namespace

ProjectContextResult buildProjectContext( const std::string &docPath, const std::string &docText,
	const Overlay &overlay )
{
	ProjectContextResult r;
	size_t slash = docPath.rfind( '/' );
	std::string dir = slash == std::string::npos ? "." : ( slash == 0 ? "/" : docPath.substr( 0, slash ) );
	std::string root = BlangProject::findProjectRoot( dir );
	if ( root.empty() )
		return r;   // no project: U2's single-file context

	// Membership (R5): the document must be one of the files bcc build compiles.
	std::vector<std::string> sources = BlangProject::discoverSourceFiles( root );
	std::string docCanon = BlangProject::canonicalPath( docPath );
	bool member = false;
	for ( const auto &s : sources )
		member = member || BlangProject::canonicalPath( s ) == docCanon;
	if ( !member )
		return r;   // e.g. a file in a project subdirectory: U2's single-file context

	r.tomlPath = root + "/blang.toml";
	std::string tomlText;
	if ( !readThroughOverlay( overlay, r.tomlPath, tomlText ) )
	{
		r.tomlUnreadable = true;   // N4: one Warning, then the single-file context
		return r;
	}
	std::vector<ConfigIssue> issues;
	std::unique_ptr<ProjectConfig> cfg( ProjectConfig::loadFromText( r.tomlPath, tomlText, &issues ) );
	for ( const auto &i : issues )
	{
		SetupDiagnostic s;
		s.severity = Severity::Warning;
		s.message = r.tomlPath + ":" + std::to_string( i.line ) + ": parse error: " + i.message;
		r.setup.push_back( s );
	}

	r.isProject = true;
	r.root = root;
	r.isLibrary = cfg->isLibrary();
	r.moduleOrigin = BlangProject::canonicalPath( root );

	for ( const auto &s : sources )
	{
		CompileInput in;
		in.kind = CompileInput::Kind::kSource;
		in.inMemory = true;
		if ( BlangProject::canonicalPath( s ) == docCanon )
		{
			in.path = docPath;   // EXACTLY the URI path (analyze M6)
			in.text = docText;
		}
		else
		{
			in.path = s;
			readThroughOverlay( overlay, s, in.text );
		}
		r.sources.push_back( in );
	}

	// The SHARED walk (bcc's own rule): resolution, cycles, fold order.
	BlangdVisitor visitor( overlay, r.moduleOrigin, r );
	std::vector<DepIface> closure;
	DepIface self;
	BlangProject::walkDependencies( root, nullptr, visitor, closure, self );
	for ( const auto &c : closure )
	{
		if ( !c.ok )
			continue;
		CompileInput in;
		in.path = c.bmodPath;
		in.kind = CompileInput::Kind::kInterface;
		in.inMemory = true;
		in.text = c.text;
		in.origin = c.origin;
		r.interfaces.push_back( in );
	}
	return r;
}

} // namespace lsp
