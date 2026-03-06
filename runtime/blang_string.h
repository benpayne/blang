#ifndef BLANG_STRING_H
#define BLANG_STRING_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	const char *data;    /* pointer to UTF-8 bytes */
	int64_t length;      /* length in bytes */
	int64_t capacity;    /* allocated capacity (0 for static/literal strings) */
	int32_t ref_count;   /* reference count */
} BlangString;

/* Creation */
BlangString *__blang_string_create( const char *data, int64_t length );
BlangString *__blang_string_create_static( const char *static_data, int64_t length );

/* Reference counting */
void __blang_string_retain( BlangString *s );
void __blang_string_release( BlangString *s );

/* Properties */
int64_t __blang_string_length( BlangString *s );
bool __blang_string_is_empty( BlangString *s );

/* Access */
char __blang_string_char_at( BlangString *s, int64_t index );
int32_t __blang_string_byte_at( BlangString *s, int64_t index );
BlangString *__blang_string_substring( BlangString *s, int64_t start, int64_t end );

/* Search */
bool __blang_string_contains( BlangString *s, BlangString *needle );
bool __blang_string_starts_with( BlangString *s, BlangString *prefix );
bool __blang_string_ends_with( BlangString *s, BlangString *suffix );
int64_t __blang_string_index_of( BlangString *s, BlangString *needle );

/* Transform (all return new strings) */
BlangString *__blang_string_to_upper( BlangString *s );
BlangString *__blang_string_to_lower( BlangString *s );
BlangString *__blang_string_trim( BlangString *s );
BlangString *__blang_string_replace( BlangString *s, BlangString *old_str, BlangString *new_str );

/* Concatenation */
BlangString *__blang_string_concat( BlangString *a, BlangString *b );
BlangString *__blang_string_concat_many( BlangString **strings, int64_t count );

/* Comparison */
bool __blang_string_equals( BlangString *a, BlangString *b );
int32_t __blang_string_compare( BlangString *a, BlangString *b );

/* Conversion */
const char *__blang_string_to_cstring( BlangString *s );  /* returns null-terminated copy (caller frees) */
int64_t __blang_string_to_int( BlangString *s, bool *ok );
double __blang_string_to_float( BlangString *s, bool *ok );

/* For string interpolation: convert int/float/bool to string */
BlangString *__blang_int_to_string( int64_t value );
BlangString *__blang_float_to_string( double value );
BlangString *__blang_bool_to_string( bool value );

#ifdef __cplusplus
}
#endif

#endif /* BLANG_STRING_H */
