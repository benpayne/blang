#ifndef BLANG_LSP_DOCUMENT_STORE_H_
#define BLANG_LSP_DOCUMENT_STORE_H_

// Open-document tracking for full-sync (TextDocumentSyncKind.Full): the
// client sends complete document text on open and on every change.

#include <map>
#include <string>

namespace lsp
{

class DocumentStore
{
public:
	void open( const std::string &uri, const std::string &text )
	{
		mDocs[ uri ] = text;
	}

	// Full sync: a change replaces the whole text.
	void change( const std::string &uri, const std::string &text )
	{
		mDocs[ uri ] = text;
	}

	void close( const std::string &uri ) { mDocs.erase( uri ); }

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

private:
	std::map<std::string, std::string> mDocs;
};

} // namespace lsp

#endif // BLANG_LSP_DOCUMENT_STORE_H_
