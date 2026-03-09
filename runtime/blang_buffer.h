#ifndef BLANG_BUFFER_H
#define BLANG_BUFFER_H

#include <stdint.h>
#include "blang_string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	uint8_t *data;
	int64_t length;
	int64_t capacity;
	int32_t ref_count;
} BlangBuffer;

/* Lifecycle */
BlangBuffer *__blang_buffer_create( int64_t capacity );
BlangBuffer *__blang_buffer_create_from_string( BlangString *s );
void __blang_buffer_retain( BlangBuffer *buf );
void __blang_buffer_release( BlangBuffer *buf );

/* Properties */
int64_t __blang_buffer_length( BlangBuffer *buf );
int64_t __blang_buffer_capacity( BlangBuffer *buf );
int32_t __blang_buffer_is_empty( BlangBuffer *buf );

/* Read/write */
int32_t __blang_buffer_get( BlangBuffer *buf, int64_t index );
void __blang_buffer_set( BlangBuffer *buf, int64_t index, int32_t value );

/* Append */
void __blang_buffer_append_byte( BlangBuffer *buf, int32_t byte );
void __blang_buffer_append_bytes( BlangBuffer *buf, BlangBuffer *src, int64_t len );
void __blang_buffer_append_string( BlangBuffer *buf, BlangString *s );

/* Search and slice */
int64_t __blang_buffer_index_of( BlangBuffer *buf, BlangBuffer *pattern, int64_t offset );
BlangBuffer *__blang_buffer_slice( BlangBuffer *buf, int64_t start, int64_t end );

/* Conversion */
BlangString *__blang_buffer_to_string( BlangBuffer *buf );
BlangString *__blang_buffer_to_string_range( BlangBuffer *buf, int64_t start, int64_t end );

/* Management */
void __blang_buffer_clear( BlangBuffer *buf );
void __blang_buffer_compact( BlangBuffer *buf, int64_t bytes );

#ifdef __cplusplus
}
#endif

#endif /* BLANG_BUFFER_H */
