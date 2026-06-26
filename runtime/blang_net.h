#ifndef BLANG_NET_H
#define BLANG_NET_H

#include "blang_string.h"
#include "blang_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
   TCP Server
   ======================================================================== */

/* Create a listening TCP socket. Returns fd on success, -1 on error. */
int __blang_tcp_listen( const char *host, int port, int backlog );

/* Accept a connection on a listening socket. Blocking. Returns conn fd. */
int __blang_tcp_accept( int listen_fd );

/* Close a TCP socket. */
void __blang_tcp_close( int fd );

/* ========================================================================
   TCP Client
   ======================================================================== */

/* Connect to a remote host. Blocking. Returns fd on success, -1 on error. */
int __blang_tcp_connect( const char *host, int port );

/* ========================================================================
   I/O
   ======================================================================== */

/* Read up to max_len bytes. Returns a BlangString (empty on EOF/error). */
BlangString *__blang_tcp_read( int fd, int max_len );

/* Write a BlangString. Returns bytes written, -1 on error. */
int __blang_tcp_write_string( int fd, const BlangString *data );

/* Write raw bytes. Returns bytes written, -1 on error. */
int __blang_tcp_write_cstring( int fd, const char *data, int len );

/* Read from socket into a buffer. Returns bytes read (0 = EOF, -1 = error). */
int64_t __blang_tcp_read_into_buffer( int fd, BlangBuffer *buf, int64_t max_len );

/* Write buffer contents to socket. Returns bytes written (-1 = error). */
int64_t __blang_tcp_write_buffer( int fd, BlangBuffer *buf );

/* ========================================================================
   Selector — poll-based event loop
   ======================================================================== */

/* Create a new selector. Returns integer handle (>= 0), -1 on error. */
int __blang_selector_create( void );

/* Register a read handler on a connection fd. */
void __blang_selector_add_read( int sel_handle, int fd,
	void (*handler)( void *ctx, int fd ), void *ctx );

/* Register an accept handler on a listen fd. */
void __blang_selector_add_accept( int sel_handle, int listen_fd,
	void (*handler)( void *ctx, int new_fd ), void *ctx );

/* Remove a fd from the selector. */
void __blang_selector_remove( int sel_handle, int fd );

/* Run the event loop (blocking). Returns when shutdown is called.
   Typically called from a spawned thread — not directly by user code. */
void __blang_selector_run( int sel_handle );

/* Block the caller until the selector's event loop has stopped.
   Does NOT run the loop itself — just waits for shutdown to complete. */
void __blang_selector_wait( int sel_handle );

/* Signal the selector to stop its event loop. */
void __blang_selector_shutdown( int sel_handle );

/* Destroy a selector and free its resources. */
void __blang_selector_destroy( int sel_handle );

/* ---- Global event loop + timers (backs `on EXPR { ... }`) ---- */

/* Handle of the global default event loop (created on first use). */
int __blang_event_loop( void );

/* Create a repeating timer (fires every interval_ms) or a one-shot timer
   (fires once after delay_ms). Returns a timerfd to register with on/__blang_event_on. */
int __blang_timer_every( int interval_ms );
int __blang_timer_after( int delay_ms );

/* Register a handler to run when `fd` (a timerfd or socket fd) is readable,
   on the global event loop. */
void __blang_event_on( int fd, void (*handler)( void *ctx, int fd ), void *ctx );

/* Cancel an individual event source (timer/socket fd): remove it from the
   loop (and close it if it is a timer). */
void __blang_event_cancel( int fd );

/* Run / stop the global event loop. run() blocks until stopped or no event
   sources remain. run_auto() is injected at the end of main() and runs the
   loop only if it was not already driven explicitly. */
void __blang_event_run( void );
void __blang_event_run_auto( void );
void __blang_event_stop( void );

#ifdef __cplusplus
}
#endif

#endif /* BLANG_NET_H */
