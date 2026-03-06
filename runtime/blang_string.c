#include "blang_string.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ========================================================================
   Creation
   ======================================================================== */

BlangString *__blang_string_create( const char *data, int64_t length )
{
	BlangString *s = (BlangString *)malloc( sizeof( BlangString ) );
	if ( s == NULL )
	{
		fprintf( stderr, "blang: out of memory allocating string\n" );
		exit( 1 );
	}

	char *buf = (char *)malloc( length + 1 );
	if ( buf == NULL )
	{
		fprintf( stderr, "blang: out of memory allocating string data\n" );
		free( s );
		exit( 1 );
	}

	if ( data != NULL && length > 0 )
		memcpy( buf, data, length );
	buf[length] = '\0';

	s->data = buf;
	s->length = length;
	s->capacity = length + 1;
	s->ref_count = 1;
	return s;
}

BlangString *__blang_string_create_static( const char *static_data, int64_t length )
{
	BlangString *s = (BlangString *)malloc( sizeof( BlangString ) );
	if ( s == NULL )
	{
		fprintf( stderr, "blang: out of memory allocating string\n" );
		exit( 1 );
	}

	s->data = static_data;
	s->length = length;
	s->capacity = 0;  /* sentinel: do not free data */
	s->ref_count = 1;
	return s;
}

/* ========================================================================
   Reference Counting
   ======================================================================== */

void __blang_string_retain( BlangString *s )
{
	if ( s == NULL )
		return;
	__atomic_add_fetch( &s->ref_count, 1, __ATOMIC_SEQ_CST );
}

void __blang_string_release( BlangString *s )
{
	if ( s == NULL )
		return;
	int32_t new_count = __atomic_sub_fetch( &s->ref_count, 1, __ATOMIC_SEQ_CST );
	if ( new_count <= 0 )
	{
		if ( s->capacity > 0 )
			free( (void *)s->data );
		free( s );
	}
}

/* ========================================================================
   Properties
   ======================================================================== */

int64_t __blang_string_length( BlangString *s )
{
	if ( s == NULL )
		return 0;
	return s->length;
}

bool __blang_string_is_empty( BlangString *s )
{
	if ( s == NULL )
		return true;
	return s->length == 0;
}

/* ========================================================================
   Access
   ======================================================================== */

char __blang_string_char_at( BlangString *s, int64_t index )
{
	if ( s == NULL || index < 0 || index >= s->length )
	{
		fprintf( stderr, "blang: string index out of bounds: index %lld, length %lld\n",
			(long long)index, (long long)( s ? s->length : 0 ) );
		exit( 1 );
	}
	return s->data[index];
}

int32_t __blang_string_byte_at( BlangString *s, int64_t index )
{
	if ( s == NULL || index < 0 || index >= s->length )
	{
		fprintf( stderr, "blang: string byte index out of bounds: index %lld, length %lld\n",
			(long long)index, (long long)( s ? s->length : 0 ) );
		exit( 1 );
	}
	return (int32_t)(unsigned char)s->data[index];
}

BlangString *__blang_string_substring( BlangString *s, int64_t start, int64_t end )
{
	if ( s == NULL )
		return __blang_string_create( "", 0 );

	/* Clamp bounds */
	if ( start < 0 )
		start = 0;
	if ( end > s->length )
		end = s->length;
	if ( start >= end )
		return __blang_string_create( "", 0 );

	return __blang_string_create( s->data + start, end - start );
}

/* ========================================================================
   Search
   ======================================================================== */

/* Internal: find needle in haystack starting at offset.  Returns index or -1. */
static int64_t find_substring( const char *haystack, int64_t haystack_len,
	const char *needle, int64_t needle_len, int64_t offset )
{
	if ( needle_len == 0 )
		return offset;
	if ( needle_len > haystack_len - offset )
		return -1;

	for ( int64_t i = offset; i <= haystack_len - needle_len; i++ )
	{
		if ( memcmp( haystack + i, needle, needle_len ) == 0 )
			return i;
	}
	return -1;
}

bool __blang_string_contains( BlangString *s, BlangString *needle )
{
	if ( s == NULL || needle == NULL )
		return false;
	return find_substring( s->data, s->length, needle->data, needle->length, 0 ) >= 0;
}

bool __blang_string_starts_with( BlangString *s, BlangString *prefix )
{
	if ( s == NULL || prefix == NULL )
		return false;
	if ( prefix->length > s->length )
		return false;
	return memcmp( s->data, prefix->data, prefix->length ) == 0;
}

