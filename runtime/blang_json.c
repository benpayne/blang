#include "blang_json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ---- Constructors ---- */

static BlangJsonValue *alloc_value( BlangJsonType type )
{
	BlangJsonValue *v = (BlangJsonValue *)calloc( 1, sizeof( BlangJsonValue ) );
	v->type = type;
	return v;
}

BlangJsonValue *__blang_json_null( void )
{
	return alloc_value( BLANG_JSON_NULL );
}

BlangJsonValue *__blang_json_bool( int val )
{
	BlangJsonValue *v = alloc_value( BLANG_JSON_BOOL );
	v->data.bool_val = val ? 1 : 0;
	return v;
}

BlangJsonValue *__blang_json_int( int64_t val )
{
	BlangJsonValue *v = alloc_value( BLANG_JSON_INT );
	v->data.int_val = val;
	return v;
}

BlangJsonValue *__blang_json_float( double val )
{
	BlangJsonValue *v = alloc_value( BLANG_JSON_FLOAT );
	v->data.float_val = val;
	return v;
}

BlangJsonValue *__blang_json_string( const char *val )
{
	BlangJsonValue *v = alloc_value( BLANG_JSON_STRING );
	v->data.string_val = strdup( val );
	return v;
}

BlangJsonValue *__blang_json_array( void )
{
	BlangJsonValue *v = alloc_value( BLANG_JSON_ARRAY );
	v->data.array.capacity = 8;
	v->data.array.items = (BlangJsonValue **)calloc( 8, sizeof( BlangJsonValue * ) );
	v->data.array.count = 0;
	return v;
}

BlangJsonValue *__blang_json_object( void )
{
	BlangJsonValue *v = alloc_value( BLANG_JSON_OBJECT );
	v->data.object.capacity = 8;
	v->data.object.pairs = (BlangJsonPair *)calloc( 8, sizeof( BlangJsonPair ) );
	v->data.object.count = 0;
	return v;
}

/* ---- Mutators ---- */

void __blang_json_array_push( BlangJsonValue *arr, BlangJsonValue *val )
{
	if ( arr->type != BLANG_JSON_ARRAY ) return;

	if ( arr->data.array.count >= arr->data.array.capacity )
	{
		arr->data.array.capacity *= 2;
		arr->data.array.items = (BlangJsonValue **)realloc(
			arr->data.array.items,
			arr->data.array.capacity * sizeof( BlangJsonValue * ) );
	}
	arr->data.array.items[arr->data.array.count++] = val;
}

void __blang_json_object_set( BlangJsonValue *obj, const char *key, BlangJsonValue *val )
{
	if ( obj->type != BLANG_JSON_OBJECT ) return;

	/* Check for existing key */
	for ( int i = 0; i < obj->data.object.count; i++ )
	{
		if ( strcmp( obj->data.object.pairs[i].key, key ) == 0 )
		{
			__blang_json_free( obj->data.object.pairs[i].value );
			obj->data.object.pairs[i].value = val;
			return;
		}
	}

	if ( obj->data.object.count >= obj->data.object.capacity )
	{
		obj->data.object.capacity *= 2;
		obj->data.object.pairs = (BlangJsonPair *)realloc(
			obj->data.object.pairs,
			obj->data.object.capacity * sizeof( BlangJsonPair ) );
	}

	BlangJsonPair *p = &obj->data.object.pairs[obj->data.object.count++];
	p->key = strdup( key );
	p->value = val;
}

/* ---- Serialization (encode) ---- */

/* Dynamic string buffer for building JSON output */
typedef struct {
	char *buf;
	int len;
	int cap;
} StrBuf;

static void sb_init( StrBuf *sb )
{
	sb->cap = 256;
	sb->buf = (char *)malloc( sb->cap );
	sb->len = 0;
	sb->buf[0] = '\0';
}

static void sb_append( StrBuf *sb, const char *s )
{
	int slen = strlen( s );
	while ( sb->len + slen + 1 > sb->cap )
	{
		sb->cap *= 2;
		sb->buf = (char *)realloc( sb->buf, sb->cap );
	}
	memcpy( sb->buf + sb->len, s, slen + 1 );
	sb->len += slen;
}

