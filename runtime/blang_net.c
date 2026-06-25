#include "blang_net.h"

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
#include <stdint.h>
#include <time.h>
#include <sys/timerfd.h>

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

int64_t __blang_tcp_write_buffer( int fd, BlangBuffer *buf )
{
	if ( buf == NULL || buf->data == NULL || buf->length <= 0 )
		return 0;

	ssize_t n = write( fd, buf->data, (size_t)buf->length );
	return ( n < 0 ) ? -1 : (int64_t)n;
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
	int is_timer;            /* 1 = timerfd (drain the expiration count before dispatch) */
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
	sel->entries[idx].is_timer = 0;
	sel->entries[idx].handler = handler;
	sel->entries[idx].ctx = ctx;
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
	sel->entries[idx].is_timer = 0;
	sel->entries[idx].handler = handler;
	sel->entries[idx].ctx = ctx;
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
					/* Timer fds must be drained (read the 8-byte expiration
					   count) or they stay perpetually readable. */
					if ( entry->is_timer )
					{
						uint64_t expirations = 0;
						ssize_t tr = read( entry->fd, &expirations, sizeof( expirations ) );
						(void)tr;
					}
					/* Data ready — invoke handler with fd */
					if ( entry->handler != NULL )
						entry->handler( entry->ctx, entry->fd );
				}
			}

			if ( sel->fds[i].revents & ( POLLHUP | POLLERR | POLLNVAL ) )
			{
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

	close( sel->pipe_fd[0] );
	close( sel->pipe_fd[1] );
	pthread_mutex_destroy( &sel->stop_mutex );
	pthread_cond_destroy( &sel->stop_cond );
	sel->active = 0;
}

/* ========================================================================
   Global event loop + timers

   A single default event loop (a selector) backs `on EXPR { ... }` handlers.
   The event expression yields an fd — a timerfd from timer.every()/after() or
   a socket fd — and the handler is registered to fire when that fd is readable.
   Handlers are registered (single-threaded) before __blang_event_run() blocks
   the calling thread running the poll loop. This is the cooperative,
   single-threaded event model (distinct from preemptive `spawn`).
   ======================================================================== */

static int g_event_loop_handle = -1;
static pthread_mutex_t g_event_loop_mutex = PTHREAD_MUTEX_INITIALIZER;

/* timerfds, so the loop knows to drain the expiration count on dispatch. */
#define MAX_TIMER_FDS 64
static int g_timer_fds[MAX_TIMER_FDS];
static int g_timer_fd_count = 0;

int __blang_event_loop( void )
{
	pthread_mutex_lock( &g_event_loop_mutex );
	if ( g_event_loop_handle < 0 )
		g_event_loop_handle = __blang_selector_create();
	int h = g_event_loop_handle;
	pthread_mutex_unlock( &g_event_loop_mutex );
	return h;
}

static void track_timer_fd( int fd )
{
	pthread_mutex_lock( &g_event_loop_mutex );
	if ( g_timer_fd_count < MAX_TIMER_FDS )
		g_timer_fds[g_timer_fd_count++] = fd;
	pthread_mutex_unlock( &g_event_loop_mutex );
}

static int is_timer_fd( int fd )
{
	for ( int i = 0; i < g_timer_fd_count; i++ )
		if ( g_timer_fds[i] == fd )
			return 1;
	return 0;
}

/* Repeating timer: fires every interval_ms. Returns a timerfd, or -1. */
int __blang_timer_every( int interval_ms )
{
	int tfd = timerfd_create( CLOCK_MONOTONIC, TFD_CLOEXEC );
	if ( tfd < 0 )
	{
		perror( "blang_net: timerfd_create" );
		return -1;
	}
	struct itimerspec its;
	its.it_value.tv_sec = interval_ms / 1000;
	its.it_value.tv_nsec = (long)( interval_ms % 1000 ) * 1000000L;
	if ( its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0 )
		its.it_value.tv_nsec = 1; /* a zero value would disarm the timer */
	its.it_interval = its.it_value; /* repeat at the same interval */
	if ( timerfd_settime( tfd, 0, &its, NULL ) < 0 )
	{
		perror( "blang_net: timerfd_settime" );
		close( tfd );
		return -1;
	}
	track_timer_fd( tfd );
	return tfd;
}

/* One-shot timer: fires once after delay_ms. Returns a timerfd, or -1. */
int __blang_timer_after( int delay_ms )
{
	int tfd = timerfd_create( CLOCK_MONOTONIC, TFD_CLOEXEC );
	if ( tfd < 0 )
	{
		perror( "blang_net: timerfd_create" );
		return -1;
	}
	struct itimerspec its;
	memset( &its, 0, sizeof( its ) );
	its.it_value.tv_sec = delay_ms / 1000;
	its.it_value.tv_nsec = (long)( delay_ms % 1000 ) * 1000000L;
	if ( its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0 )
		its.it_value.tv_nsec = 1;
	/* it_interval left 0 -> one-shot */
	if ( timerfd_settime( tfd, 0, &its, NULL ) < 0 )
	{
		perror( "blang_net: timerfd_settime" );
		close( tfd );
		return -1;
	}
	track_timer_fd( tfd );
	return tfd;
}

/* Register a handler to run when `fd` is readable, on the global event loop. */
void __blang_event_on( int fd, void (*handler)( void *ctx, int fd ), void *ctx )
{
	if ( fd < 0 )
		return;
	int sel_handle = __blang_event_loop();
	BlangSelector *sel = get_selector( sel_handle );
	if ( sel == NULL || sel->count >= SELECTOR_MAX_FDS )
		return;

	int idx = sel->count;
	sel->fds[idx].fd = fd;
	sel->fds[idx].events = POLLIN;
	sel->entries[idx].fd = fd;
	sel->entries[idx].events = POLLIN;
	sel->entries[idx].is_accept = 0;
	sel->entries[idx].is_timer = is_timer_fd( fd );
	sel->entries[idx].handler = handler;
	sel->entries[idx].ctx = ctx;
	sel->count++;

	selector_wake( sel );
}

/* Run the global event loop (blocks the calling thread until stopped). */
void __blang_event_run( void )
{
	__blang_selector_run( __blang_event_loop() );
}

/* Stop the global event loop. */
void __blang_event_stop( void )
{
	__blang_selector_shutdown( __blang_event_loop() );
}
