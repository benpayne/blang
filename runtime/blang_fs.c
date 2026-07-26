#include "blang_fs.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

/* ========================================================================
   File handle operations
   ======================================================================== */

int __blang_file_open( const char *path, const char *mode )
{
	if ( path == NULL || mode == NULL )
		return -1;

	int flags = 0;
	mode_t create_mode = 0644;

	if ( strcmp( mode, "r" ) == 0 )
	{
		flags = O_RDONLY;
	}
	else if ( strcmp( mode, "w" ) == 0 )
	{
		flags = O_WRONLY | O_CREAT | O_TRUNC;
	}
	else if ( strcmp( mode, "a" ) == 0 )
	{
		flags = O_WRONLY | O_CREAT | O_APPEND;
	}
	else if ( strcmp( mode, "rw" ) == 0 )
	{
		flags = O_RDWR | O_CREAT;
	}
	else
	{
		/* Default to read-only for unknown modes */
		flags = O_RDONLY;
	}

	int fd = open( path, flags, create_mode );
	if ( fd < 0 )
	{
		perror( "blang_fs: open" );
		return -1;
	}
	return fd;
}

void __blang_file_close( int fd )
{
	if ( fd >= 0 )
		close( fd );
}

int __blang_file_write_string( int fd, const BlangString *data )
{
	if ( data == NULL || data->data == NULL || data->length <= 0 )
		return 0;

	ssize_t n = write( fd, data->data, (size_t)data->length );
	return ( n < 0 ) ? -1 : (int)n;
}

int64_t __blang_file_read_into_buffer( int fd, BlangBuffer *buf, int64_t max_len )
{
	if ( buf == NULL || max_len <= 0 )
		return -1;

	/* Ensure buffer has capacity for max_len more bytes */
	int64_t needed = buf->length + max_len;
	if ( needed > buf->capacity )
	{
		int64_t new_cap = buf->capacity;
		if ( new_cap < 64 )
			new_cap = 64;
		while ( new_cap < needed )
			new_cap *= 2;
		uint8_t *new_data = (uint8_t *)realloc( buf->data, (size_t)new_cap );
		if ( new_data == NULL )
			return -1;
		buf->data = new_data;
		buf->capacity = new_cap;
	}

	ssize_t n = read( fd, buf->data + buf->length, (size_t)max_len );
	if ( n < 0 )
		return -1;
	if ( n == 0 )
		return 0;

	buf->length += n;
	return (int64_t)n;
}

int64_t __blang_file_read_into_byte_array( int fd, BlangArray *arr, int64_t max_len )
{
	if ( arr == NULL || max_len <= 0 )
		return -1;

	/* Read into a temp stack buffer, then push bytes into array */
	uint8_t tmp[4096];
	int64_t total = 0;
	while ( total < max_len )
	{
		int64_t chunk = max_len - total;
		if ( chunk > 4096 )
			chunk = 4096;
		ssize_t n = read( fd, tmp, (size_t)chunk );
		if ( n < 0 )
			return ( total > 0 ) ? total : -1;
		if ( n == 0 )
			break;
		for ( ssize_t i = 0; i < n; i++ )
			__blang_array_push( arr, &tmp[i] );
		total += n;
	}
	return total;
}

int64_t __blang_file_seek( int fd, int64_t offset, int whence )
{
	int w;
	switch ( whence )
	{
		case 0: w = SEEK_SET; break;
		case 1: w = SEEK_CUR; break;
		case 2: w = SEEK_END; break;
		default: w = SEEK_SET; break;
	}
	off_t result = lseek( fd, (off_t)offset, w );
	return ( result < 0 ) ? -1 : (int64_t)result;
}

int __blang_file_flush( int fd )
{
	if ( fd < 0 )
		return -1;
	return fsync( fd );
}

/* ========================================================================
   Filesystem operations
   ======================================================================== */

int __blang_fs_file_type( const char *path )
{
	struct stat st;
	if ( path == NULL || stat( path, &st ) != 0 )
		return -1;
	if ( S_ISDIR( st.st_mode ) )
		return 1;
	return 0;
}

int64_t __blang_fs_file_size( const char *path )
{
	struct stat st;
	if ( path == NULL || stat( path, &st ) != 0 )
		return -1;
	return (int64_t)st.st_size;
}

int __blang_fs_remove( const char *path )
{
	if ( path == NULL )
		return -1;

	/* Try unlink first (file), then rmdir (directory) */
	if ( unlink( path ) == 0 )
		return 0;
	if ( rmdir( path ) == 0 )
		return 0;

	return -1;
}

int __blang_fs_mkdir( const char *path )
{
	if ( path == NULL )
		return -1;
	return mkdir( path, 0755 );
}

/* Element destructor for BlangArray of BlangString* */
static void string_elem_dtor( void *elem )
{
	BlangString *s = (BlangString *)elem;
	if ( s != NULL )
		__blang_string_release( s );
}

BlangArray *__blang_fs_list_dir( const char *path )
{
	BlangArray *arr = __blang_array_create( sizeof( void * ), 16 );
	__blang_array_set_elem_dtor( arr, string_elem_dtor );

	if ( path == NULL )
		return arr;

	DIR *d = opendir( path );
	if ( d == NULL )
		return arr;

	struct dirent *entry;
	while ( ( entry = readdir( d ) ) != NULL )
	{
		/* Skip . and .. */
		if ( strcmp( entry->d_name, "." ) == 0 || strcmp( entry->d_name, ".." ) == 0 )
			continue;

		BlangString *s = __blang_string_create( entry->d_name, (int64_t)strlen( entry->d_name ) );
		__blang_array_push( arr, &s );
	}

	closedir( d );
	return arr;
}