static void sb_append_char( StrBuf *sb, char c )
{
	if ( sb->len + 2 > sb->cap )
	{
		sb->cap *= 2;
		sb->buf = (char *)realloc( sb->buf, sb->cap );
	}
	sb->buf[sb->len++] = c;
	sb->buf[sb->len] = '\0';
}

static void encode_string( StrBuf *sb, const char *s )
{
	sb_append_char( sb, '"' );
	for ( const char *p = s; *p; p++ )
	{
		switch ( *p )
		{
		case '"':  sb_append( sb, "\\\"" ); break;
		case '\\': sb_append( sb, "\\\\" ); break;
		case '\b': sb_append( sb, "\\b" ); break;
		case '\f': sb_append( sb, "\\f" ); break;
		case '\n': sb_append( sb, "\\n" ); break;
		case '\r': sb_append( sb, "\\r" ); break;
		case '\t': sb_append( sb, "\\t" ); break;
		default:
			if ( (unsigned char)*p < 0x20 )
			{
				char esc[8];
				snprintf( esc, sizeof( esc ), "\\u%04x", (unsigned char)*p );
				sb_append( sb, esc );
			}
			else
			{
				sb_append_char( sb, *p );
			}
			break;
		}
	}
	sb_append_char( sb, '"' );
}

static void encode_value( StrBuf *sb, BlangJsonValue *val )
{
	char tmp[64];

	switch ( val->type )
	{
	case BLANG_JSON_NULL:
		sb_append( sb, "null" );
		break;
	case BLANG_JSON_BOOL:
		sb_append( sb, val->data.bool_val ? "true" : "false" );
		break;
	case BLANG_JSON_INT:
		snprintf( tmp, sizeof( tmp ), "%lld", (long long)val->data.int_val );
		sb_append( sb, tmp );
		break;
	case BLANG_JSON_FLOAT:
		snprintf( tmp, sizeof( tmp ), "%g", val->data.float_val );
		sb_append( sb, tmp );
		break;
	case BLANG_JSON_STRING:
		encode_string( sb, val->data.string_val );
		break;
	case BLANG_JSON_ARRAY:
		sb_append_char( sb, '[' );
		for ( int i = 0; i < val->data.array.count; i++ )
		{
			if ( i > 0 ) sb_append_char( sb, ',' );
			encode_value( sb, val->data.array.items[i] );
		}
		sb_append_char( sb, ']' );
		break;
	case BLANG_JSON_OBJECT:
		sb_append_char( sb, '{' );
		for ( int i = 0; i < val->data.object.count; i++ )
		{
			if ( i > 0 ) sb_append_char( sb, ',' );
			encode_string( sb, val->data.object.pairs[i].key );
			sb_append_char( sb, ':' );
			encode_value( sb, val->data.object.pairs[i].value );
		}
		sb_append_char( sb, '}' );
		break;
	}
}

char *__blang_json_encode( BlangJsonValue *val )
{
	StrBuf sb;
	sb_init( &sb );
	encode_value( &sb, val );
	return sb.buf;
}

/* ---- Deserialization (decode) ---- */

typedef struct {
	const char *input;
	int pos;
	const char *error;
} JsonParser;

static void skip_ws( JsonParser *p )
{
	while ( p->input[p->pos] && isspace( (unsigned char)p->input[p->pos] ) )
		p->pos++;
}

static BlangJsonValue *parse_value( JsonParser *p );

