#include "blang_buffer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Minimum default capacity when 0 is requested. */
#define DEFAULT_CAPACITY 64

/* ========================================================================
   Helper: ensure capacity for at least `needed` bytes
   ======================================================================== */

static void ensure_capacity( BlangBuffer *buf, int64_t needed )
{
	if ( needed <= buf->capacity )
		return;

	int64_t new_cap = buf->capacity;
	if ( new_cap < DEFAULT_CAPACITY )
		new_cap = DEFAULT_CAPACITY;
	while ( new_cap < needed )
		new_cap *= 2;

	buf->data = (uint8_t *)realloc( buf->data, (size_t)new_cap );
	if ( buf->data == NULL )
	{
		fprintf( stderr, "blang: buffer realloc failed\n" );
		exit( 1 );
	}
	buf->capacity = new_cap;
}

/* ========================================================================
   Lifecycle
   ======================================================================== */

BlangBuffer *__blang_buffer_create( int64_t capacity )
{
	if ( capacity < DEFAULT_CAPACITY )
		capacity = DEFAULT_CAPACITY;

	BlangBuffer *buf = (BlangBuffer *)calloc( 1, sizeof( BlangBuffer ) );
	if ( buf == NULL )
	{
		fprintf( stderr, "blang: buffer alloc failed\n" );
		exit( 1 );
	}
	buf->length = 0;
	buf->capacity = capacity;
	buf->ref_count = 1;
	buf->data = (uint8_t *)calloc( (size_t)capacity, 1 );
	if ( buf->data == NULL )
	{
		fprintf( stderr, "blang: buffer data alloc failed\n" );
		exit( 1 );
	}
	return buf;
}

BlangBuffer *__blang_buffer_create_from_string( BlangString *s )
{
	if ( s == NULL )
		return __blang_buffer_create( DEFAULT_CAPACITY );

	int64_t len = s->length;
	int64_t cap = len;
	if ( cap < DEFAULT_CAPACITY )
		cap = DEFAULT_CAPACITY;

	BlangBuffer *buf = (BlangBuffer *)calloc( 1, sizeof( BlangBuffer ) );
	if ( buf == NULL )
	{
		fprintf( stderr, "blang: buffer alloc failed\n" );
		exit( 1 );
	}
	buf->length = len;
	buf->capacity = cap;
	buf->ref_count = 1;
	buf->data = (uint8_t *)calloc( (size_t)cap, 1 );
	if ( buf->data == NULL )
	{
		fprintf( stderr, "blang: buffer data alloc failed\n" );
		exit( 1 );
	}
	if ( len > 0 )
		memcpy( buf->data, s->data, (size_t)len );
	return buf;
}

void __blang_buffer_retain( BlangBuffer *buf )
{
	if ( buf == NULL )
		return;
	__atomic_add_fetch( &buf->ref_count, 1, __ATOMIC_SEQ_CST );
}

void __blang_buffer_release( BlangBuffer *buf )
{
	if ( buf == NULL )
		return;
	int32_t new_count = __atomic_sub_fetch( &buf->ref_count, 1, __ATOMIC_SEQ_CST );
	if ( new_count <= 0 )
	{
		free( buf->data );
		free( buf );
	}
}

/* ========================================================================
   Properties
   ======================================================================== */

int64_t __blang_buffer_length( BlangBuffer *buf )
{
	if ( buf == NULL )
		return 0;
	return buf->length;
}

int64_t __blang_buffer_capacity( BlangBuffer *buf )
{
	if ( buf == NULL )
		return 0;
	return buf->capacity;
}

int32_t __blang_buffer_is_empty( BlangBuffer *buf )
{
	if ( buf == NULL )
		return 1;
	return buf->length == 0 ? 1 : 0;
}

/* ========================================================================
   Read/write (bounds-checked)
   ======================================================================== */

int32_t __blang_buffer_get( BlangBuffer *buf, int64_t index )
{
	if ( buf == NULL )
	{
		fprintf( stderr, "blang: buffer get on null buffer\n" );
		exit( 1 );
	}
	if ( index < 0 || index >= buf->length )
	{
		fprintf( stderr, "blang: buffer index out of bounds: index %lld, length %lld\n",
			(long long)index, (long long)buf->length );
		exit( 1 );
	}
	return (int32_t)buf->data[index];
}

void __blang_buffer_set( BlangBuffer *buf, int64_t index, int32_t value )
{
	if ( buf == NULL )
	{
		fprintf( stderr, "blang: buffer set on null buffer\n" );
		exit( 1 );
	}
	if ( index < 0 || index >= buf->length )
	{
		fprintf( stderr, "blang: buffer index out of bounds: index %lld, length %lld\n",
			(long long)index, (long long)buf->length );
		exit( 1 );
	}
	buf->data[index] = (uint8_t)( value & 0xFF );
}

