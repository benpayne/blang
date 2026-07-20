/* Unit tests for runtime/blang_env.c (getenv wrappers). Uses setenv to create a
 * hermetic, deterministic environment. */
#include "test_util.h"
#include "../blang_env.h"
#include "../blang_string.h"

#include <stdlib.h>
#include <string.h>

static BlangString *S( const char *c )
{
	return __blang_string_create( c, (int64_t)strlen( c ) );
}

static void t_get_hit( void )
{
	setenv( "BLANG_TEST_ENV_VAR", "hello123", 1 );
	BlangString *name = S( "BLANG_TEST_ENV_VAR" );
	BlangString *val = __blang_env_get( name );          /* owned +1 */
	CHECK( val != NULL );
	const char *c = __blang_string_to_cstring( val );
	CHECK_STR_EQ( c, "hello123" );
	free( (void *)c );
	__blang_string_release( val );                       /* release owned return */
	__blang_string_release( name );
}
static void t_get_miss( void )
{
	unsetenv( "BLANG_DEFINITELY_UNSET_XYZ" );
	BlangString *name = S( "BLANG_DEFINITELY_UNSET_XYZ" );
	BlangString *val = __blang_env_get( name );
	CHECK( val == NULL );                                /* unset -> NULL (Option.none) */
	__blang_string_release( name );
}
static void t_has_true( void )
{
	setenv( "BLANG_TEST_ENV_VAR2", "x", 1 );
	BlangString *name = S( "BLANG_TEST_ENV_VAR2" );
	CHECK_TRUE( __blang_env_has( name ) );
	__blang_string_release( name );
}
static void t_has_false( void )
{
	unsetenv( "BLANG_DEFINITELY_UNSET_ABC" );
	BlangString *name = S( "BLANG_DEFINITELY_UNSET_ABC" );
	CHECK_FALSE( __blang_env_has( name ) );
	__blang_string_release( name );
}

static blang_test_case cases[] = {
	{ "get_hit", t_get_hit },
	{ "get_miss", t_get_miss },
	{ "has_true", t_has_true },
	{ "has_false", t_has_false },
};
TEST_MAIN( cases )
