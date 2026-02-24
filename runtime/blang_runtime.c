#include "blang_runtime.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

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
	BlangRefHeader *hdr = (BlangRefHeader *)calloc( 1, sizeof( BlangRefHeader ) + data_size );
	if ( hdr == NULL )
		return NULL;
	hdr->ref_count = 1;
	hdr->is_sync = 0;
	hdr->mutex = NULL;
	return (char *)hdr + sizeof( BlangRefHeader );
}

void *__blang_rc_alloc_sync( size_t data_size )
{
	BlangRefHeader *hdr = (BlangRefHeader *)calloc( 1, sizeof( BlangRefHeader ) + data_size );
	if ( hdr == NULL )
		return NULL;
	hdr->ref_count = 1;
	hdr->is_sync = 1;
	hdr->mutex = malloc( sizeof( pthread_mutex_t ) );
	if ( hdr->mutex != NULL )
		pthread_mutex_init( (pthread_mutex_t *)hdr->mutex, NULL );
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
   Green Thread Pool (spawn)
   ======================================================================== */

/* Simple fixed-size task queue. */
#define TASK_QUEUE_CAPACITY 4096

typedef struct TaskQueue
{
	blang_spawn_fn tasks[TASK_QUEUE_CAPACITY];
	int head;
	int tail;
	int count;
	pthread_mutex_t mutex;
	pthread_cond_t not_empty;
	pthread_cond_t not_full;
	int shutdown;
} TaskQueue;

typedef struct ThreadPool
{
	pthread_t *threads;
	int num_threads;
	TaskQueue queue;
	int tasks_in_flight;  /* tasks submitted but not yet completed */
	pthread_mutex_t flight_mutex;
	pthread_cond_t all_done;
} ThreadPool;

/* Global thread pool instance. */
static ThreadPool *g_pool = NULL;

static void *worker_thread( void *arg )
{
	ThreadPool *pool = (ThreadPool *)arg;
	TaskQueue *q = &pool->queue;

	for ( ;; )
	{
		pthread_mutex_lock( &q->mutex );

		while ( q->count == 0 && !q->shutdown )
			pthread_cond_wait( &q->not_empty, &q->mutex );

		if ( q->shutdown && q->count == 0 )
		{
			pthread_mutex_unlock( &q->mutex );
			break;
		}

		blang_spawn_fn fn = q->tasks[q->head];
		q->head = ( q->head + 1 ) % TASK_QUEUE_CAPACITY;
		q->count--;
		pthread_cond_signal( &q->not_full );
		pthread_mutex_unlock( &q->mutex );

		/* Execute the task. */
		fn();

		/* Decrement in-flight counter. */
		pthread_mutex_lock( &pool->flight_mutex );
		pool->tasks_in_flight--;
		if ( pool->tasks_in_flight == 0 )
			pthread_cond_signal( &pool->all_done );
		pthread_mutex_unlock( &pool->flight_mutex );
	}

	return NULL;
}

void __blang_runtime_init( int num_threads )
{
	if ( g_pool != NULL )
		return;

	if ( num_threads <= 0 )
		num_threads = 4;  /* default */

	g_pool = (ThreadPool *)calloc( 1, sizeof( ThreadPool ) );
	g_pool->num_threads = num_threads;
	g_pool->tasks_in_flight = 0;

	TaskQueue *q = &g_pool->queue;
	q->head = 0;
	q->tail = 0;
	q->count = 0;
	q->shutdown = 0;
	pthread_mutex_init( &q->mutex, NULL );
	pthread_cond_init( &q->not_empty, NULL );
	pthread_cond_init( &q->not_full, NULL );

	pthread_mutex_init( &g_pool->flight_mutex, NULL );
	pthread_cond_init( &g_pool->all_done, NULL );

	g_pool->threads = (pthread_t *)calloc( num_threads, sizeof( pthread_t ) );
	for ( int i = 0; i < num_threads; i++ )
		pthread_create( &g_pool->threads[i], NULL, worker_thread, g_pool );
}

void __blang_spawn( blang_spawn_fn fn )
{
	if ( g_pool == NULL )
		__blang_runtime_init( 0 );

	TaskQueue *q = &g_pool->queue;

	/* Increment in-flight count before enqueuing. */
	pthread_mutex_lock( &g_pool->flight_mutex );
	g_pool->tasks_in_flight++;
	pthread_mutex_unlock( &g_pool->flight_mutex );

	pthread_mutex_lock( &q->mutex );

	while ( q->count == TASK_QUEUE_CAPACITY && !q->shutdown )
		pthread_cond_wait( &q->not_full, &q->mutex );

	if ( q->shutdown )
	{
		pthread_mutex_unlock( &q->mutex );
		return;
	}

	q->tasks[q->tail] = fn;
	q->tail = ( q->tail + 1 ) % TASK_QUEUE_CAPACITY;
	q->count++;
	pthread_cond_signal( &q->not_empty );
	pthread_mutex_unlock( &q->mutex );
}

void __blang_runtime_shutdown( void )
{
	if ( g_pool == NULL )
		return;

	/* Wait for all in-flight tasks to complete. */
	pthread_mutex_lock( &g_pool->flight_mutex );
	while ( g_pool->tasks_in_flight > 0 )
		pthread_cond_wait( &g_pool->all_done, &g_pool->flight_mutex );
	pthread_mutex_unlock( &g_pool->flight_mutex );

	/* Signal workers to shut down. */
	TaskQueue *q = &g_pool->queue;
	pthread_mutex_lock( &q->mutex );
	q->shutdown = 1;
	pthread_cond_broadcast( &q->not_empty );
	pthread_mutex_unlock( &q->mutex );

	for ( int i = 0; i < g_pool->num_threads; i++ )
		pthread_join( g_pool->threads[i], NULL );

	free( g_pool->threads );
	pthread_mutex_destroy( &q->mutex );
	pthread_cond_destroy( &q->not_empty );
	pthread_cond_destroy( &q->not_full );
	pthread_mutex_destroy( &g_pool->flight_mutex );
	pthread_cond_destroy( &g_pool->all_done );
	free( g_pool );
	g_pool = NULL;
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
	pthread_mutex_t mutex;
	pthread_cond_t not_empty;
	pthread_cond_t not_full;
};

BlangChan *__blang_chan_create( size_t elem_size, size_t capacity )
{
	if ( capacity == 0 )
		capacity = 1;  /* minimum buffer of 1 for rendezvous */

	BlangChan *ch = (BlangChan *)calloc( 1, sizeof( BlangChan ) );
	ch->elem_size = elem_size;
	ch->capacity = capacity;
	ch->buffer = calloc( capacity, elem_size );
	ch->head = 0;
	ch->tail = 0;
	ch->count = 0;
	ch->closed = 0;
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

/* ========================================================================
   Async / Tasks
   ======================================================================== */

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
