/* Unit tests for runtime/blang_json.c (BlangJsonValue encode/decode) */
#include "test_util.h"
#include "../blang_json.h"

static void t_decode_int( void )
{
	const char *err = NULL;
	BlangJsonValue *v = __blang_json_decode( "42", &err );
	CHECK( v != NULL );
	CHECK_EQ_I( __blang_json_get_int( v ), 42 );
	__blang_json_free( v );
}

static void t_decode_string( void )
{
	const char *err = NULL;
	BlangJsonValue *v = __blang_json_decode( "\"hi\"", &err );
	CHECK( v != NULL );
	CHECK_STR_EQ( __blang_json_get_string( v ), "hi" );
	__blang_json_free( v );
}

static void t_decode_bool( void )
{
	const char *err = NULL;
	BlangJsonValue *t = __blang_json_decode( "true", &err );
	BlangJsonValue *f = __blang_json_decode( "false", &err );
	CHECK_EQ_I( __blang_json_get_bool( t ), 1 );
	CHECK_EQ_I( __blang_json_get_bool( f ), 0 );
	__blang_json_free( t );
	__blang_json_free( f );
}

static void t_decode_error( void )
{
	const char *err = NULL;
	BlangJsonValue *v = __blang_json_decode( "{ not valid json", &err );
	CHECK( v == NULL );      /* malformed input -> NULL */
	CHECK( err != NULL );    /* and an error message is set */
}

static void t_object_roundtrip( void )
{
	BlangJsonValue *obj = __blang_json_object();
	__blang_json_object_set( obj, "n", __blang_json_int( 7 ) );
	__blang_json_object_set( obj, "s", __blang_json_string( "x" ) );
	char *enc = __blang_json_encode( obj );
	CHECK( enc != NULL );

	const char *err = NULL;
	BlangJsonValue *dec = __blang_json_decode( enc, &err );
	CHECK( dec != NULL );
	CHECK_EQ_I( __blang_json_get_int( __blang_json_object_get( dec, "n" ) ), 7 );
	CHECK_STR_EQ( __blang_json_get_string( __blang_json_object_get( dec, "s" ) ), "x" );

	free( enc );
	__blang_json_free( obj );
	__blang_json_free( dec );
}

static void t_array_build( void )
{
	BlangJsonValue *arr = __blang_json_array();
	__blang_json_array_push( arr, __blang_json_int( 1 ) );
	__blang_json_array_push( arr, __blang_json_int( 2 ) );
	__blang_json_array_push( arr, __blang_json_int( 3 ) );
	CHECK_EQ_I( arr->data.array.count, 3 );
	CHECK_EQ_I( __blang_json_get_int( arr->data.array.items[0] ), 1 );
	CHECK_EQ_I( __blang_json_get_int( arr->data.array.items[2] ), 3 );
	__blang_json_free( arr );
}

static void t_object_get_missing( void )
{
	BlangJsonValue *obj = __blang_json_object();
	__blang_json_object_set( obj, "a", __blang_json_int( 1 ) );
	CHECK( __blang_json_object_get( obj, "missing" ) == NULL );
	__blang_json_free( obj );
}

static const blang_test_case cases[] = {
	{ "decode_int",         t_decode_int },
	{ "decode_string",      t_decode_string },
	{ "decode_bool",        t_decode_bool },
	{ "decode_error",       t_decode_error },
	{ "object_roundtrip",   t_object_roundtrip },
	{ "array_build",        t_array_build },
	{ "object_get_missing", t_object_get_missing },
};
TEST_MAIN( cases )
