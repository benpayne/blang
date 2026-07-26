/* runtime/blang_hash.c — BLang hash module C backing (FNV-1a). */
#include "blang_hash.h"

#include <stdlib.h>

int32_t __blang_hash_string( BlangString *s )
{
	if ( s == NULL )
		return 0;
	const char *c = __blang_string_to_cstring( s ); /* caller-frees copy */
	if ( c == NULL )
		return 0;

	/* FNV-1a, 64-bit accumulation for good mixing. Deterministic + platform-
	   stable. */
	uint64_t h = 1469598103934665603ULL;       /* FNV offset basis */
	for ( const char *p = c; *p != '\0'; ++p )
	{
		h ^= (unsigned char)*p;
		h *= 1099511628211ULL;                  /* FNV prime */
	}
	free( (void *)c );

	/* Fold to 32 bits, then mask to 31 so the BLang `int` result is always
	   non-negative. Ample for bucket-index modulo. */
	uint32_t folded = (uint32_t)( h ^ ( h >> 32 ) );
	return (int32_t)( folded & 0x7FFFFFFFU );
}
