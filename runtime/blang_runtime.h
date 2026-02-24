#ifndef BLANG_RUNTIME_H_
#define BLANG_RUNTIME_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- ARC (Automatic Reference Counting) ---- */

/* Header prepended to every heap-allocated shared/sync object.
   Layout: [BlangRefHeader] [user data ...] */
typedef struct BlangRefHeader
{
	int32_t ref_count;  /* atomic reference count */
	int32_t is_sync;    /* 1 if this object has a mutex */
	void *mutex;        /* pointer to pthread_mutex_t (NULL for shared-only) */
} BlangRefHeader;

/* Allocate a new ref-counted object with given data size.
   Returned pointer points PAST the header (to user data).
   Initial ref_count = 1. */
void *__blang_rc_alloc( size_t data_size );

/* Allocate a sync (mutex-protected) ref-counted object.
   Same as __blang_rc_alloc but also creates a mutex. */
void *__blang_rc_alloc_sync( size_t data_size );

/* Increment reference count. */
void __blang_rc_retain( void *ptr );

/* Decrement reference count.  Frees memory (and mutex) when count reaches 0. */
void __blang_rc_release( void *ptr );

/* Lock the mutex on a sync object.  No-op if ptr is NULL or not sync. */
void __blang_sync_lock( void *ptr );

/* Unlock the mutex on a sync object. */
void __blang_sync_unlock( void *ptr );

/* ---- Green Thread Pool (spawn) ---- */

/* Signature for a spawn body: void(*)(void). */
typedef void (*blang_spawn_fn)( void );

/* Initialize the global thread pool with `num_threads` worker threads.
   If num_threads == 0, uses the number of CPU cores. */
void __blang_runtime_init( int num_threads );

/* Submit a task to the thread pool. The function `fn` will be executed
   asynchronously by one of the worker threads. */
void __blang_spawn( blang_spawn_fn fn );

/* Block until all submitted tasks have completed, then destroy the pool. */
void __blang_runtime_shutdown( void );

/* ---- Channels ---- */

/* Opaque channel handle. */
typedef struct BlangChan BlangChan;

/* Create a buffered channel that holds elements of `elem_size` bytes.
   `capacity` is the buffer size (0 = unbuffered / rendezvous). */
BlangChan *__blang_chan_create( size_t elem_size, size_t capacity );

/* Send `data` (elem_size bytes) into the channel.
   Blocks if the channel buffer is full. */
void __blang_chan_send( BlangChan *ch, const void *data );

/* Receive `elem_size` bytes from the channel into `data_out`.
   Blocks if the channel buffer is empty.
   Returns 1 on success, 0 if channel is closed and empty. */
int __blang_chan_recv( BlangChan *ch, void *data_out );

/* Close the channel (no more sends allowed). */
void __blang_chan_close( BlangChan *ch );

/* Destroy the channel and free all resources. */
void __blang_chan_destroy( BlangChan *ch );

/* ---- Async / Event Loop ---- */

/* Signature for an async task: void*(*)(void*). */
typedef void *(*blang_async_fn)( void *arg );

/* Opaque future/task handle. */
typedef struct BlangTask BlangTask;

/* Schedule an async function to run.
   Returns a task handle that can be awaited. */
BlangTask *__blang_async_call( blang_async_fn fn, void *arg );

/* Block until the task completes and return its result value. */
void *__blang_await( BlangTask *task );

/* Destroy a completed task and free its resources. */
void __blang_task_destroy( BlangTask *task );

#ifdef __cplusplus
}
#endif

#endif /* BLANG_RUNTIME_H_ */
