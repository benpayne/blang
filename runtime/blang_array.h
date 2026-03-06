#ifndef BLANG_ARRAY_H
#define BLANG_ARRAY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	void *data;          /* pointer to contiguous elements */
	int64_t length;      /* number of elements */
	int64_t capacity;    /* allocated capacity (in elements) */
	int32_t ref_count;   /* reference count */
	int32_t elem_size;   /* size of each element in bytes */
} BlangArray;

/* Creation */
BlangArray *__blang_array_create( int32_t elem_size, int64_t initial_capacity );
BlangArray *__blang_array_create_from_data( int32_t elem_size, const void *data, int64_t count );

/* Reference counting */
void __blang_array_retain( BlangArray *a );
void __blang_array_release( BlangArray *a );

/* Properties */
int64_t __blang_array_length( BlangArray *a );
int64_t __blang_array_capacity( BlangArray *a );
bool __blang_array_is_empty( BlangArray *a );

/* Access (bounds-checked, panics on OOB) */
void __blang_array_get( BlangArray *a, int64_t index, void *out );
void __blang_array_set( BlangArray *a, int64_t index, const void *value );

/* Mutation */
void __blang_array_push( BlangArray *a, const void *value );
bool __blang_array_pop( BlangArray *a, void *out );  /* returns false if empty */
void __blang_array_insert( BlangArray *a, int64_t index, const void *value );
void __blang_array_remove( BlangArray *a, int64_t index, void *out );

/* Concatenation */
BlangArray *__blang_array_concat( BlangArray *a, BlangArray *b );

/* Utility */
void __blang_array_clear( BlangArray *a );

#ifdef __cplusplus
}
#endif

#endif /* BLANG_ARRAY_H */
