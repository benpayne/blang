#include "blang_runtime.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#ifdef BLANG_HAS_LIBUV
#include <uv.h>

/* Global event loop state (used by async/await and shutdown). */
static uv_loop_t *g_async_loop = NULL;
static pthread_t g_loop_thread;
static int g_loop_running = 0;
#endif

/* ========================================================================
   Allocation helpers

   The BLang runtime treats heap exhaustion as a fatal, unrecoverable error:
   there is no language-level mechanism for a program to handle OOM, so the
   only safe behaviour is to abort with a diagnostic rather than return NULL
   and let generated code dereference it.  All internal allocations route
   through these helpers so the policy is enforced in exactly one place.
   ======================================================================== */

void __blang_oom( const char *what )
{
	fprintf( stderr, "blang: out of memory (%s)\n",
		what != NULL ? what : "allocation" );
	abort();
}

void *__blang_alloc( size_t size )
{
	/* Guard against zero-size allocations returning a non-usable pointer. */
	if ( size == 0 )
		size = 1;
	void *p = malloc( size );
	if ( p == NULL )
		__blang_oom( "malloc" );
	return p;
}

void *__blang_calloc( size_t count, size_t size )
{
	if ( count == 0 )
		count = 1;
	if ( size == 0 )
		size = 1;
	void *p = calloc( count, size );
	if ( p == NULL )
		__blang_oom( "calloc" );
	return p;
}

/* ========================================================================
   ARC (Automatic Reference Counting)
   ======================================================================== */

/* Get the header from a user-data pointer. */
static BlangRefHeader *get_header( void *ptr )
{
	return (BlangRefHeader *)( (char *)ptr - sizeof( BlangRefHeader ) );
}

void *__blang_rc_alloc( size_t data_size )
{
	BlangRefHeader *hdr = (BlangRefHeader *)__blang_calloc( 1, sizeof( BlangRefHeader ) + data_size );
	hdr->ref_count = 1;
	hdr->is_sync = 0;
	hdr->mutex = NULL;
	hdr->destructor = NULL;
	return (char *)hdr + sizeof( BlangRefHeader );
}

void *__blang_rc_alloc_dtor( size_t data_size, blang_dtor_fn dtor )
{
	BlangRefHeader *hdr = (BlangRefHeader *)__blang_calloc( 1, sizeof( BlangRefHeader ) + data_size );
	hdr->ref_count = 1;
	hdr->is_sync = 0;
	hdr->mutex = NULL;
	hdr->destructor = dtor;
	return (char *)hdr + sizeof( BlangRefHeader );
}

void *__blang_rc_alloc_sync( size_t data_size )
{
	BlangRefHeader *hdr = (BlangRefHeader *)__blang_calloc( 1, sizeof( BlangRefHeader ) + data_size );
	hdr->ref_count = 1;
	hdr->is_sync = 1;
	hdr->destructor = NULL;
	hdr->mutex = malloc( sizeof( pthread_mutex_t ) );
	if ( hdr->mutex != NULL )
	{
		/* Use recursive mutex to prevent deadlocks when nested expressions
		   read the same sync variable (e.g., counter = counter + counter). */
		pthread_mutexattr_t attr;
		pthread_mutexattr_init( &attr );
		pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_RECURSIVE );
		pthread_mutex_init( (pthread_mutex_t *)hdr->mutex, &attr );
		pthread_mutexattr_destroy( &attr );
	}
	return (char *)hdr + sizeof( BlangRefHeader );
}

void __blang_rc_retain( void *ptr )
{
	if ( ptr == NULL )
		return;
	BlangRefHeader *hdr = get_header( ptr );
	__atomic_add_fetch( &hdr->ref_count, 1, __ATOMIC_SEQ_CST );
}

