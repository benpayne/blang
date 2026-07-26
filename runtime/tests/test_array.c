/* Unit tests for runtime/blang_array.c (BlangArray) */
#include "test_util.h"
#include "../blang_array.h"

static BlangArray *arr_of( const int *vals, int n )
{
	BlangArray *a = __blang_array_create( (int32_t)sizeof(int), n );
	for ( int i = 0; i < n; i++ )
		__blang_array_push( a, &vals[i] );
	return a;
}

static void t_create_empty( void )
{
	BlangArray *a = __blang_array_create( (int32_t)sizeof(int), 0 );
	CHECK( a != NULL );
	CHECK_EQ_I( __blang_array_length( a ), 0 );
	CHECK_TRUE( __blang_array_is_empty( a ) );
	__blang_array_release( a );
}

static void t_push_length( void )
{
	int vals[] = { 10, 20, 30 };
	BlangArray *a = arr_of( vals, 3 );
	CHECK_EQ_I( __blang_array_length( a ), 3 );
	CHECK_FALSE( __blang_array_is_empty( a ) );
	__blang_array_release( a );
}

static void t_get( void )
{
	int vals[] = { 5, 6, 7 };
	BlangArray *a = arr_of( vals, 3 );
	int out = 0;
	__blang_array_get( a, 0, &out ); CHECK_EQ_I( out, 5 );
	__blang_array_get( a, 2, &out ); CHECK_EQ_I( out, 7 );
	__blang_array_release( a );
}

static void t_set( void )
{
	int vals[] = { 1, 2, 3 };
	BlangArray *a = arr_of( vals, 3 );
	int nv = 99, out = 0;
	__blang_array_set( a, 1, &nv );
	__blang_array_get( a, 1, &out );
	CHECK_EQ_I( out, 99 );
	__blang_array_release( a );
}

static void t_pop( void )
{
	int vals[] = { 4, 8 };
	BlangArray *a = arr_of( vals, 2 );
	int out = 0;
	CHECK_TRUE( __blang_array_pop( a, &out ) );
	CHECK_EQ_I( out, 8 );
	CHECK_EQ_I( __blang_array_length( a ), 1 );
	__blang_array_release( a );
}

static void t_pop_empty( void )
{
	BlangArray *a = __blang_array_create( (int32_t)sizeof(int), 0 );
	int out = 0;
	CHECK_FALSE( __blang_array_pop( a, &out ) ); /* returns false when empty */
	__blang_array_release( a );
}

static void t_insert_remove( void )
{
	int vals[] = { 1, 3 };
	BlangArray *a = arr_of( vals, 2 );
	int mid = 2, out = 0;
	__blang_array_insert( a, 1, &mid );          /* -> 1,2,3 */
	CHECK_EQ_I( __blang_array_length( a ), 3 );
	__blang_array_get( a, 1, &out ); CHECK_EQ_I( out, 2 );
	__blang_array_remove( a, 0, &out );          /* removes 1 -> 2,3 */
	CHECK_EQ_I( out, 1 );
	CHECK_EQ_I( __blang_array_length( a ), 2 );
	__blang_array_get( a, 0, &out ); CHECK_EQ_I( out, 2 );
	__blang_array_release( a );
}

static void t_concat( void )
{
	int a1[] = { 1, 2 }, a2[] = { 3, 4 };
	BlangArray *x = arr_of( a1, 2 ), *y = arr_of( a2, 2 );
	BlangArray *z = __blang_array_concat( x, y );
	CHECK_EQ_I( __blang_array_length( z ), 4 );
	int out = 0;
	__blang_array_get( z, 3, &out ); CHECK_EQ_I( out, 4 );
	__blang_array_release( x ); __blang_array_release( y ); __blang_array_release( z );
}

static void t_clear( void )
{
	int vals[] = { 1, 2, 3 };
	BlangArray *a = arr_of( vals, 3 );
	__blang_array_clear( a );
	CHECK_EQ_I( __blang_array_length( a ), 0 );
	CHECK_TRUE( __blang_array_is_empty( a ) );
	__blang_array_release( a );
}

static void t_grow( void )
{
	/* push well past initial capacity; values must survive reallocation */
	BlangArray *a = __blang_array_create( (int32_t)sizeof(int), 1 );
	for ( int i = 0; i < 100; i++ )
		__blang_array_push( a, &i );
	CHECK_EQ_I( __blang_array_length( a ), 100 );
	int out = 0;
	__blang_array_get( a, 99, &out ); CHECK_EQ_I( out, 99 );
	__blang_array_get( a, 50, &out ); CHECK_EQ_I( out, 50 );
	__blang_array_release( a );
}

/* --- bounds/null guard probes (assert the process aborts) --- */
static void oob_get_child( void )
{
	int vals[] = { 1, 2, 3 };
	BlangArray *a = arr_of( vals, 3 );
	int out = 0;
	__blang_array_get( a, 99, &out ); /* out-of-bounds: guard must exit(1) */
}
static void t_get_oob( void )
{
	/* THE bounds-teeth test: passes only because the guard aborts. Remove the
	 * index check in __blang_array_get and (normal build) this flips to 0. */
	CHECK_EQ_I( expect_abort( oob_get_child ), 1 );
}

static void oob_set_child( void )
{
	int vals[] = { 1 };
	BlangArray *a = arr_of( vals, 1 );
	int nv = 7;
	__blang_array_set( a, 42, &nv ); /* out-of-bounds */
}
static void t_set_oob( void )
{
	CHECK_EQ_I( expect_abort( oob_set_child ), 1 );
}

static void null_get_child( void )
{
	int out = 0;
	__blang_array_get( NULL, 0, &out ); /* null array: guard must exit(1) */
}
static void t_get_null( void )
{
	CHECK_EQ_I( expect_abort( null_get_child ), 1 );
}

static const blang_test_case cases[] = {
	{ "create_empty", t_create_empty },
	{ "push_length",  t_push_length },
	{ "get",          t_get },
	{ "set",          t_set },
	{ "pop",          t_pop },
	{ "pop_empty",    t_pop_empty },
	{ "insert_remove", t_insert_remove },
	{ "concat",       t_concat },
	{ "clear",        t_clear },
	{ "grow",         t_grow },
	{ "get_oob",      t_get_oob },
	{ "set_oob",      t_set_oob },
	{ "get_null",     t_get_null },
};
TEST_MAIN( cases )
