#ifndef BLANG_LSP_TRANSPORT_H_
#define BLANG_LSP_TRANSPORT_H_

// LSP base-protocol framing: each message is
//   Content-Length: <bytes>\r\n
//   [other headers, ignored]\r\n
//   \r\n
//   <bytes of JSON payload>
// Reader and writer take caller-supplied streams so the server can run over
// stdio in production and over stringstreams in unit tests.

#include <iosfwd>
#include <string>

namespace lsp
{

// Read one framed message body from `in`. Returns false on clean EOF before
// a header starts. Throws TransportError on a malformed header (no
// Content-Length, non-numeric or oversized length, truncated payload) — a
// framing error means the byte stream is unrecoverable.
struct TransportError
{
	std::string message;
};

bool readFrame( std::istream &in, std::string &payload );

// Write one framed message and flush.
void writeFrame( std::ostream &out, const std::string &payload );

// Upper bound on a single message body (16 MiB): a corrupt length header
// must not make the reader try to allocate gigabytes.
extern const std::size_t kMaxFrameBytes;

} // namespace lsp

#endif // BLANG_LSP_TRANSPORT_H_
