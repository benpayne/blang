// Unit tests for the LSP protocol layer: lsp::Json (parse/serialize) and the
// Content-Length transport framing. Zero dependencies, same assert-macro
// pattern as the runtime C tests. Exit 0 on success, non-zero with a message
// on the first failure.

#include <iostream>
#include <sstream>
#include <string>

#include "Json.h"
#include "Transport.h"

using lsp::Json;

static int gFailures = 0;

#define CHECK( cond ) \
	do { \
		if ( !( cond ) ) \
		{ \
			std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ \
			          << ": " << #cond << std::endl; \
			gFailures++; \
		} \
	} while ( false )

static Json parseOk( const std::string &text )
{
	Json out;
	std::string err;
	bool ok = Json::parse( text, out, err );
	if ( !ok )
	{
		std::cerr << "FAIL parse of: " << text << " — " << err << std::endl;
		gFailures++;
	}
	return out;
}

static void expectParseError( const std::string &text )
{
	Json out;
	std::string err;
	if ( Json::parse( text, out, err ) )
	{
		std::cerr << "FAIL expected parse error for: " << text << std::endl;
		gFailures++;
	}
	else
	{
		CHECK( !err.empty() );
		CHECK( out.isNull() );
	}
}

static void testScalars()
{
	CHECK( parseOk( "null" ).isNull() );
	CHECK( parseOk( "true" ).asBool() == true );
	CHECK( parseOk( "false" ).asBool() == false );
	CHECK( parseOk( "42" ).asInt() == 42 );
	CHECK( parseOk( "-7" ).asInt() == -7 );
	CHECK( parseOk( "2.5" ).asNumber() == 2.5 );
	CHECK( parseOk( "1e3" ).asNumber() == 1000.0 );
	CHECK( parseOk( "\"hi\"" ).asString() == "hi" );
	CHECK( parseOk( "  \t\n 1 " ).asInt() == 1 );
}

static void testStringEscapes()
{
	CHECK( parseOk( "\"a\\nb\"" ).asString() == "a\nb" );
	CHECK( parseOk( "\"q\\\"q\"" ).asString() == "q\"q" );
	CHECK( parseOk( "\"s\\\\s\"" ).asString() == "s\\s" );
	CHECK( parseOk( "\"\\u0041\"" ).asString() == "A" );
	// 2- and 3-byte UTF-8 from \u escapes.
	CHECK( parseOk( "\"\\u00e9\"" ).asString() == "\xc3\xa9" );
	CHECK( parseOk( "\"\\u20ac\"" ).asString() == "\xe2\x82\xac" );
	// Surrogate pair -> 4-byte UTF-8 (U+1F600).
	CHECK( parseOk( "\"\\ud83d\\ude00\"" ).asString() == "\xf0\x9f\x98\x80" );

	// Round-trip: control chars re-escape.
	CHECK( Json( "a\nb" ).serialize() == "\"a\\nb\"" );
	CHECK( Json( std::string( "z\x01z", 3 ) ).serialize() == "\"z\\u0001z\"" );
	// Raw UTF-8 passes through unescaped.
	CHECK( Json( "\xc3\xa9" ).serialize() == "\"\xc3\xa9\"" );
}

static void testContainers()
{
	Json arr = parseOk( "[1, \"two\", [3], {\"four\": 4}, null]" );
	CHECK( arr.isArray() );
	CHECK( arr.size() == 5 );
	CHECK( arr.at( 0 ).asInt() == 1 );
	CHECK( arr.at( 1 ).asString() == "two" );
	CHECK( arr.at( 2 ).at( 0 ).asInt() == 3 );
	CHECK( arr.at( 3 ).get( "four" ).asInt() == 4 );
	CHECK( arr.at( 4 ).isNull() );
	CHECK( arr.at( 99 ).isNull() ); // out of range -> null, no crash

	Json obj = parseOk( "{\"a\": 1, \"b\": {\"c\": [true]}}" );
	CHECK( obj.isObject() );
	CHECK( obj.has( "a" ) );
	CHECK( !obj.has( "z" ) );
	CHECK( obj.get( "z" ).isNull() );
	CHECK( obj.get( "b" ).get( "c" ).at( 0 ).asBool() );

	CHECK( parseOk( "[]" ).size() == 0 );
	CHECK( parseOk( "{}" ).size() == 0 );
}

