#ifndef BLANG_LSP_DOCUMENT_STORE_H_
#define BLANG_LSP_DOCUMENT_STORE_H_

// Open-document tracking for full-sync (TextDocumentSyncKind.Full): the
// client sends complete document text on open and on every change.
//
// modules-v2-lsp U3 (FR-011): it is also the OVERLAY — any file that is part of
// a document's compile context (a sibling project source, a dependency source,
// a blang.toml) is read from its open buffer when one exists, keyed by the
// file's canonical path (realpath, else the literal path).

#include <map>
#include <string>

#include "../ProjectLayout.h"

namespace lsp
{

// Unsaved-buffer lookup by canonical path (BlangProject::canonicalPath).
class Overlay
{
public:
	virtual ~Overlay() {}
	virtual const std::string *textForPath( const std::string &canonPath ) const = 0;
};

// An overlay with no open buffers (tests, and compiles outside a server).
class EmptyOverlay : public Overlay
{
public:
	const std::string *textForPath( const std::string & ) const override { return nullptr; }
};

class DocumentStore : public Overlay
{
public:
	// `path` is the document's filesystem path (uriToPath( uri )); it keys the
	// overlay. Empty for a non-file URI.
	void open( const std::string &uri, const std::string &text, const std::string &path = "" )
	{
		mDocs[ uri ] = text;
		if ( !path.empty() )
		{
			std::string canon = BlangProject::canonicalPath( path );
			mPathToUri[ canon ] = uri;
			mUriToPath[ uri ] = canon;
		}
	}

	// Full sync: a change replaces the whole text.
	void change( const std::string &uri, const std::string &text )
	{
		mDocs[ uri ] = text;
	}

	void close( const std::string &uri )
	{
		mDocs.erase( uri );
		auto it = mUriToPath.find( uri );
		if ( it != mUriToPath.end() )
		{
			mPathToUri.erase( it->second );
			mUriToPath.erase( it );
		}
	}

	bool has( const std::string &uri ) const
	{
		return mDocs.find( uri ) != mDocs.end();
	}

	const std::string &text( const std::string &uri ) const
	{
		static const std::string empty;
		auto it = mDocs.find( uri );
		return it != mDocs.end() ? it->second : empty;
	}

	const std::string *textForPath( const std::string &canonPath ) const override
	{
		auto p = mPathToUri.find( canonPath );
		if ( p == mPathToUri.end() )
			return nullptr;
		auto d = mDocs.find( p->second );
		return d != mDocs.end() ? &d->second : nullptr;
	}

private:
	std::map<std::string, std::string> mDocs;
	std::map<std::string, std::string> mPathToUri;   // canonical path -> uri
	std::map<std::string, std::string> mUriToPath;
};

} // namespace lsp

#endif // BLANG_LSP_DOCUMENT_STORE_H_