static char *parse_string_raw( JsonParser *p )
{
	if ( p->input[p->pos] != '"' )
	{
		p->error = "Expected '\"'";
		return NULL;
	}
	p->pos++; /* skip opening quote */

	StrBuf sb;
	sb_init( &sb );

	while ( p->input[p->pos] && p->input[p->pos] != '"' )
	{
		if ( p->input[p->pos] == '\\' )
		{
			p->pos++;
			switch ( p->input[p->pos] )
			{
			case '"':  sb_append_char( &sb, '"' ); break;
			case '\\': sb_append_char( &sb, '\\' ); break;
			case '/':  sb_append_char( &sb, '/' ); break;
			case 'b':  sb_append_char( &sb, '\b' ); break;
			case 'f':  sb_append_char( &sb, '\f' ); break;
			case 'n':  sb_append_char( &sb, '\n' ); break;
			case 'r':  sb_append_char( &sb, '\r' ); break;
			case 't':  sb_append_char( &sb, '\t' ); break;
			default:
				sb_append_char( &sb, p->input[p->pos] );
				break;
			}
			p->pos++;
		}
		else
		{
			sb_append_char( &sb, p->input[p->pos++] );
		}
	}

	if ( p->input[p->pos] != '"' )
	{
		p->error = "Unterminated string";
		free( sb.buf );
		return NULL;
	}
	p->pos++; /* skip closing quote */
	return sb.buf;
}

static BlangJsonValue *parse_string( JsonParser *p )
{
	char *s = parse_string_raw( p );
	if ( !s ) return NULL;
	BlangJsonValue *v = alloc_value( BLANG_JSON_STRING );
	v->data.string_val = s;
	return v;
}

static BlangJsonValue *parse_number( JsonParser *p )
{
	const char *start = p->input + p->pos;
	int is_float = 0;

	if ( p->input[p->pos] == '-' ) p->pos++;
	while ( isdigit( (unsigned char)p->input[p->pos] ) ) p->pos++;

	if ( p->input[p->pos] == '.' )
	{
		is_float = 1;
		p->pos++;
		while ( isdigit( (unsigned char)p->input[p->pos] ) ) p->pos++;
	}

	if ( p->input[p->pos] == 'e' || p->input[p->pos] == 'E' )
	{
		is_float = 1;
		p->pos++;
		if ( p->input[p->pos] == '+' || p->input[p->pos] == '-' ) p->pos++;
		while ( isdigit( (unsigned char)p->input[p->pos] ) ) p->pos++;
	}

	if ( is_float )
	{
		BlangJsonValue *v = alloc_value( BLANG_JSON_FLOAT );
		v->data.float_val = strtod( start, NULL );
		return v;
	}
	else
	{
		BlangJsonValue *v = alloc_value( BLANG_JSON_INT );
		v->data.int_val = strtoll( start, NULL, 10 );
		return v;
	}
}

static BlangJsonValue *parse_array( JsonParser *p )
{
	p->pos++; /* skip '[' */
	BlangJsonValue *arr = __blang_json_array();

	skip_ws( p );
	if ( p->input[p->pos] == ']' )
	{
		p->pos++;
		return arr;
	}

	while ( 1 )
	{
		skip_ws( p );
		BlangJsonValue *elem = parse_value( p );
		if ( !elem )
		{
			__blang_json_free( arr );
			return NULL;
		}
		__blang_json_array_push( arr, elem );

		skip_ws( p );
		if ( p->input[p->pos] == ',' )
		{
			p->pos++;
			continue;
		}
		else if ( p->input[p->pos] == ']' )
		{
			p->pos++;
			return arr;
		}
		else
		{
			p->error = "Expected ',' or ']' in array";
			__blang_json_free( arr );
			return NULL;
		}
	}
}

static BlangJsonValue *parse_object( JsonParser *p )
{
	p->pos++; /* skip '{' */
	BlangJsonValue *obj = __blang_json_object();

	skip_ws( p );
	if ( p->input[p->pos] == '}' )
	{
		p->pos++;
		return obj;
	}

	while ( 1 )
	{
		skip_ws( p );
		char *key = parse_string_raw( p );
		if ( !key )
		{
			__blang_json_free( obj );
			return NULL;
		}

		skip_ws( p );
		if ( p->input[p->pos] != ':' )
		{
			p->error = "Expected ':' in object";
			free( key );
			__blang_json_free( obj );
			return NULL;
		}
		p->pos++;

		skip_ws( p );
		BlangJsonValue *val = parse_value( p );
		if ( !val )
		{
			free( key );
			__blang_json_free( obj );
			return NULL;
		}

		__blang_json_object_set( obj, key, val );
		free( key );

		skip_ws( p );
		if ( p->input[p->pos] == ',' )
		{
			p->pos++;
			continue;
		}
		else if ( p->input[p->pos] == '}' )
		{
			p->pos++;
			return obj;
		}
		else
		{
			p->error = "Expected ',' or '}' in object";
			__blang_json_free( obj );
			return NULL;
		}
	}
}

