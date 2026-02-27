#ifndef BLANG_JSON_H_
#define BLANG_JSON_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- JSON Value Types ---- */

typedef enum {
	BLANG_JSON_NULL,
	BLANG_JSON_BOOL,
	BLANG_JSON_INT,
	BLANG_JSON_FLOAT,
	BLANG_JSON_STRING,
	BLANG_JSON_ARRAY,
	BLANG_JSON_OBJECT
} BlangJsonType;

typedef struct BlangJsonValue BlangJsonValue;
typedef struct BlangJsonPair BlangJsonPair;

struct BlangJsonPair
{
	char *key;
	BlangJsonValue *value;
};

struct BlangJsonValue
{
	BlangJsonType type;
	union {
		int64_t int_val;
		double float_val;
		int bool_val;
		char *string_val;
		struct {
			BlangJsonValue **items;
			int count;
			int capacity;
		} array;
		struct {
			BlangJsonPair *pairs;
			int count;
			int capacity;
		} object;
	} data;
};

/* ---- Constructors ---- */

BlangJsonValue *__blang_json_null( void );
BlangJsonValue *__blang_json_bool( int val );
BlangJsonValue *__blang_json_int( int64_t val );
BlangJsonValue *__blang_json_float( double val );
BlangJsonValue *__blang_json_string( const char *val );
BlangJsonValue *__blang_json_array( void );
BlangJsonValue *__blang_json_object( void );

/* ---- Mutators ---- */

void __blang_json_array_push( BlangJsonValue *arr, BlangJsonValue *val );
void __blang_json_object_set( BlangJsonValue *obj, const char *key, BlangJsonValue *val );

/* ---- Serialization ---- */

/* Serialize a JSON value to a newly allocated string.
   Caller must free the returned string. */
char *__blang_json_encode( BlangJsonValue *val );

/* Parse a JSON string into a value tree.
   Returns NULL on parse error. Sets *error_msg on failure. */
BlangJsonValue *__blang_json_decode( const char *input, const char **error_msg );

/* ---- Cleanup ---- */

void __blang_json_free( BlangJsonValue *val );

#ifdef __cplusplus
}
#endif

#endif /* BLANG_JSON_H_ */