void __blang_rc_release( void *ptr )
{
	if ( ptr == NULL )
		return;
	BlangRefHeader *hdr = get_header( ptr );
	int32_t new_count = __atomic_sub_fetch( &hdr->ref_count, 1, __ATOMIC_SEQ_CST );
	if ( new_count <= 0 )
	{
		if ( hdr->destructor != NULL )
			hdr->destructor( ptr );
		if ( hdr->mutex != NULL )
		{
			pthread_mutex_destroy( (pthread_mutex_t *)hdr->mutex );
			free( hdr->mutex );
		}
		free( hdr );
	}
}

void __blang_sync_lock( void *ptr )
{
	if ( ptr == NULL )
		return;
	BlangRefHeader *hdr = get_header( ptr );
	if ( hdr->is_sync && hdr->mutex != NULL )
		pthread_mutex_lock( (pthread_mutex_t *)hdr->mutex );
}

void __blang_sync_unlock( void *ptr )
{
	if ( ptr == NULL )
		return;
	BlangRefHeader *hdr = get_header( ptr );
	if ( hdr->is_sync && hdr->mutex != NULL )
		pthread_mutex_unlock( (pthread_mutex_t *)hdr->mutex );
}

/* ========================================================================
   Lambda Context Lifetime
   ======================================================================== */

void __blang_lambda_ctx_retain( void *ctx )
{
	if ( ctx == NULL )
		return;
	int64_t *rc = (int64_t *)ctx;
	__atomic_add_fetch( rc, 1, __ATOMIC_SEQ_CST );
}

void __blang_lambda_ctx_release( void *ctx )
{
	if ( ctx == NULL )
		return;
	int64_t *rc = (int64_t *)ctx;
	if ( __atomic_sub_fetch( rc, 1, __ATOMIC_SEQ_CST ) <= 0 )
	{
		/* Destructor is stored at offset 8 (after the i64 refcount) */
		void (**dtor)(void *) = (void (**)(void *))( (char *)ctx + sizeof( int64_t ) );
		if ( *dtor != NULL )
			(*dtor)( ctx );
		free( ctx );
	}
}

/* ========================================================================
   Spawn — detached threads (one pthread per spawn)
   ======================================================================== */

struct BlangSpawnTask
{
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t done_cond;
	int completed;  /* 0 = running, 1 = done */
	blang_spawn_fn fn;
	void *ctx;
};

/* Global tracker for in-flight tasks and wait_all support. */
typedef struct SpawnTracker
{
	pthread_mutex_t mutex;
	pthread_cond_t all_done;
	int tasks_in_flight;
	BlangSpawnTask **handles;
	int handle_count;
	int handle_capacity;
	int initialized;
} SpawnTracker;

static SpawnTracker g_tracker = { .initialized = 0 };

static void tracker_init( void )
{
	if ( g_tracker.initialized )
		return;
	pthread_mutex_init( &g_tracker.mutex, NULL );
	pthread_cond_init( &g_tracker.all_done, NULL );
	g_tracker.tasks_in_flight = 0;
	g_tracker.handle_capacity = 64;
	g_tracker.handle_count = 0;
	g_tracker.handles = (BlangSpawnTask **)calloc(
		g_tracker.handle_capacity, sizeof( BlangSpawnTask * ) );
	g_tracker.initialized = 1;
}

static void *spawn_thread_fn( void *arg )
{
	BlangSpawnTask *task = (BlangSpawnTask *)arg;

	/* Execute the spawn body. */
	task->fn( task->ctx );

	/* Free the context (heap-allocated by codegen). */
	if ( task->ctx != NULL )
		free( task->ctx );

	/* Mark completed and signal waiters. */
	pthread_mutex_lock( &task->mutex );
	task->completed = 1;
	pthread_cond_signal( &task->done_cond );
	pthread_mutex_unlock( &task->mutex );

	/* Decrement in-flight counter. */
	pthread_mutex_lock( &g_tracker.mutex );
	g_tracker.tasks_in_flight--;
	if ( g_tracker.tasks_in_flight == 0 )
		pthread_cond_signal( &g_tracker.all_done );
	pthread_mutex_unlock( &g_tracker.mutex );

	return NULL;
}

