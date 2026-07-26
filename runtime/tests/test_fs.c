/* Unit tests for runtime/blang_fs.c (file I/O + directory ops), using /tmp. */
#include "test_util.h"
#include "../blang_fs.h"
#include "../blang_buffer.h"
#include "../blang_string.h"
#include "../blang_array.h"

static BlangString *S( const char *c )
{
	return __blang_string_create( c, (int64_t)strlen( c ) );
}
static void tmp_path( char *out, size_t n, const char *tag )
{
	snprintf( out, n, "/tmp/blang_fs_%s_%d", tag, (int)getpid() );
}
static void buf_cstr_eq( BlangBuffer *b, const char *exp )
{
	BlangString *s = __blang_buffer_to_string( b );
	const char *c = __blang_string_to_cstring( s );
	CHECK_STR_EQ( c, exp );
	free( (void *)c );
	__blang_string_release( s );
}

static void t_write_read( void )
{
	char path[256]; tmp_path( path, sizeof( path ), "wr" );
	int fd = __blang_file_open( path, "w" );
	CHECK( fd >= 0 );
	BlangString *s = S( "hello" );
	__blang_file_write_string( fd, s );
	__blang_file_close( fd );
	__blang_string_release( s );

	CHECK_EQ_I( __blang_fs_file_size( path ), 5 );

	fd = __blang_file_open( path, "r" );
	CHECK( fd >= 0 );
	BlangBuffer *b = __blang_buffer_create( 16 );
	int64_t n = __blang_file_read_into_buffer( fd, b, 1024 );
	CHECK_EQ_I( n, 5 );
	buf_cstr_eq( b, "hello" );
	__blang_file_close( fd );
	__blang_buffer_release( b );

	CHECK_EQ_I( __blang_fs_remove( path ), 0 );
}

static void t_seek( void )
{
	char path[256]; tmp_path( path, sizeof( path ), "seek" );
	int fd = __blang_file_open( path, "w" );
	BlangString *s = S( "0123456789" );
	__blang_file_write_string( fd, s );
	__blang_file_close( fd );
	__blang_string_release( s );

	fd = __blang_file_open( path, "r" );
	CHECK_EQ_I( __blang_file_seek( fd, 5, 0 ), 5 ); /* SEEK_SET */
	BlangBuffer *b = __blang_buffer_create( 16 );
	__blang_file_read_into_buffer( fd, b, 1024 );
	buf_cstr_eq( b, "56789" );
	__blang_file_close( fd );
	__blang_buffer_release( b );
	__blang_fs_remove( path );
}

static void t_size( void )
{
	char path[256]; tmp_path( path, sizeof( path ), "size" );
	int fd = __blang_file_open( path, "w" );
	BlangString *s = S( "abcd" );
	__blang_file_write_string( fd, s );
	__blang_file_close( fd );
	__blang_string_release( s );
	CHECK_EQ_I( __blang_fs_file_size( path ), 4 );
	__blang_fs_remove( path );
}

static void t_remove( void )
{
	char path[256]; tmp_path( path, sizeof( path ), "rm" );
	int fd = __blang_file_open( path, "w" );
	__blang_file_close( fd );
	CHECK_EQ_I( __blang_fs_remove( path ), 0 );      /* removes existing */
	CHECK_TRUE( __blang_fs_file_size( path ) < 0 );  /* now gone */
}

static void t_mkdir_list( void )
{
	char dir[256]; tmp_path( dir, sizeof( dir ), "dir" );
	CHECK_EQ_I( __blang_fs_mkdir( dir ), 0 );
	char file[400]; snprintf( file, sizeof( file ), "%s/f.txt", dir );
	int fd = __blang_file_open( file, "w" );
	CHECK( fd >= 0 );
	__blang_file_close( fd );

	BlangArray *entries = __blang_fs_list_dir( dir );
	CHECK( entries != NULL );
	CHECK_TRUE( __blang_array_length( entries ) >= 1 );
	__blang_array_release( entries );

	__blang_fs_remove( file );
	__blang_fs_remove( dir );
}

static const blang_test_case cases[] = {
	{ "write_read", t_write_read },
	{ "seek",       t_seek },
	{ "size",       t_size },
	{ "remove",     t_remove },
	{ "mkdir_list", t_mkdir_list },
};
TEST_MAIN( cases )
