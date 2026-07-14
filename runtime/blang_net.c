#include "blang_net.h"
#include "blang_array.h"
#include "blang_runtime.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <pthread.h>

/* ========================================================================
   TCP Server
   ======================================================================== */

int __blang_tcp_listen( const char *host, int port, int backlog )
{
	int fd = socket( AF_INET, SOCK_STREAM, 0 );
	if ( fd < 0 )
	{
		perror( "blang_net: socket" );
		return -1;
	}

	int opt = 1;
	setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof( opt ) );

	struct sockaddr_in addr;
	memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	addr.sin_port = htons( (uint16_t)port );

	if ( host == NULL || strcmp( host, "0.0.0.0" ) == 0 )
		addr.sin_addr.s_addr = INADDR_ANY;
	else
		inet_pton( AF_INET, host, &addr.sin_addr );

	if ( bind( fd, (struct sockaddr *)&addr, sizeof( addr ) ) < 0 )
	{
		perror( "blang_net: bind" );
		close( fd );
		return -1;
	}

	if ( listen( fd, backlog ) < 0 )
	{
		perror( "blang_net: listen" );
		close( fd );
		return -1;
	}

	return fd;
}

int __blang_tcp_accept( int listen_fd )
{
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof( client_addr );
	int conn_fd = accept( listen_fd, (struct sockaddr *)&client_addr, &client_len );
	if ( conn_fd < 0 )
	{
		perror( "blang_net: accept" );
		return -1;
	}
	return conn_fd;
}

void __blang_tcp_close( int fd )
{
	if ( fd >= 0 )
		close( fd );
}

/* ========================================================================
   TCP Client
   ======================================================================== */

int __blang_tcp_connect( const char *host, int port )
{
	int fd = socket( AF_INET, SOCK_STREAM, 0 );
	if ( fd < 0 )
	{
		perror( "blang_net: socket" );
		return -1;
	}

	struct sockaddr_in addr;
	memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	addr.sin_port = htons( (uint16_t)port );

	if ( inet_pton( AF_INET, host, &addr.sin_addr ) <= 0 )
	{
		/* Try DNS resolution */
		struct hostent *he = gethostbyname( host );
		if ( he == NULL )
		{
			fprintf( stderr, "blang_net: cannot resolve host '%s'\n", host );
			close( fd );
			return -1;
		}
		memcpy( &addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length );
	}

	if ( connect( fd, (struct sockaddr *)&addr, sizeof( addr ) ) < 0 )
	{
		perror( "blang_net: connect" );
		close( fd );
		return -1;
	}

	return fd;
}

/* ========================================================================
   I/O
   ======================================================================== */

BlangString *__blang_tcp_read( int fd, int max_len )
{
	if ( max_len <= 0 )
		max_len = 4096;

	char *buf = (char *)malloc( (size_t)max_len );
	if ( buf == NULL )
		return __blang_string_create( "", 0 );

	ssize_t n = read( fd, buf, (size_t)max_len );
	if ( n <= 0 )
	{
		free( buf );
		return __blang_string_create( "", 0 );
	}

	BlangString *result = __blang_string_create( buf, (int64_t)n );
	free( buf );
	return result;
}

int __blang_tcp_write_string( int fd, const BlangString *data )
{
	if ( data == NULL || data->data == NULL || data->length <= 0 )
		return 0;

	ssize_t n = write( fd, data->data, (size_t)data->length );
	return ( n < 0 ) ? -1 : (int)n;
}

int __blang_tcp_write_cstring( int fd, const char *data, int len )
{
	if ( data == NULL || len <= 0 )
		return 0;

	ssize_t n = write( fd, data, (size_t)len );
	return ( n < 0 ) ? -1 : (int)n;
}

/* ========================================================================
   Buffer-based I/O
   ======================================================================== */

int64_t __blang_tcp_read_into_buffer( int fd, BlangBuffer *buf, int64_t max_len )
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

