/* Unit tests for runtime/blang_net.c — deterministic error/lifecycle paths only
 * (no flaky real-network binding). */
#include "test_util.h"
#include "../blang_net.h"

static void t_connect_refused( void )
{
	/* Nothing listens on 127.0.0.1:1 -> connect must fail (negative fd). */
	int fd = __blang_tcp_connect( "127.0.0.1", 1 );
	CHECK_TRUE( fd < 0 );
	if ( fd >= 0 )
		__blang_tcp_close( fd );
}

static void t_connect_bad_host( void )
{
	/* Unresolvable host -> failure, no crash. */
	int fd = __blang_tcp_connect( "no.such.host.invalid.", 80 );
	CHECK_TRUE( fd < 0 );
	if ( fd >= 0 )
		__blang_tcp_close( fd );
}

static void t_selector_lifecycle( void )
{
	int sel = __blang_selector_create();
	CHECK_TRUE( sel >= 0 );
	__blang_selector_destroy( sel ); /* must not crash */
}

static void t_close_invalid( void )
{
	/* Closing a bogus fd must not crash the process. */
	__blang_tcp_close( -1 );
	CHECK_TRUE( 1 );
}

static const blang_test_case cases[] = {
	{ "connect_refused",    t_connect_refused },
	{ "connect_bad_host",   t_connect_bad_host },
	{ "selector_lifecycle", t_selector_lifecycle },
	{ "close_invalid",      t_close_invalid },
};
TEST_MAIN( cases )
