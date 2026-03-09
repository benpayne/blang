#include "blang_array.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Minimum default capacity when 0 is requested. */
#define DEFAULT_CAPACITY 8

/* ========================================================================
   Helper: ensure capacity for at least `needed` elements
   ======================================================================== */

static void ensure_capacity( BlangArray *a, int64_t needed )
{
	if ( needed <= a->capacity )
		return;

	int64_t new_cap = a->capacity;
	if ( new_cap < DEFAULT_CAPACITY )
		new_cap = DEFAULT_CAPACITY;
	while ( new_cap < needed )
		new_cap *= 2;

	a->data = realloc( a->data, (size_t)new_cap * (size_t)a->elem_size );
	if ( a->data == NULL )
	{
		fprintf( stderr, "blang: array realloc failed\n" );
		exit( 1 );
	}
	a->capacity = new_cap;
}

/* Helper: pointer to element at index. */
static void *elem_ptr( BlangArray *a, int64_t index )
{
	return (char *)a->data + index * (int64_t)a->elem_size;
}

/* ========================================================================
   Creation
   ======================================================================== */

BlangArray *__blang_array_create( int32_t elem_size, int64_t initial_capacity )
{
	if ( initial_capacity < DEFAULT_CAPACITY )
		initial_capacity = DEFAULT_CAPACITY;

	BlangArray *a = (BlangArray *)calloc( 1, sizeof( BlangArray ) );
	if ( a == NULL )
	{
		fprintf( stderr, "blang: array alloc failed\n" );
		exit( 1 );
	}
	a->elem_size = elem_size;
	a->length = 0;
	a->capacity = initial_capacity;
	a->ref_count = 1;
	a->elem_dtor = NULL;
	a->data = calloc( (size_t)initial_capacity, (size_t)elem_size );
	if ( a->data == NULL )
	{
		fprintf( stderr, "blang: array data alloc failed\n" );
		exit( 1 );
	}
	return a;
}

BlangArray *__blang_array_create_from_data( int32_t elem_size, const void *data, int64_t count )
{
	int64_t cap = count;
	if ( cap < DEFAULT_CAPACITY )
		cap = DEFAULT_CAPACITY;

	BlangArray *a = (BlangArray *)calloc( 1, sizeof( BlangArray ) );
	if ( a == NULL )
	{
		fprintf( stderr, "blang: array alloc failed\n" );
		exit( 1 );
	}
	a->elem_size = elem_size;
	a->length = count;
	a->capacity = cap;
	a->ref_count = 1;
	a->elem_dtor = NULL;
	a->data = calloc( (size_t)cap, (size_t)elem_size );
	if ( a->data == NULL )
	{
		fprintf( stderr, "blang: array data alloc failed\n" );
		exit( 1 );
	}
	if ( data != NULL && count > 0 )
		memcpy( a->data, data, (size_t)count * (size_t)elem_size );
	return a;
}

/* ========================================================================
   Reference Counting
   ======================================================================== */

void __blang_array_retain( BlangArray *a )
{
	if ( a == NULL )
		return;
	__atomic_add_fetch( &a->ref_count, 1, __ATOMIC_SEQ_CST );
}

void __blang_array_release( BlangArray *a )
{
	if ( a == NULL )
		return;
	int32_t new_count = __atomic_sub_fetch( &a->ref_count, 1, __ATOMIC_SEQ_CST );
	if ( new_count <= 0 )
	{
		/* Release refcounted elements before freeing the data buffer */
		if ( a->elem_dtor != NULL && a->data != NULL )
		{
			for ( int64_t i = 0; i < a->length; i++ )
			{
				void *slot = (char *)a->data + i * (int64_t)a->elem_size;
				void *elem_val = *(void **)slot;
				if ( elem_val != NULL )
					a->elem_dtor( elem_val );
			}
		}
		free( a->data );
		free( a );
	}
}

/* ========================================================================
   Properties
   ======================================================================== */

int64_t __blang_array_length( BlangArray *a )
{
	if ( a == NULL )
		return 0;
	return a->length;
}

int64_t __blang_array_capacity( BlangArray *a )
{
	if ( a == NULL )
		return 0;
	return a->capacity;
}

bool __blang_array_is_empty( BlangArray *a )
{
	if ( a == NULL )
		return true;
	return a->length == 0;
}

/* ========================================================================
   Access (bounds-checked)
   ======================================================================== */

void __blang_array_get( BlangArray *a, int64_t index, void *out )
{
	if ( a == NULL )
	{
		fprintf( stderr, "blang: array get on null array\n" );
		exit( 1 );
	}
	if ( index < 0 || index >= a->length )
	{
		fprintf( stderr, "blang: array index out of bounds: index %lld, length %lld\n",
			(long long)index, (long long)a->length );
		exit( 1 );
	}
	memcpy( out, elem_ptr( a, index ), (size_t)a->elem_size );
}

