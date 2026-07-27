/* Unit tests for runtime/blang_string.c (BlangString) */
#include "test_util.h"
#include "../blang_string.h"

static BlangString *S( const char *c )
{
	return __blang_string_create( c, (int64_t)strlen( c ) );
}
static void expect_cstr( BlangString *s, const char *exp )
{
	const char *c = __blang_string_to_cstring( s );
	CHECK_STR_EQ( c, exp );
	free( (void *)c );
}

static void t_create_len( void )
{
	BlangString *s = S( "hello" );
	CHECK_EQ_I( __blang_string_length( s ), 5 );
	CHECK_EQ_I( __blang_string_char_at( s, 0 ), 'h' );
	CHECK_EQ_I( __blang_string_char_at( s, 4 ), 'o' );
	CHECK_FALSE( __blang_string_is_empty( s ) );
	__blang_string_release( s );
}

static void t_empty( void )
{
	BlangString *s = S( "" );
	CHECK_TRUE( __blang_string_is_empty( s ) );
	CHECK_EQ_I( __blang_string_length( s ), 0 );
	__blang_string_release( s );
}

static void t_equals( void )
{
	BlangString *a = S( "abc" ), *b = S( "abc" ), *c = S( "abd" );
	CHECK_TRUE( __blang_string_equals( a, b ) );
	CHECK_FALSE( __blang_string_equals( a, c ) );
	__blang_string_release( a ); __blang_string_release( b ); __blang_string_release( c );
}

static void t_concat( void )
{
	BlangString *a = S( "foo" ), *b = S( "bar" );
	BlangString *r = __blang_string_concat( a, b );
	expect_cstr( r, "foobar" );
	CHECK_EQ_I( __blang_string_length( r ), 6 );
	__blang_string_release( a ); __blang_string_release( b ); __blang_string_release( r );
}

static void t_substring( void )
{
	BlangString *s = S( "hello" );
	BlangString *r = __blang_string_substring( s, 1, 4 );
	expect_cstr( r, "ell" );
	__blang_string_release( s ); __blang_string_release( r );
}

static void t_to_upper( void )
{
	BlangString *s = S( "Hello" ), *r = __blang_string_to_upper( s );
	expect_cstr( r, "HELLO" );
	__blang_string_release( s ); __blang_string_release( r );
}

static void t_to_lower( void )
{
	BlangString *s = S( "Hello" ), *r = __blang_string_to_lower( s );
	expect_cstr( r, "hello" );
	__blang_string_release( s ); __blang_string_release( r );
}

static void t_trim( void )
{
	BlangString *s = S( "  hi  " ), *r = __blang_string_trim( s );
	expect_cstr( r, "hi" );
	__blang_string_release( s ); __blang_string_release( r );
}

static void t_contains( void )
{
	BlangString *s = S( "hello" ), *y = S( "ell" ), *n = S( "xyz" );
	CHECK_TRUE( __blang_string_contains( s, y ) );
	CHECK_FALSE( __blang_string_contains( s, n ) );
	__blang_string_release( s ); __blang_string_release( y ); __blang_string_release( n );
}

static void t_starts_ends( void )
{
	BlangString *s = S( "hello" ), *h = S( "he" ), *o = S( "lo" );
	CHECK_TRUE( __blang_string_starts_with( s, h ) );
	CHECK_TRUE( __blang_string_ends_with( s, o ) );
	CHECK_FALSE( __blang_string_starts_with( s, o ) );
	__blang_string_release( s ); __blang_string_release( h ); __blang_string_release( o );
}

static void t_index_of( void )
{
	BlangString *s = S( "hello" ), *l = S( "l" ), *z = S( "z" );
	CHECK_EQ_I( __blang_string_index_of( s, l ), 2 );
	CHECK_EQ_I( __blang_string_index_of( s, z ), -1 );
	__blang_string_release( s ); __blang_string_release( l ); __blang_string_release( z );
}

static void t_compare( void )
{
	BlangString *a = S( "a" ), *b = S( "b" ), *a2 = S( "a" );
	CHECK_TRUE( __blang_string_compare( a, b ) < 0 );
	CHECK_TRUE( __blang_string_compare( b, a ) > 0 );
	CHECK_EQ_I( __blang_string_compare( a, a2 ), 0 );
	__blang_string_release( a ); __blang_string_release( b ); __blang_string_release( a2 );
}

static void t_to_int( void )
{
	BlangString *ok_s = S( "42" ), *bad_s = S( "nope" );
	bool ok = false;
	CHECK_EQ_I( __blang_string_to_int( ok_s, &ok ), 42 );
	CHECK_TRUE( ok );
	ok = true;
	(void)__blang_string_to_int( bad_s, &ok );
	CHECK_FALSE( ok );
	__blang_string_release( ok_s ); __blang_string_release( bad_s );
}

static void t_replace( void )
{
	BlangString *s = S( "a-b-c" ), *o = S( "-" ), *n = S( "+" );
	BlangString *r = __blang_string_replace( s, o, n );
	expect_cstr( r, "a+b+c" );
	__blang_string_release( s ); __blang_string_release( o );
	__blang_string_release( n ); __blang_string_release( r );
}

static void t_equals_cstr( void )
{
	BlangString *s = S( "start" );
	CHECK( __blang_string_equals_cstr( s, "start", 5 ) );
	CHECK( !__blang_string_equals_cstr( s, "stop", 4 ) );
	CHECK( !__blang_string_equals_cstr( s, "star", 4 ) );   /* prefix, shorter len */
	CHECK( !__blang_string_equals_cstr( s, "starts", 6 ) ); /* longer len */
	CHECK( !__blang_string_equals_cstr( NULL, "start", 5 ) );
	CHECK( !__blang_string_equals_cstr( s, NULL, 5 ) );
	BlangString *e = S( "" );
	CHECK( __blang_string_equals_cstr( e, "", 0 ) );
	__blang_string_release( s ); __blang_string_release( e );
}

/* bounds guard probe */
static void char_at_oob_child( void )
{
	BlangString *s = __blang_string_create( "hi", 2 );
	(void)__blang_string_char_at( s, 99 ); /* out of bounds: guard must exit(1) */
}
static void t_char_at_oob( void )
{
	CHECK_EQ_I( expect_abort( char_at_oob_child ), 1 );
}

static const blang_test_case cases[] = {
	{ "create_len",  t_create_len },
	{ "empty",       t_empty },
	{ "equals",      t_equals },
	{ "concat",      t_concat },
	{ "substring",   t_substring },
	{ "to_upper",    t_to_upper },
	{ "to_lower",    t_to_lower },
	{ "trim",        t_trim },
	{ "contains",    t_contains },
	{ "starts_ends", t_starts_ends },
	{ "index_of",    t_index_of },
	{ "compare",     t_compare },
	{ "to_int",      t_to_int },
	{ "replace",     t_replace },
	{ "equals_cstr", t_equals_cstr },
	{ "char_at_oob", t_char_at_oob },
};
TEST_MAIN( cases )
