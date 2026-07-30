#ifndef BLANG_LSP_JSON_H_
#define BLANG_LSP_JSON_H_

// Minimal JSON value type for the LSP protocol layer. Self-owned on purpose:
// the repo has a zero-external-dependency rule, and runtime/blang_json.h is
// the LANGUAGE runtime (C, BlangString-coupled) — reusing it here would couple
// the compiler's protocol layer to the compiled programs' runtime.
//
// Objects preserve insertion order (vector of pairs, not a map) so serialized
// replies are deterministic and the golden-transcript harness never sees key
// reordering. Lookup is linear — LSP objects are small.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lsp
{

class Json
{
public:
	enum class Kind { Null, Bool, Number, String, Array, Object };

	// --- Construction ---
	Json() : mKind( Kind::Null ) {}
	Json( std::nullptr_t ) : mKind( Kind::Null ) {}
	Json( bool b ) : mKind( Kind::Bool ), mBool( b ) {}
	Json( int n ) : mKind( Kind::Number ), mNumber( n ) {}
	Json( int64_t n ) : mKind( Kind::Number ), mNumber( (double)n ) {}
	Json( double n ) : mKind( Kind::Number ), mNumber( n ) {}
	Json( const char *s ) : mKind( Kind::String ), mString( s ) {}
	Json( const std::string &s ) : mKind( Kind::String ), mString( s ) {}

	static Json array() { Json j; j.mKind = Kind::Array; return j; }
	static Json object() { Json j; j.mKind = Kind::Object; return j; }

	// --- Inspection ---
	Kind kind() const { return mKind; }
	bool isNull() const { return mKind == Kind::Null; }
	bool isBool() const { return mKind == Kind::Bool; }
	bool isNumber() const { return mKind == Kind::Number; }
	bool isString() const { return mKind == Kind::String; }
	bool isArray() const { return mKind == Kind::Array; }
	bool isObject() const { return mKind == Kind::Object; }

	// Value accessors return a neutral default on kind mismatch (protocol
	// handlers validate kinds explicitly where it matters).
	bool asBool() const { return isBool() ? mBool : false; }
	double asNumber() const { return isNumber() ? mNumber : 0.0; }
	int asInt() const { return (int)asNumber(); }
	const std::string &asString() const
	{
		static const std::string empty;
		return isString() ? mString : empty;
	}

	// --- Array ---
	void push( Json v ) { mArray.push_back( std::move( v ) ); }
	std::size_t size() const
	{
		return isArray() ? mArray.size() : ( isObject() ? mObject.size() : 0 );
	}
	const Json &at( std::size_t i ) const
	{
		static const Json null;
		return ( isArray() && i < mArray.size() ) ? mArray[ i ] : null;
	}
	const std::vector<Json> &items() const { return mArray; }

	// --- Object ---
	// set() replaces an existing key in place (order preserved) or appends.
	void set( const std::string &key, Json v );
	bool has( const std::string &key ) const;
	// get() returns a shared null on a missing key or non-object.
	const Json &get( const std::string &key ) const;
	const std::vector<std::pair<std::string, Json>> &members() const { return mObject; }

	// --- Serialization ---
	// Compact (no whitespace), "/" not escaped, control chars as \uXXXX.
	// Numbers that are integral print without a decimal point.
	std::string serialize() const;

	// Parse a complete JSON document. Returns false (and sets error to a
	// byte-offset message) on malformed input, trailing garbage, or nesting
	// deeper than kMaxDepth. On failure `out` is null.
	static bool parse( const std::string &text, Json &out, std::string &error );

	static const int kMaxDepth = 128;

private:

	void serializeTo( std::string &out ) const;
	static void appendEscaped( std::string &out, const std::string &s );

	Kind mKind;
	bool mBool = false;
	double mNumber = 0.0;
	std::string mString;
	std::vector<Json> mArray;
	std::vector<std::pair<std::string, Json>> mObject;
};

} // namespace lsp

#endif // BLANG_LSP_JSON_H_
