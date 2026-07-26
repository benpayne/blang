/* runtime/blang_time.c — BLang time module C backing. */
#include "blang_time.h"

#include <time.h>

int64_t __blang_time_now( void )
{
	return (int64_t)time( NULL );
}

int64_t __blang_time_now_millis( void )
{
	struct timespec ts;
	clock_gettime( CLOCK_REALTIME, &ts );
	return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}

int64_t __blang_time_monotonic_nanos( void )
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}