void __blang_runtime_init( int num_threads )
{
	(void)num_threads;
	tracker_init();
}

BlangSpawnTask *__blang_spawn( blang_spawn_fn fn, void *ctx )
{
	tracker_init();

	BlangSpawnTask *task = (BlangSpawnTask *)__blang_calloc( 1, sizeof( BlangSpawnTask ) );
	pthread_mutex_init( &task->mutex, NULL );
	pthread_cond_init( &task->done_cond, NULL );
	task->completed = 0;
	task->fn = fn;
	task->ctx = ctx;

	/* Track the handle. */
	pthread_mutex_lock( &g_tracker.mutex );
	g_tracker.tasks_in_flight++;
	if ( g_tracker.handle_count >= g_tracker.handle_capacity )
	{
		int new_capacity = g_tracker.handle_capacity * 2;
		/* realloc into a temporary so the original block is not leaked if the
		   reallocation fails. */
		BlangSpawnTask **grown = (BlangSpawnTask **)realloc(
			g_tracker.handles,
			new_capacity * sizeof( BlangSpawnTask * ) );
		if ( grown == NULL )
		{
			pthread_mutex_unlock( &g_tracker.mutex );
			__blang_oom( "spawn handle table" );
		}
		g_tracker.handles = grown;
		g_tracker.handle_capacity = new_capacity;
	}
	g_tracker.handles[g_tracker.handle_count++] = task;
	pthread_mutex_unlock( &g_tracker.mutex );

	/* Create a joinable thread for this spawn.  The handle is tracked so the
	   thread can be joined at wait/shutdown time (see __blang_runtime_shutdown),
	   which is what guarantees spawned work completes and is freed. */
	pthread_attr_t attr;
	pthread_attr_init( &attr );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_JOINABLE );
	pthread_create( &task->thread, &attr, spawn_thread_fn, task );
	pthread_attr_destroy( &attr );

	return task;
}

void __blang_runtime_shutdown( void )
{
	if ( !g_tracker.initialized )
		return;

	/* Wait for all in-flight tasks to complete. */
	pthread_mutex_lock( &g_tracker.mutex );
	while ( g_tracker.tasks_in_flight > 0 )
		pthread_cond_wait( &g_tracker.all_done, &g_tracker.mutex );
	pthread_mutex_unlock( &g_tracker.mutex );

	/* Join and free all tracked task handles. */
	for ( int i = 0; i < g_tracker.handle_count; i++ )
	{
		if ( g_tracker.handles[i] != NULL )
		{
			pthread_join( g_tracker.handles[i]->thread, NULL );
			__blang_spawn_task_destroy( g_tracker.handles[i] );
		}
	}
	free( g_tracker.handles );

	pthread_mutex_destroy( &g_tracker.mutex );
	pthread_cond_destroy( &g_tracker.all_done );
	g_tracker.handles = NULL;
	g_tracker.handle_count = 0;
	g_tracker.handle_capacity = 0;
	g_tracker.initialized = 0;
}

/* ========================================================================
   Task Handles (spawn + wait)
   ======================================================================== */

void __blang_spawn_wait( BlangSpawnTask *task )
{
	if ( task == NULL )
		return;

	pthread_mutex_lock( &task->mutex );
	while ( !task->completed )
		pthread_cond_wait( &task->done_cond, &task->mutex );
	pthread_mutex_unlock( &task->mutex );
}

void __blang_spawn_task_destroy( BlangSpawnTask *task )
{
	if ( task == NULL )
		return;

	/* Remove from tracker to prevent double-free. */
	if ( g_tracker.initialized )
	{
		for ( int i = 0; i < g_tracker.handle_count; i++ )
		{
			if ( g_tracker.handles[i] == task )
			{
				g_tracker.handles[i] = NULL;
				break;
			}
		}
	}

	pthread_mutex_destroy( &task->mutex );
	pthread_cond_destroy( &task->done_cond );
	free( task );
}

