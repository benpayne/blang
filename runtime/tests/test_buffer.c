/* Unit tests for runtime/blang_buffer.c (BlangBuffer) */
#include "test_util.h"
#include "../blang_buffer.h"
#include "../blang_string.h"

static BlangString *S( const char *c )
{
	return __blang_string_create( c, (int64_t)strlen( c ) );
}
static void expect_to_string( BlangBuffer *b, const char *exp )
{
	BlangString *s = __blang_buffer_to_string( b );
	const char *c = __blang_string_to_cstring( s );
	CHECK_STR_EQ( c, exp );
	free( (void *)c );
	__blang_string_release( s );
}

static void t_create( void )
{
	BlangBuffer *b = __blang_buffer_create( 16 );
	CHECK( b != NULL );
	CHECK_EQ_I( __blang_buffer_length( b ), 0 );
	CHECK_TRUE( __blang_buffer_is_empty( b ) );
	__blang_buffer_release( b );
}

static void t_append_byte( void )
{
	BlangBuffer *b = __blang_buffer_create( 0 );
	__blang_buffer_append_byte( b, 'A' );
	__blang_buffer_append_byte( b, 'B' );
	CHECK_EQ_I( __blang_buffer_length( b ), 2 );
	CHECK_EQ_I( __blang_buffer_get( b, 0 ), 'A' );
	CHECK_EQ_I( __blang_buffer_get( b, 1 ), 'B' );
	__blang_buffer_release( b );
}

static void t_from_string( void )
{
	BlangString *s = S( "hi" );
	BlangBuffer *b = __blang_buffer_create_from_string( s );
	CHECK_EQ_I( __blang_buffer_length( b ), 2 );
	expect_to_string( b, "hi" );
	__blang_string_release( s );
	__blang_buffer_release( b );
}

static void t_append_string( void )
{
	BlangBuffer *b = __blang_buffer_create( 0 );
	BlangString *s = S( "abc" );
	__blang_buffer_append_string( b, s );
	expect_to_string( b, "abc" );
	__blang_string_release( s );
	__blang_buffer_release( b );
}

static void t_get_set( void )
{
	BlangBuffer *b = __blang_buffer_create( 0 );
	__blang_buffer_append_byte( b, 10 );
	__blang_buffer_append_byte( b, 20 );
	__blang_buffer_set( b, 0, 90 );
	CHECK_EQ_I( __blang_buffer_get( b, 0 ), 90 );
	CHECK_EQ_I( __blang_buffer_get( b, 1 ), 20 );
	__blang_buffer_release( b );
}

static void t_to_string( void )
{
	BlangBuffer *b = __blang_buffer_create( 0 );
	const char *msg = "hello";
	for ( const char *p = msg; *p; p++ )
		__blang_buffer_append_byte( b, (unsigned char)*p );
	expect_to_string( b, "hello" );
	__blang_buffer_release( b );
}

static void t_slice( void )
{
	BlangString *s = S( "hello" );
	BlangBuffer *b = __blang_buffer_create_from_string( s );
	BlangBuffer *sl = __blang_buffer_slice( b, 1, 4 );
	expect_to_string( sl, "ell" );
	__blang_string_release( s );
	__blang_buffer_release( b );
	__blang_buffer_release( sl );
}

static void t_index_of( void )
{
	BlangString *hs = S( "hello" ), *ps = S( "ll" );
	BlangBuffer *b = __blang_buffer_create_from_string( hs );
	BlangBuffer *pat = __blang_buffer_create_from_string( ps );
	CHECK_EQ_I( __blang_buffer_index_of( b, pat, 0 ), 2 );
	__blang_string_release( hs ); __blang_string_release( ps );
	__blang_buffer_release( b ); __blang_buffer_release( pat );
}

static void t_clear( void )
{
	BlangBuffer *b = __blang_buffer_create( 0 );
	__blang_buffer_append_byte( b, 1 );
	__blang_buffer_append_byte( b, 2 );
	__blang_buffer_clear( b );
	CHECK_EQ_I( __blang_buffer_length( b ), 0 );
	CHECK_TRUE( __blang_buffer_is_empty( b ) );
	__blang_buffer_release( b );
}

/* bounds guard probe */
static void get_oob_child( void )
{
	BlangBuffer *b = __blang_buffer_create( 0 );
	__blang_buffer_append_byte( b, 1 );
	(void)__blang_buffer_get( b, 42 ); /* out of bounds: guard must exit */
}
static void t_get_oob( void )
{
	CHECK_EQ_I( expect_abort( get_oob_child ), 1 );
}

static const blang_test_case cases[] = {
	{ "create",        t_create },
	{ "append_byte",   t_append_byte },
	{ "from_string",   t_from_string },
	{ "append_string", t_append_string },
	{ "get_set",       t_get_set },
	{ "to_string",     t_to_string },
	{ "slice",         t_slice },
	{ "index_of",      t_index_of },
	{ "clear",         t_clear },
	{ "get_oob",       t_get_oob },
};
TEST_MAIN( cases )