bool __blang_string_ends_with( BlangString *s, BlangString *suffix )
{
	if ( s == NULL || suffix == NULL )
		return false;
	if ( suffix->length > s->length )
		return false;
	return memcmp( s->data + s->length - suffix->length, suffix->data, suffix->length ) == 0;
}

int64_t __blang_string_index_of( BlangString *s, BlangString *needle )
{
	if ( s == NULL || needle == NULL )
		return -1;
	return find_substring( s->data, s->length, needle->data, needle->length, 0 );
}

/* ========================================================================
   Transform
   ======================================================================== */

BlangString *__blang_string_to_upper( BlangString *s )
{
	if ( s == NULL )
		return __blang_string_create( "", 0 );

	BlangString *result = __blang_string_create( s->data, s->length );
	char *buf = (char *)result->data;
	for ( int64_t i = 0; i < result->length; i++ )
		buf[i] = (char)toupper( (unsigned char)buf[i] );
	return result;
}

BlangString *__blang_string_to_lower( BlangString *s )
{
	if ( s == NULL )
		return __blang_string_create( "", 0 );

	BlangString *result = __blang_string_create( s->data, s->length );
	char *buf = (char *)result->data;
	for ( int64_t i = 0; i < result->length; i++ )
		buf[i] = (char)tolower( (unsigned char)buf[i] );
	return result;
}

BlangString *__blang_string_trim( BlangString *s )
{
	if ( s == NULL || s->length == 0 )
		return __blang_string_create( "", 0 );

	int64_t start = 0;
	int64_t end = s->length;

	while ( start < end && isspace( (unsigned char)s->data[start] ) )
		start++;
	while ( end > start && isspace( (unsigned char)s->data[end - 1] ) )
		end--;

	return __blang_string_create( s->data + start, end - start );
}

BlangString *__blang_string_replace( BlangString *s, BlangString *old_str, BlangString *new_str )
{
	if ( s == NULL || old_str == NULL || new_str == NULL )
		return s ? __blang_string_create( s->data, s->length ) : __blang_string_create( "", 0 );

	if ( old_str->length == 0 )
		return __blang_string_create( s->data, s->length );

	/* Count occurrences to determine result size. */
	int64_t count = 0;
	int64_t pos = 0;
	while ( ( pos = find_substring( s->data, s->length, old_str->data, old_str->length, pos ) ) >= 0 )
	{
		count++;
		pos += old_str->length;
	}

	if ( count == 0 )
		return __blang_string_create( s->data, s->length );

	int64_t new_length = s->length + count * ( new_str->length - old_str->length );
	char *buf = (char *)malloc( new_length + 1 );
	if ( buf == NULL )
	{
		fprintf( stderr, "blang: out of memory in string replace\n" );
		exit( 1 );
	}

	int64_t src = 0;
	int64_t dst = 0;
	while ( src < s->length )
	{
		int64_t match = find_substring( s->data, s->length, old_str->data, old_str->length, src );
		if ( match < 0 )
		{
			memcpy( buf + dst, s->data + src, s->length - src );
			dst += s->length - src;
			break;
		}

		/* Copy bytes before the match. */
		if ( match > src )
		{
			memcpy( buf + dst, s->data + src, match - src );
			dst += match - src;
		}

		/* Copy replacement. */
		if ( new_str->length > 0 )
		{
			memcpy( buf + dst, new_str->data, new_str->length );
			dst += new_str->length;
		}

		src = match + old_str->length;
	}

	buf[new_length] = '\0';

	BlangString *result = (BlangString *)malloc( sizeof( BlangString ) );
	if ( result == NULL )
	{
		fprintf( stderr, "blang: out of memory in string replace\n" );
		free( buf );
		exit( 1 );
	}
	result->data = buf;
	result->length = new_length;
	result->capacity = new_length + 1;
	result->ref_count = 1;
	return result;
}

/* ========================================================================
   Concatenation
   ======================================================================== */

BlangString *__blang_string_concat( BlangString *a, BlangString *b )
{
	int64_t a_len = ( a != NULL ) ? a->length : 0;
	int64_t b_len = ( b != NULL ) ? b->length : 0;
	int64_t total = a_len + b_len;

	char *buf = (char *)malloc( total + 1 );
	if ( buf == NULL )
	{
		fprintf( stderr, "blang: out of memory in string concat\n" );
		exit( 1 );
	}

	if ( a_len > 0 )
		memcpy( buf, a->data, a_len );
	if ( b_len > 0 )
		memcpy( buf + a_len, b->data, b_len );
	buf[total] = '\0';

	BlangString *result = (BlangString *)malloc( sizeof( BlangString ) );
	if ( result == NULL )
	{
		fprintf( stderr, "blang: out of memory in string concat\n" );
		free( buf );
		exit( 1 );
	}
	result->data = buf;
	result->length = total;
	result->capacity = total + 1;
	result->ref_count = 1;
	return result;
}