/* ========================================================================
   Append
   ======================================================================== */

void __blang_buffer_append_byte( BlangBuffer *buf, int32_t byte )
{
	if ( buf == NULL )
	{
		fprintf( stderr, "blang: buffer append_byte on null buffer\n" );
		exit( 1 );
	}
	ensure_capacity( buf, buf->length + 1 );
	buf->data[buf->length] = (uint8_t)( byte & 0xFF );
	buf->length++;
}

void __blang_buffer_append_bytes( BlangBuffer *buf, BlangBuffer *src, int64_t len )
{
	if ( buf == NULL )
	{
		fprintf( stderr, "blang: buffer append_bytes on null buffer\n" );
		exit( 1 );
	}
	if ( src == NULL || len <= 0 )
		return;
	if ( len > src->length )
		len = src->length;
	ensure_capacity( buf, buf->length + len );
	memcpy( buf->data + buf->length, src->data, (size_t)len );
	buf->length += len;
}

void __blang_buffer_append_string( BlangBuffer *buf, BlangString *s )
{
	if ( buf == NULL )
	{
		fprintf( stderr, "blang: buffer append_string on null buffer\n" );
		exit( 1 );
	}
	if ( s == NULL || s->length <= 0 )
		return;
	ensure_capacity( buf, buf->length + s->length );
	memcpy( buf->data + buf->length, s->data, (size_t)s->length );
	buf->length += s->length;
}

/* ========================================================================
   Search and slice
   ======================================================================== */

int64_t __blang_buffer_index_of( BlangBuffer *buf, BlangBuffer *pattern, int64_t offset )
{
	if ( buf == NULL || pattern == NULL )
		return -1;
	if ( offset < 0 )
		offset = 0;
	if ( pattern->length <= 0 )
		return offset;
	if ( offset + pattern->length > buf->length )
		return -1;

	for ( int64_t i = offset; i <= buf->length - pattern->length; i++ )
	{
		if ( memcmp( buf->data + i, pattern->data, (size_t)pattern->length ) == 0 )
			return i;
	}
	return -1;
}

BlangBuffer *__blang_buffer_slice( BlangBuffer *buf, int64_t start, int64_t end )
{
	if ( buf == NULL )
		return __blang_buffer_create( DEFAULT_CAPACITY );

	if ( start < 0 )
		start = 0;
	if ( end > buf->length )
		end = buf->length;
	if ( start >= end )
		return __blang_buffer_create( DEFAULT_CAPACITY );

	int64_t len = end - start;
	int64_t cap = len;
	if ( cap < DEFAULT_CAPACITY )
		cap = DEFAULT_CAPACITY;

	BlangBuffer *result = (BlangBuffer *)calloc( 1, sizeof( BlangBuffer ) );
	if ( result == NULL )
	{
		fprintf( stderr, "blang: buffer alloc failed\n" );
		exit( 1 );
	}
	result->length = len;
	result->capacity = cap;
	result->ref_count = 1;
	result->data = (uint8_t *)calloc( (size_t)cap, 1 );
	if ( result->data == NULL )
	{
		fprintf( stderr, "blang: buffer data alloc failed\n" );
		exit( 1 );
	}
	memcpy( result->data, buf->data + start, (size_t)len );
	return result;
}

/* ========================================================================
   Conversion
   ======================================================================== */

BlangString *__blang_buffer_to_string( BlangBuffer *buf )
{
	if ( buf == NULL )
		return __blang_string_create( "", 0 );
	return __blang_string_create( (const char *)buf->data, buf->length );
}

BlangString *__blang_buffer_to_string_range( BlangBuffer *buf, int64_t start, int64_t end )
{
	if ( buf == NULL )
		return __blang_string_create( "", 0 );

	if ( start < 0 )
		start = 0;
	if ( end > buf->length )
		end = buf->length;
	if ( start >= end )
		return __blang_string_create( "", 0 );

	return __blang_string_create( (const char *)buf->data + start, end - start );
}

/* ========================================================================
   Management
   ======================================================================== */

void __blang_buffer_clear( BlangBuffer *buf )
{
	if ( buf == NULL )
		return;
	buf->length = 0;
}

void __blang_buffer_compact( BlangBuffer *buf, int64_t bytes )
{
	if ( buf == NULL || bytes <= 0 )
		return;
	if ( bytes >= buf->length )
	{
		buf->length = 0;
		return;
	}
	memmove( buf->data, buf->data + bytes, (size_t)( buf->length - bytes ) );
	buf->length -= bytes;
}