void __blang_array_set( BlangArray *a, int64_t index, const void *value )
{
	if ( a == NULL )
	{
		fprintf( stderr, "blang: array set on null array\n" );
		exit( 1 );
	}
	if ( index < 0 || index >= a->length )
	{
		fprintf( stderr, "blang: array index out of bounds: index %lld, length %lld\n",
			(long long)index, (long long)a->length );
		exit( 1 );
	}
	memcpy( elem_ptr( a, index ), value, (size_t)a->elem_size );
}

/* ========================================================================
   Mutation
   ======================================================================== */

void __blang_array_push( BlangArray *a, const void *value )
{
	if ( a == NULL )
	{
		fprintf( stderr, "blang: array push on null array\n" );
		exit( 1 );
	}
	ensure_capacity( a, a->length + 1 );
	memcpy( elem_ptr( a, a->length ), value, (size_t)a->elem_size );
	a->length++;
}

bool __blang_array_pop( BlangArray *a, void *out )
{
	if ( a == NULL || a->length == 0 )
		return false;
	a->length--;
	if ( out != NULL )
		memcpy( out, elem_ptr( a, a->length ), (size_t)a->elem_size );
	return true;
}

void __blang_array_insert( BlangArray *a, int64_t index, const void *value )
{
	if ( a == NULL )
	{
		fprintf( stderr, "blang: array insert on null array\n" );
		exit( 1 );
	}
	if ( index < 0 || index > a->length )
	{
		fprintf( stderr, "blang: array insert index out of bounds: index %lld, length %lld\n",
			(long long)index, (long long)a->length );
		exit( 1 );
	}
	ensure_capacity( a, a->length + 1 );
	/* Shift elements right to make room. */
	if ( index < a->length )
	{
		memmove( elem_ptr( a, index + 1 ), elem_ptr( a, index ),
			(size_t)( a->length - index ) * (size_t)a->elem_size );
	}
	memcpy( elem_ptr( a, index ), value, (size_t)a->elem_size );
	a->length++;
}

void __blang_array_remove( BlangArray *a, int64_t index, void *out )
{
	if ( a == NULL )
	{
		fprintf( stderr, "blang: array remove on null array\n" );
		exit( 1 );
	}
	if ( index < 0 || index >= a->length )
	{
		fprintf( stderr, "blang: array remove index out of bounds: index %lld, length %lld\n",
			(long long)index, (long long)a->length );
		exit( 1 );
	}
	if ( out != NULL )
		memcpy( out, elem_ptr( a, index ), (size_t)a->elem_size );
	/* Shift elements left to fill gap. */
	if ( index < a->length - 1 )
	{
		memmove( elem_ptr( a, index ), elem_ptr( a, index + 1 ),
			(size_t)( a->length - index - 1 ) * (size_t)a->elem_size );
	}
	a->length--;
}

/* ========================================================================
   Concatenation
   ======================================================================== */

BlangArray *__blang_array_concat( BlangArray *a, BlangArray *b )
{
	if ( a == NULL && b == NULL )
		return __blang_array_create( 4, 0 );

	int32_t elem_size = ( a != NULL ) ? a->elem_size : b->elem_size;
	int64_t a_len = ( a != NULL ) ? a->length : 0;
	int64_t b_len = ( b != NULL ) ? b->length : 0;
	int64_t total = a_len + b_len;

	BlangArray *result = __blang_array_create( elem_size, total );
	result->length = total;

	if ( a != NULL && a->length > 0 )
		memcpy( result->data, a->data, (size_t)a->length * (size_t)elem_size );
	if ( b != NULL && b->length > 0 )
		memcpy( (char *)result->data + a_len * (int64_t)elem_size, b->data,
			(size_t)b->length * (size_t)elem_size );

	return result;
}

/* ========================================================================
   Utility
   ======================================================================== */

void __blang_array_set_elem_dtor( BlangArray *a, blang_array_elem_dtor_fn dtor )
{
	if ( a == NULL )
		return;
	a->elem_dtor = dtor;
}

void __blang_array_clear( BlangArray *a )
{
	if ( a == NULL )
		return;
	/* Release refcounted elements before clearing */
	if ( a->elem_dtor != NULL && a->data != NULL )
	{
		for ( int64_t i = 0; i < a->length; i++ )
		{
			void *slot = (char *)a->data + i * (int64_t)a->elem_size;
			void *elem_val = *(void **)slot;
			if ( elem_val != NULL )
				a->elem_dtor( elem_val );
		}
	}
	a->length = 0;
}