BlangString *__blang_string_concat_many( BlangString **strings, int64_t count )
{
	if ( strings == NULL || count <= 0 )
		return __blang_string_create( "", 0 );

	/* Calculate total length. */
	int64_t total = 0;
	for ( int64_t i = 0; i < count; i++ )
	{
		if ( strings[i] != NULL )
			total += strings[i]->length;
	}

	char *buf = (char *)malloc( total + 1 );
	if ( buf == NULL )
	{
		fprintf( stderr, "blang: out of memory in string concat_many\n" );
		exit( 1 );
	}

	int64_t offset = 0;
	for ( int64_t i = 0; i < count; i++ )
	{
		if ( strings[i] != NULL && strings[i]->length > 0 )
		{
			memcpy( buf + offset, strings[i]->data, strings[i]->length );
			offset += strings[i]->length;
		}
	}
	buf[total] = '\0';

	BlangString *result = (BlangString *)malloc( sizeof( BlangString ) );
	if ( result == NULL )
	{
		fprintf( stderr, "blang: out of memory in string concat_many\n" );
		free( buf );
		exit( 1 );
	}
	result->data = buf;
	result->length = total;
	result->capacity = total + 1;
	result->ref_count = 1;
	return result;
}

/* ========================================================================
   Comparison
   ======================================================================== */

bool __blang_string_equals( BlangString *a, BlangString *b )
{
	if ( a == b )
		return true;
	if ( a == NULL || b == NULL )
		return false;
	if ( a->length != b->length )
		return false;
	return memcmp( a->data, b->data, a->length ) == 0;
}

int32_t __blang_string_compare( BlangString *a, BlangString *b )
{
	if ( a == b )
		return 0;
	if ( a == NULL )
		return -1;
	if ( b == NULL )
		return 1;

	int64_t min_len = ( a->length < b->length ) ? a->length : b->length;
	int cmp = memcmp( a->data, b->data, min_len );
	if ( cmp != 0 )
		return ( cmp < 0 ) ? -1 : 1;

	if ( a->length < b->length )
		return -1;
	if ( a->length > b->length )
		return 1;
	return 0;
}

/* ========================================================================
   Conversion
   ======================================================================== */

const char *__blang_string_to_cstring( BlangString *s )
{
	if ( s == NULL )
	{
		char *empty = (char *)malloc( 1 );
		if ( empty != NULL )
			empty[0] = '\0';
		return empty;
	}

	char *buf = (char *)malloc( s->length + 1 );
	if ( buf == NULL )
	{
		fprintf( stderr, "blang: out of memory in string to_cstring\n" );
		exit( 1 );
	}
	memcpy( buf, s->data, s->length );
	buf[s->length] = '\0';
	return buf;
}

int64_t __blang_string_to_int( BlangString *s, bool *ok )
{
	if ( s == NULL || s->length == 0 )
	{
		if ( ok != NULL )
			*ok = false;
		return 0;
	}

	/* Need a null-terminated copy for strtol. */
	const char *cstr = __blang_string_to_cstring( s );
	char *endptr = NULL;
	long long val = strtoll( cstr, &endptr, 10 );

	bool success = ( endptr != cstr && *endptr == '\0' );
	if ( ok != NULL )
		*ok = success;

	free( (void *)cstr );
	return success ? (int64_t)val : 0;
}

double __blang_string_to_float( BlangString *s, bool *ok )
{
	if ( s == NULL || s->length == 0 )
	{
		if ( ok != NULL )
			*ok = false;
		return 0.0;
	}

	const char *cstr = __blang_string_to_cstring( s );
	char *endptr = NULL;
	double val = strtod( cstr, &endptr );

	bool success = ( endptr != cstr && *endptr == '\0' );
	if ( ok != NULL )
		*ok = success;

	free( (void *)cstr );
	return success ? val : 0.0;
}

/* ========================================================================
   Primitive to String Conversion (for string interpolation)
   ======================================================================== */

BlangString *__blang_int_to_string( int64_t value )
{
	char buf[32];
	int len = snprintf( buf, sizeof( buf ), "%lld", (long long)value );
	return __blang_string_create( buf, len );
}

BlangString *__blang_float_to_string( double value )
{
	char buf[64];
	int len = snprintf( buf, sizeof( buf ), "%g", value );
	return __blang_string_create( buf, len );
}

BlangString *__blang_bool_to_string( bool value )
{
	if ( value )
		return __blang_string_create_static( "true", 4 );
	else
		return __blang_string_create_static( "false", 5 );
}