static BlangJsonValue *parse_value( JsonParser *p )
{
	skip_ws( p );
	char c = p->input[p->pos];

	if ( c == '"' ) return parse_string( p );
	if ( c == '[' ) return parse_array( p );
	if ( c == '{' ) return parse_object( p );
	if ( c == '-' || isdigit( (unsigned char)c ) ) return parse_number( p );

	if ( strncmp( p->input + p->pos, "true", 4 ) == 0 &&
		!isalnum( (unsigned char)p->input[p->pos + 4] ) )
	{
		p->pos += 4;
		return __blang_json_bool( 1 );
	}
	if ( strncmp( p->input + p->pos, "false", 5 ) == 0 &&
		!isalnum( (unsigned char)p->input[p->pos + 5] ) )
	{
		p->pos += 5;
		return __blang_json_bool( 0 );
	}
	if ( strncmp( p->input + p->pos, "null", 4 ) == 0 &&
		!isalnum( (unsigned char)p->input[p->pos + 4] ) )
	{
		p->pos += 4;
		return __blang_json_null();
	}

	p->error = "Unexpected character in JSON";
	return NULL;
}

BlangJsonValue *__blang_json_decode( const char *input, const char **error_msg )
{
	JsonParser parser;
	parser.input = input;
	parser.pos = 0;
	parser.error = NULL;

	BlangJsonValue *result = parse_value( &parser );

	if ( !result && error_msg )
		*error_msg = parser.error ? parser.error : "Unknown parse error";

	return result;
}

/* ---- Accessors ---- */

BlangJsonValue *__blang_json_object_get( BlangJsonValue *obj, const char *key )
{
	if ( !obj || obj->type != BLANG_JSON_OBJECT || !key )
		return NULL;

	for ( int i = 0; i < obj->data.object.count; i++ )
	{
		if ( strcmp( obj->data.object.pairs[i].key, key ) == 0 )
			return obj->data.object.pairs[i].value;
	}
	return NULL;
}

int64_t __blang_json_get_int( BlangJsonValue *val )
{
	if ( !val ) return 0;
	if ( val->type == BLANG_JSON_INT ) return val->data.int_val;
	if ( val->type == BLANG_JSON_FLOAT ) return (int64_t)val->data.float_val;
	return 0;
}

double __blang_json_get_float( BlangJsonValue *val )
{
	if ( !val ) return 0.0;
	if ( val->type == BLANG_JSON_FLOAT ) return val->data.float_val;
	if ( val->type == BLANG_JSON_INT ) return (double)val->data.int_val;
	return 0.0;
}

const char *__blang_json_get_string( BlangJsonValue *val )
{
	if ( !val || val->type != BLANG_JSON_STRING ) return "";
	return val->data.string_val;
}

int __blang_json_get_bool( BlangJsonValue *val )
{
	if ( !val ) return 0;
	if ( val->type == BLANG_JSON_BOOL ) return val->data.bool_val;
	if ( val->type == BLANG_JSON_INT ) return val->data.int_val != 0;
	return 0;
}

/* ---- Cleanup ---- */

void __blang_json_free( BlangJsonValue *val )
{
	if ( !val ) return;

	switch ( val->type )
	{
	case BLANG_JSON_STRING:
		free( val->data.string_val );
		break;
	case BLANG_JSON_ARRAY:
		for ( int i = 0; i < val->data.array.count; i++ )
			__blang_json_free( val->data.array.items[i] );
		free( val->data.array.items );
		break;
	case BLANG_JSON_OBJECT:
		for ( int i = 0; i < val->data.object.count; i++ )
		{
			free( val->data.object.pairs[i].key );
			__blang_json_free( val->data.object.pairs[i].value );
		}
		free( val->data.object.pairs );
		break;
	default:
		break;
	}

	free( val );
}