int64_t __blang_tcp_read_into_byte_array( int fd, BlangArray *arr, int64_t max_len )
{
	if ( arr == NULL || max_len <= 0 )
		return -1;

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

int64_t __blang_tcp_write_byte_array( int fd, BlangArray *arr )
{
	if ( arr == NULL || arr->data == NULL || arr->length <= 0 )
		return 0;

	ssize_t n = write( fd, arr->data, (size_t)arr->length );
	return ( n < 0 ) ? -1 : (int64_t)n;
}

int64_t __blang_tcp_write_buffer( int fd, BlangBuffer *buf )
{
	if ( buf == NULL || buf->data == NULL || buf->length <= 0 )
		return 0;

	ssize_t n = write( fd, buf->data, (size_t)buf->length );
	return ( n < 0 ) ? -1 : (int64_t)n;
}

/* Write all bytes from a string, retrying on partial writes. */
int64_t __blang_tcp_write_all( int fd, const BlangString *data )
{
	if ( data == NULL || data->data == NULL || data->length <= 0 )
		return 0;

	const char *ptr = data->data;
	int64_t remaining = data->length;
	int64_t total = 0;
	while ( remaining > 0 )
	{
		ssize_t n = write( fd, ptr, (size_t)remaining );
		if ( n < 0 )
		{
			if ( errno == EINTR )
				continue;
			return ( total > 0 ) ? total : -1;
		}
		if ( n == 0 )
			break;
		ptr += n;
		remaining -= n;
		total += n;
	}
	return total;
}

/* Stream a file to a socket in chunks. Reads from file_fd and writes to sock_fd.
   Returns total bytes transferred, -1 on error. */
int64_t __blang_sendfile( int sock_fd, int file_fd, int64_t offset, int64_t count )
{
	if ( sock_fd < 0 || file_fd < 0 || count <= 0 )
		return -1;

	/* Seek to offset */
	if ( offset > 0 )
	{
		off_t r = lseek( file_fd, (off_t)offset, SEEK_SET );
		if ( r < 0 )
			return -1;
	}

	char buf[8192];
	int64_t total = 0;
	while ( total < count )
	{
		int64_t remaining = count - total;
		size_t chunk = ( remaining > 8192 ) ? 8192 : (size_t)remaining;
		ssize_t nr = read( file_fd, buf, chunk );
		if ( nr < 0 )
		{
			if ( errno == EINTR )
				continue;
			return ( total > 0 ) ? total : -1;
		}
		if ( nr == 0 )
			break;

		/* Write all bytes read */
		const char *wptr = buf;
		ssize_t nleft = nr;
		while ( nleft > 0 )
		{
			ssize_t nw = write( sock_fd, wptr, (size_t)nleft );
			if ( nw < 0 )
			{
				if ( errno == EINTR )
					continue;
				return ( total > 0 ) ? total : -1;
			}
			wptr += nw;
			nleft -= nw;
		}
		total += nr;
	}
	return total;
}

/* ========================================================================
   Selector — poll-based event loop
   ======================================================================== */

#define SELECTOR_MAX_FDS 64
#define SELECTOR_MAX_HANDLES 16

typedef struct
{
	int fd;
	short events;            /* POLLIN */
	int is_accept;           /* 1 = listen socket (auto-accept), 0 = data */
	void (*handler)( void *ctx, int fd );
	void *ctx;
} SelectorEntry;

typedef struct
{
	struct pollfd fds[SELECTOR_MAX_FDS];
	SelectorEntry entries[SELECTOR_MAX_FDS];
	int count;
	int pipe_fd[2];          /* self-pipe for waking poll */
	int shutdown;
	int stopped;             /* 1 = event loop has exited */
	pthread_mutex_t stop_mutex;
	pthread_cond_t stop_cond;
	int active;              /* 1 = in use, 0 = free */
} BlangSelector;

static BlangSelector g_selectors[SELECTOR_MAX_HANDLES];
static int g_selectors_init = 0;

static void init_selectors( void )
{
	if ( g_selectors_init )
		return;
	memset( g_selectors, 0, sizeof( g_selectors ) );
	g_selectors_init = 1;
}

int __blang_selector_create( void )
{
	init_selectors();

	for ( int i = 0; i < SELECTOR_MAX_HANDLES; i++ )
	{
		if ( !g_selectors[i].active )
		{
			BlangSelector *sel = &g_selectors[i];
			memset( sel, 0, sizeof( BlangSelector ) );
			sel->active = 1;
			sel->stopped = 0;
			pthread_mutex_init( &sel->stop_mutex, NULL );
			pthread_cond_init( &sel->stop_cond, NULL );

			if ( pipe( sel->pipe_fd ) < 0 )
			{
				perror( "blang_net: pipe" );
				sel->active = 0;
				return -1;
			}

			/* Add self-pipe as first entry for wake-up */
			sel->fds[0].fd = sel->pipe_fd[0];
			sel->fds[0].events = POLLIN;
			sel->entries[0].fd = sel->pipe_fd[0];
			sel->entries[0].events = POLLIN;
			sel->entries[0].is_accept = 0;
			sel->entries[0].handler = NULL;
			sel->entries[0].ctx = NULL;
			sel->count = 1;

			return i;
		}
	}

	fprintf( stderr, "blang_net: too many selectors (max %d)\n", SELECTOR_MAX_HANDLES );
	return -1;
}

static BlangSelector *get_selector( int handle )
{
	if ( handle < 0 || handle >= SELECTOR_MAX_HANDLES )
		return NULL;
	if ( !g_selectors[handle].active )
		return NULL;
	return &g_selectors[handle];
}

static void selector_wake( BlangSelector *sel )
{
	char c = 'W';
	ssize_t r = write( sel->pipe_fd[1], &c, 1 );
	(void)r;
}

void __blang_selector_add_read( int sel_handle, int fd,
	void (*handler)( void *ctx, int fd ), void *ctx )
{
	BlangSelector *sel = get_selector( sel_handle );
	if ( sel == NULL || sel->count >= SELECTOR_MAX_FDS )
		return;

	int idx = sel->count;
	sel->fds[idx].fd = fd;
	sel->fds[idx].events = POLLIN;
	sel->entries[idx].fd = fd;
	sel->entries[idx].events = POLLIN;
	sel->entries[idx].is_accept = 0;
	sel->entries[idx].handler = handler;
	sel->entries[idx].ctx = ctx;
	__blang_lambda_ctx_retain( ctx );
	sel->count++;

	selector_wake( sel );
}

void __blang_selector_add_accept( int sel_handle, int listen_fd,
	void (*handler)( void *ctx, int new_fd ), void *ctx )
{
	BlangSelector *sel = get_selector( sel_handle );
	if ( sel == NULL || sel->count >= SELECTOR_MAX_FDS )
		return;

	int idx = sel->count;
	sel->fds[idx].fd = listen_fd;
	sel->fds[idx].events = POLLIN;
	sel->entries[idx].fd = listen_fd;
	sel->entries[idx].events = POLLIN;
	sel->entries[idx].is_accept = 1;
	sel->entries[idx].handler = handler;
	sel->entries[idx].ctx = ctx;
	__blang_lambda_ctx_retain( ctx );
	sel->count++;

	selector_wake( sel );
}

void __blang_selector_remove( int sel_handle, int fd )
{
	BlangSelector *sel = get_selector( sel_handle );
	if ( sel == NULL )
		return;

	for ( int i = 1; i < sel->count; i++ )  /* skip pipe at index 0 */
	{
		if ( sel->entries[i].fd == fd )
		{
			__blang_lambda_ctx_release( sel->entries[i].ctx );
			/* Shift remaining entries down */
			for ( int j = i; j < sel->count - 1; j++ )
			{
				sel->fds[j] = sel->fds[j + 1];
				sel->entries[j] = sel->entries[j + 1];
			}
			sel->count--;
			break;
		}
	}

	selector_wake( sel );
}

void __blang_selector_run( int sel_handle )
{
	BlangSelector *sel = get_selector( sel_handle );
	if ( sel == NULL )
		return;

	sel->shutdown = 0;

	while ( !sel->shutdown )
	{
		int ret = poll( sel->fds, (nfds_t)sel->count, -1 );
		if ( ret < 0 )
		{
			if ( errno == EINTR )
				continue;
			perror( "blang_net: poll" );
			break;
		}

		/* Check self-pipe (index 0) */
		if ( sel->fds[0].revents & POLLIN )
		{
			char buf[64];
			ssize_t r = read( sel->pipe_fd[0], buf, sizeof( buf ) );
			(void)r;
			/* Just a wake-up, continue to check shutdown and re-poll */
		}

		/* Process active fds (iterate backwards to allow removal) */
		for ( int i = sel->count - 1; i >= 1; i-- )
		{
			if ( sel->fds[i].revents & POLLIN )
			{
				SelectorEntry *entry = &sel->entries[i];
				if ( entry->is_accept )
				{
					/* Accept and pass new fd to handler */
					int new_fd = __blang_tcp_accept( entry->fd );
					if ( new_fd >= 0 && entry->handler != NULL )
						entry->handler( entry->ctx, new_fd );
				}
				else
				{
					/* Data ready — invoke handler with fd */
					if ( entry->handler != NULL )
						entry->handler( entry->ctx, entry->fd );
				}
			}

			if ( sel->fds[i].revents & ( POLLHUP | POLLERR | POLLNVAL ) )
			{
				/* Release lambda context before removing dead fd */
				__blang_lambda_ctx_release( sel->entries[i].ctx );
				/* Remove dead fd */
				for ( int j = i; j < sel->count - 1; j++ )
				{
					sel->fds[j] = sel->fds[j + 1];
					sel->entries[j] = sel->entries[j + 1];
				}
				sel->count--;
			}
		}
	}

	/* Signal that the event loop has exited. */
	pthread_mutex_lock( &sel->stop_mutex );
	sel->stopped = 1;
	pthread_cond_broadcast( &sel->stop_cond );
	pthread_mutex_unlock( &sel->stop_mutex );
}

void __blang_selector_wait( int sel_handle )
{
	BlangSelector *sel = get_selector( sel_handle );
	if ( sel == NULL )
		return;

	pthread_mutex_lock( &sel->stop_mutex );
	while ( !sel->stopped )
		pthread_cond_wait( &sel->stop_cond, &sel->stop_mutex );
	pthread_mutex_unlock( &sel->stop_mutex );

	/* Auto-destroy after event loop has stopped */
	__blang_selector_destroy( sel_handle );
}

void __blang_selector_shutdown( int sel_handle )
{
	BlangSelector *sel = get_selector( sel_handle );
	if ( sel == NULL )
		return;

	sel->shutdown = 1;
	selector_wake( sel );
}

void __blang_selector_destroy( int sel_handle )
{
	BlangSelector *sel = get_selector( sel_handle );
	if ( sel == NULL )
		return;

	/* Release lambda contexts for all remaining entries (skip pipe at index 0) */
	for ( int i = 1; i < sel->count; i++ )
		__blang_lambda_ctx_release( sel->entries[i].ctx );

	close( sel->pipe_fd[0] );
	close( sel->pipe_fd[1] );
	pthread_mutex_destroy( &sel->stop_mutex );
	pthread_cond_destroy( &sel->stop_cond );
	sel->active = 0;
}
