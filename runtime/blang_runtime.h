#ifndef BLANG_RUNTIME_H_
#define BLANG_RUNTIME_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Allocation helpers ---- */

/* Report a fatal out-of-memory condition and abort.  `what` is an optional
   short label describing the failed allocation (may be NULL). */
void __blang_oom( const char *what );

/* Checked malloc/calloc: never return NULL.  On allocation failure they call
   __blang_oom() and abort.  A zero size/count is rounded up to 1 so the
   returned pointer is always usable.  Generated code that allocates raw
   context structs (lambdas, spawn/async closures) routes through these. */
void *__blang_alloc( size_t size );
void *__blang_calloc( size_t count, size_t size );

/* ---- ARC (Automatic Reference Counting) ---- */

/* Destructor callback type: called with user-data pointer when refcount hits 0. */
typedef void (*blang_dtor_fn)( void *ptr );

/* Header prepended to every heap-allocated shared/sync object.
   Layout: [BlangRefHeader] [user data ...] */
typedef struct BlangRefHeader
{
	int32_t ref_count;  /* atomic reference count */
	int32_t is_sync;    /* 1 if this object has a mutex */
	void *mutex;        /* pointer to pthread_mutex_t (NULL for shared-only) */
	blang_dtor_fn destructor; /* destructor for releasing owned resources (may be NULL) */
} BlangRefHeader;

/* Allocate a new ref-counted object with given data size.
   Returned pointer points PAST the header (to user data).
   Initial ref_count = 1.  destructor = NULL. */
void *__blang_rc_alloc( size_t data_size );

/* Allocate a ref-counted object with a destructor callback.
   When refcount reaches 0, the destructor is called before freeing. */
void *__blang_rc_alloc_dtor( size_t data_size, blang_dtor_fn dtor );

/* Allocate a sync (mutex-protected) ref-counted object.
   Same as __blang_rc_alloc but also creates a mutex. */
void *__blang_rc_alloc_sync( size_t data_size );

/* Increment reference count. */
void __blang_rc_retain( void *ptr );

/* Decrement reference count.  Calls destructor (if set) and frees memory
   when count reaches 0. */
void __blang_rc_release( void *ptr );

/* Lock the mutex on a sync object.  No-op if ptr is NULL or not sync. */
void __blang_sync_lock( void *ptr );

/* Unlock the mutex on a sync object. */
void __blang_sync_unlock( void *ptr );

/* ---- Lambda Context Lifetime ---- */

/* Lambda context layout: { int64_t refcount, void(*destructor)(void*), ...captured_fields... }
   The refcount and destructor are stored at known offsets in the malloc'd context struct. */

/* Increment lambda context reference count. No-op if ctx is NULL. */
void __blang_lambda_ctx_retain( void *ctx );

/* Decrement lambda context reference count. When count reaches 0,
   calls the destructor (at offset 8) to release captured values, then frees ctx.
   No-op if ctx is NULL. */
void __blang_lambda_ctx_release( void *ctx );

/* ---- Green Thread Pool (spawn) ---- */

/* Signature for a spawn body: void(*)(void*) with context pointer. */
typedef void (*blang_spawn_fn)( void *ctx );

/* ---- Task Handles (spawn + wait) ---- */

/* Opaque handle returned by __blang_spawn. */
typedef struct BlangSpawnTask BlangSpawnTask;

/* Initialize the global thread pool with `num_threads` worker threads.
   If num_threads == 0, uses the number of CPU cores. */
void __blang_runtime_init( int num_threads );

/* Submit a task to the thread pool. The function `fn` will be executed
   asynchronously by one of the worker threads with the given context.
   The context will be freed after the function returns.
   Returns a task handle that can be waited on, or ignored (fire-and-forget). */
BlangSpawnTask *__blang_spawn( blang_spawn_fn fn, void *ctx );

/* Block until all submitted tasks have completed, then destroy the pool. */
void __blang_runtime_shutdown( void );

/* Wait for a single spawned task to complete. Blocks until done. */
void __blang_spawn_wait( BlangSpawnTask *task );

/* Destroy a completed task handle and free resources. */
void __blang_spawn_task_destroy( BlangSpawnTask *task );

/* Wait for all currently in-flight spawned tasks to complete.
   Does NOT shut down the thread pool (unlike __blang_runtime_shutdown). */
void __blang_wait_all( void );

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
   Returns a task handle that can be awaited.
   When built with libuv (BLANG_HAS_LIBUV), tasks are dispatched to the libuv
   thread pool via uv_queue_work.  Otherwise, falls back to one pthread per call. */
BlangTask *__blang_async_call( blang_async_fn fn, void *arg );

/* Block until the task completes and return its result value. */
void *__blang_await( BlangTask *task );

/* Destroy a completed task and free its resources. */
void __blang_task_destroy( BlangTask *task );

#ifdef __cplusplus
}
#endif

#endif /* BLANG_RUNTIME_H_ */