void __blang_wait_all( void )
{
	if ( !g_tracker.initialized )
		return;

	pthread_mutex_lock( &g_tracker.mutex );
	while ( g_tracker.tasks_in_flight > 0 )
		pthread_cond_wait( &g_tracker.all_done, &g_tracker.mutex );
	pthread_mutex_unlock( &g_tracker.mutex );
}

/* ========================================================================
   Channels
   ======================================================================== */

struct BlangChan
{
	void *buffer;          /* circular buffer */
	size_t elem_size;      /* size of each element */
	size_t capacity;       /* max elements in buffer */
	size_t head;           /* read position */
	size_t tail;           /* write position */
	size_t count;          /* current number of elements */
	int closed;            /* 1 if channel is closed */
	int refcount;          /* references held (creator + capturing spawns) */
	pthread_mutex_t mutex;
	pthread_cond_t not_empty;
	pthread_cond_t not_full;
};

BlangChan *__blang_chan_create( size_t elem_size, size_t capacity )
{
	if ( capacity == 0 )
		capacity = 1;  /* minimum buffer of 1 for rendezvous */
	if ( elem_size == 0 )
		elem_size = 1; /* avoid zero-stride ring buffer math */

	BlangChan *ch = (BlangChan *)__blang_calloc( 1, sizeof( BlangChan ) );
	ch->elem_size = elem_size;
	ch->capacity = capacity;
	ch->buffer = __blang_calloc( capacity, elem_size );
	ch->head = 0;
	ch->tail = 0;
	ch->count = 0;
	ch->closed = 0;
	ch->refcount = 1;
	pthread_mutex_init( &ch->mutex, NULL );
	pthread_cond_init( &ch->not_empty, NULL );
	pthread_cond_init( &ch->not_full, NULL );
	return ch;
}

void __blang_chan_send( BlangChan *ch, const void *data )
{
	if ( ch == NULL || data == NULL )
		return;

	pthread_mutex_lock( &ch->mutex );

	while ( ch->count == ch->capacity && !ch->closed )
		pthread_cond_wait( &ch->not_full, &ch->mutex );

	if ( ch->closed )
	{
		pthread_mutex_unlock( &ch->mutex );
		return;
	}

	memcpy( (char *)ch->buffer + ch->tail * ch->elem_size, data, ch->elem_size );
	ch->tail = ( ch->tail + 1 ) % ch->capacity;
	ch->count++;
	pthread_cond_signal( &ch->not_empty );
	pthread_mutex_unlock( &ch->mutex );
}

int __blang_chan_recv( BlangChan *ch, void *data_out )
{
	if ( ch == NULL || data_out == NULL )
		return 0;

	pthread_mutex_lock( &ch->mutex );

	while ( ch->count == 0 && !ch->closed )
		pthread_cond_wait( &ch->not_empty, &ch->mutex );

	if ( ch->count == 0 && ch->closed )
	{
		pthread_mutex_unlock( &ch->mutex );
		return 0;
	}

	memcpy( data_out, (char *)ch->buffer + ch->head * ch->elem_size, ch->elem_size );
	ch->head = ( ch->head + 1 ) % ch->capacity;
	ch->count--;
	pthread_cond_signal( &ch->not_full );
	pthread_mutex_unlock( &ch->mutex );
	return 1;
}

void __blang_chan_close( BlangChan *ch )
{
	if ( ch == NULL )
		return;

	pthread_mutex_lock( &ch->mutex );
	ch->closed = 1;
	pthread_cond_broadcast( &ch->not_empty );
	pthread_cond_broadcast( &ch->not_full );
	pthread_mutex_unlock( &ch->mutex );
}

void __blang_chan_destroy( BlangChan *ch )
{
	if ( ch == NULL )
		return;
	pthread_mutex_destroy( &ch->mutex );
	pthread_cond_destroy( &ch->not_empty );
	pthread_cond_destroy( &ch->not_full );
	free( ch->buffer );
	free( ch );
}

