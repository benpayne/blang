/* Unit tests for runtime/blang_hash.c (FNV-1a string hash). */
#include "test_util.h"
#include "../blang_hash.h"
#include "../blang_string.h"

#include <string.h>

static BlangString *S( const char *c )
{
	return __blang_string_create( c, (int64_t)strlen( c ) );
}

static void t_deterministic( void )
{
	BlangString *a = S( "hello world" );
	BlangString *b = S( "hello world" );
	CHECK_EQ_I( __blang_hash_string( a ), __blang_hash_string( b ) );
	__blang_string_release( a );
	__blang_string_release( b );
}
static void t_distinct( void )
{
	/* A small known set of distinct short strings must hash distinctly. */
	const char *words[] = { "alice", "bob", "charlie", "dave", "eve", "frank" };
	int32_t hs[6];
	for ( int i = 0; i < 6; i++ )
	{
		BlangString *s = S( words[i] );
		hs[i] = __blang_hash_string( s );
		__blang_string_release( s );
	}
	for ( int i = 0; i < 6; i++ )
		for ( int j = i + 1; j < 6; j++ )
			CHECK( hs[i] != hs[j] );
}
static void t_nonnegative( void )
{
	const char *words[] = { "", "x", "a really quite long string value here", "\x7f\x7f\x7f" };
	for ( int i = 0; i < 4; i++ )
	{
		BlangString *s = S( words[i] );
		CHECK( __blang_hash_string( s ) >= 0 );
		__blang_string_release( s );
	}
}
static void t_empty_string( void )
{
	BlangString *e = S( "" );
	/* FNV-1a of the empty string is the offset basis, masked to 63 bits. */
	int32_t h = __blang_hash_string( e );
	CHECK( h >= 0 );
	BlangString *e2 = S( "" );
	CHECK_EQ_I( h, __blang_hash_string( e2 ) );
	__blang_string_release( e );
	__blang_string_release( e2 );
}

static blang_test_case cases[] = {
	{ "deterministic", t_deterministic },
	{ "distinct", t_distinct },
	{ "nonnegative", t_nonnegative },
	{ "empty_string", t_empty_string },
};
TEST_MAIN( cases )
