/* runtime/blang_env.c — BLang env module C backing (getenv). */
#include "blang_env.h"

#include <stdlib.h>
#include <string.h>

BlangString *__blang_env_get( BlangString *name )
{
	if ( name == NULL )
		return NULL;
	const char *cname = __blang_string_to_cstring( name ); /* caller-frees copy */
	if ( cname == NULL )
		return NULL;
	const char *val = getenv( cname );
	free( (void *)cname );
	if ( val == NULL )
		return NULL;                     /* unset -> BLang Option.none */
	/* Fresh, owned (+1) BlangString — the caller releases it (owned-return
	   contract). No shared global involved. */
	return __blang_string_create( val, (int64_t)strlen( val ) );
}

bool __blang_env_has( BlangString *name )
{
	if ( name == NULL )
		return false;
	const char *cname = __blang_string_to_cstring( name );
	if ( cname == NULL )
		return false;
	bool present = ( getenv( cname ) != NULL );
	free( (void *)cname );
	return present;
}