void __blang_chan_retain( BlangChan *ch )
{
	if ( ch == NULL )
		return;
	__atomic_add_fetch( &ch->refcount, 1, __ATOMIC_SEQ_CST );
}

void __blang_chan_release( BlangChan *ch )
{
	if ( ch == NULL )
		return;
	if ( __atomic_sub_fetch( &ch->refcount, 1, __ATOMIC_SEQ_CST ) == 0 )
		__blang_chan_destroy( ch );
}

/* ========================================================================
   Async / Tasks
   ======================================================================== */

#ifdef BLANG_HAS_LIBUV

struct BlangTask
{
	uv_work_t work_req;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	void *result;
	int completed;
	blang_async_fn fn;
	void *arg;
};

static void async_work_cb( uv_work_t *req )
{
	BlangTask *task = (BlangTask *)req->data;
	task->result = task->fn( task->arg );
}

static void async_after_cb( uv_work_t *req, int status )
{
	BlangTask *task = (BlangTask *)req->data;
	(void)status;

	pthread_mutex_lock( &task->mutex );
	task->completed = 1;
	pthread_cond_signal( &task->cond );
	pthread_mutex_unlock( &task->mutex );
}

static void *loop_thread_fn( void *arg )
{
	uv_loop_t *loop = (uv_loop_t *)arg;
	uv_run( loop, UV_RUN_DEFAULT );
	return NULL;
}

/* Ensure the global event loop is running. */
static void ensure_async_loop( void )
{
	if ( g_async_loop != NULL )
		return;

	g_async_loop = uv_default_loop();
	g_loop_running = 1;
	pthread_create( &g_loop_thread, NULL, loop_thread_fn, g_async_loop );
}

BlangTask *__blang_async_call( blang_async_fn fn, void *arg )
{
	ensure_async_loop();

	BlangTask *task = (BlangTask *)calloc( 1, sizeof( BlangTask ) );
	task->fn = fn;
	task->arg = arg;
	task->result = NULL;
	task->completed = 0;
	pthread_mutex_init( &task->mutex, NULL );
	pthread_cond_init( &task->cond, NULL );

	task->work_req.data = task;
	uv_queue_work( g_async_loop, &task->work_req, async_work_cb, async_after_cb );
	return task;
}

void *__blang_await( BlangTask *task )
{
	if ( task == NULL )
		return NULL;

	pthread_mutex_lock( &task->mutex );
	while ( !task->completed )
		pthread_cond_wait( &task->cond, &task->mutex );
	pthread_mutex_unlock( &task->mutex );

	return task->result;
}

void __blang_task_destroy( BlangTask *task )
{
	if ( task != NULL )
	{
		pthread_mutex_destroy( &task->mutex );
		pthread_cond_destroy( &task->cond );
		free( task );
	}
}

#else /* !BLANG_HAS_LIBUV — pthread-per-call fallback */

struct BlangTask
{
	pthread_t thread;
	void *result;
	int completed;
	blang_async_fn fn;
	void *arg;
};

static void *async_thread_wrapper( void *task_ptr )
{
	BlangTask *task = (BlangTask *)task_ptr;
	task->result = task->fn( task->arg );
	task->completed = 1;
	return NULL;
}

BlangTask *__blang_async_call( blang_async_fn fn, void *arg )
{
	BlangTask *task = (BlangTask *)calloc( 1, sizeof( BlangTask ) );
	task->fn = fn;
	task->arg = arg;
	task->result = NULL;
	task->completed = 0;
	pthread_create( &task->thread, NULL, async_thread_wrapper, task );
	return task;
}

void *__blang_await( BlangTask *task )
{
	if ( task == NULL )
		return NULL;
	if ( !task->completed )
		pthread_join( task->thread, NULL );
	return task->result;
}

void __blang_task_destroy( BlangTask *task )
{
	if ( task != NULL )
		free( task );
}

#endif /* BLANG_HAS_LIBUV */