static void testInsertionOrder()
{
	// Keys serialize in insertion order — the golden-transcript harness
	// depends on this determinism.
	Json obj = Json::object();
	obj.set( "zebra", 1 );
	obj.set( "alpha", 2 );
	obj.set( "mid", Json::array() );
	CHECK( obj.serialize() == "{\"zebra\":1,\"alpha\":2,\"mid\":[]}" );

	// set() on an existing key replaces in place, keeping position.
	obj.set( "zebra", 9 );
	CHECK( obj.serialize() == "{\"zebra\":9,\"alpha\":2,\"mid\":[]}" );

	// Parse preserves document order too.
	Json parsed = parseOk( "{\"b\":1,\"a\":2}" );
	CHECK( parsed.serialize() == "{\"b\":1,\"a\":2}" );
}

static void testNumbersSerialize()
{
	CHECK( Json( 0 ).serialize() == "0" );
	CHECK( Json( -3 ).serialize() == "-3" );
	CHECK( Json( 2.5 ).serialize() == "2.5" );
	// Integral doubles print without a decimal point (LSP positions are
	// integers and must not come back as "5.0").
	CHECK( Json( 5.0 ).serialize() == "5" );
	CHECK( Json( (int64_t)1234567890123LL ).serialize() == "1234567890123" );
}

static void testParseErrors()
{
	expectParseError( "" );
	expectParseError( "{" );
	expectParseError( "[1," );
	expectParseError( "\"unterminated" );
	expectParseError( "{\"a\" 1}" );
	expectParseError( "{\"a\":1,}" );
	expectParseError( "tru" );
	expectParseError( "nul" );
	expectParseError( "1 2" );          // trailing garbage
	expectParseError( "[1] []" );       // trailing garbage
	expectParseError( "\"bad\\x\"" );   // invalid escape
	expectParseError( "\"\\ud83d\"" );  // unpaired high surrogate
	expectParseError( "\"\\ude00\"" );  // unpaired low surrogate
	expectParseError( "01a" );

	// Depth cap: kMaxDepth+2 nested arrays must be rejected, not overflow.
	std::string deep;
	for ( int i = 0; i < Json::kMaxDepth + 2; i++ )
		deep += '[';
	for ( int i = 0; i < Json::kMaxDepth + 2; i++ )
		deep += ']';
	expectParseError( deep );
}

static void testTransport()
{
	// Round-trip a frame.
	std::stringstream stream;
	lsp::writeFrame( stream, "{\"id\":1}" );
	std::string body;
	CHECK( lsp::readFrame( stream, body ) );
	CHECK( body == "{\"id\":1}" );
	CHECK( !lsp::readFrame( stream, body ) ); // clean EOF after last frame

	// Two frames back to back; extra headers are ignored.
	std::stringstream two;
	two << "Content-Length: 2\r\nContent-Type: application/json\r\n\r\nab"
	    << "Content-Length: 3\r\n\r\nxyz";
	CHECK( lsp::readFrame( two, body ) && body == "ab" );
	CHECK( lsp::readFrame( two, body ) && body == "xyz" );
	CHECK( !lsp::readFrame( two, body ) );

	// Malformed: missing Content-Length.
	std::stringstream bad1( "Content-Type: json\r\n\r\n{}" );
	bool threw = false;
	try { lsp::readFrame( bad1, body ); }
	catch ( const lsp::TransportError & ) { threw = true; }
	CHECK( threw );

	// Malformed: non-numeric length.
	std::stringstream bad2( "Content-Length: abc\r\n\r\n{}" );
	threw = false;
	try { lsp::readFrame( bad2, body ); }
	catch ( const lsp::TransportError & ) { threw = true; }
	CHECK( threw );

	// Truncated payload.
	std::stringstream bad3( "Content-Length: 10\r\n\r\nab" );
	threw = false;
	try { lsp::readFrame( bad3, body ); }
	catch ( const lsp::TransportError & ) { threw = true; }
	CHECK( threw );

	// Oversized length is rejected before any allocation.
	std::stringstream bad4( "Content-Length: 99999999999\r\n\r\n" );
	threw = false;
	try { lsp::readFrame( bad4, body ); }
	catch ( const lsp::TransportError & ) { threw = true; }
	CHECK( threw );

	// Zero-length body is a valid frame.
	std::stringstream zero( "Content-Length: 0\r\n\r\n" );
	CHECK( lsp::readFrame( zero, body ) && body.empty() );
}

int main()
{
	testScalars();
	testStringEscapes();
	testContainers();
	testInsertionOrder();
	testNumbersSerialize();
	testParseErrors();
	testTransport();

	if ( gFailures > 0 )
	{
		std::cerr << gFailures << " check(s) failed" << std::endl;
		return 1;
	}
	std::cout << "jsonTest: all checks passed" << std::endl;
	return 0;
}
