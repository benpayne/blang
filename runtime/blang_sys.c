#include "blang_sys.h"

#include <stdlib.h>
#include <string.h>

/* Global args array — populated once by __blang_sys_init */
static BlangArray *g_sys_args = NULL;

void __blang_sys_init( int argc, char **argv )
{
	if ( g_sys_args != NULL )
		return;  /* already initialized */

	/* Create an Array<string> — each element is a BlangString* (pointer-sized) */
	g_sys_args = __blang_array_create( (int32_t)sizeof( BlangString * ), argc );

	for ( int i = 0; i < argc; i++ )
	{
		const char *arg = argv[i];
		int64_t len = (int64_t)strlen( arg );
		BlangString *s = __blang_string_create( arg, len );
		__blang_array_push( g_sys_args, &s );
	}
}

BlangArray *__blang_sys_get_args( void )
{
	if ( g_sys_args == NULL )
	{
		/* If never initialized, return empty array */
		g_sys_args = __blang_array_create( (int32_t)sizeof( BlangString * ), 0 );
	}
	return g_sys_args;
}

void __blang_sys_exit( int code )
{
	exit( code );
}
