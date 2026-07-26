/* Tiny dependency-free unit-test harness for the BLang C runtime libraries.
 *
 * No external framework (constitution/design D7): just assert-style macros, a
 * fork-based abort probe (to assert bounds/null guards terminate the process),
 * and a one-behavior-per-argv[1] dispatcher so each add_test() registers a
 * single focused known-answer test. A test program returns non-zero iff any
 * CHECK in the selected case failed.
 */
#ifndef BLANG_TEST_UTIL_H
#define BLANG_TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int g_fail = 0;

#define CHECK(cond) \
	do { if ( !(cond) ) { \
		fprintf( stderr, "  FAIL %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond ); \
		g_fail++; } } while ( 0 )

#define CHECK_EQ_I(a,b) \
	do { long long _a = (long long)(a), _b = (long long)(b); \
		if ( _a != _b ) { \
			fprintf( stderr, "  FAIL %s:%d: %s == %s : %lld != %lld\n", \
				__FILE__, __LINE__, #a, #b, _a, _b ); \
			g_fail++; } } while ( 0 )

#define CHECK_TRUE(a)  CHECK( (a) )
#define CHECK_FALSE(a) CHECK( !(a) )

#define CHECK_STR_EQ(a,b) \
	do { const char *_a = (a), *_b = (b); \
		if ( _a == NULL || _b == NULL || strcmp( _a, _b ) != 0 ) { \
			fprintf( stderr, "  FAIL %s:%d: str %s == %s : \"%s\" != \"%s\"\n", \
				__FILE__, __LINE__, #a, #b, _a ? _a : "(null)", _b ? _b : "(null)" ); \
			g_fail++; } } while ( 0 )

/* Run fn in a forked child (stderr silenced). Returns 1 if the child terminated
 * abnormally (non-zero exit or by signal) — i.e. a guard fired — and 0 if the
 * child returned and exited 0 (guard did NOT fire). Used to assert that a
 * bounds/null check aborts the process; removing the check makes the guarded
 * call return normally, so this flips to 0 and the test fails. */
typedef void (*blang_probe_fn)( void );
static int expect_abort( blang_probe_fn fn )
{
	pid_t pid = fork();
	if ( pid == 0 )
	{
		if ( freopen( "/dev/null", "w", stderr ) == NULL ) { /* ignore */ }
		fn();
		_exit( 0 ); /* fn returned normally -> the guard did not fire */
	}
	int status = 0;
	waitpid( pid, &status, 0 );
	if ( WIFSIGNALED( status ) )
		return 1;
	if ( WIFEXITED( status ) && WEXITSTATUS( status ) != 0 )
		return 1;
	return 0;
}

typedef struct { const char *name; void ( *fn )( void ); } blang_test_case;

/* Dispatcher: `prog <case>` runs one case (exit non-zero iff it had failures);
 * `prog` with no arg runs all. */
#define TEST_MAIN(cases) \
int main( int argc, char **argv ) \
{ \
	const blang_test_case *tc = (cases); \
	size_t n = sizeof(cases) / sizeof((cases)[0]); \
	if ( argc >= 2 ) \
	{ \
		for ( size_t i = 0; i < n; i++ ) \
			if ( strcmp( argv[1], tc[i].name ) == 0 ) { tc[i].fn(); return g_fail ? 1 : 0; } \
		fprintf( stderr, "unknown test case: %s\n", argv[1] ); \
		return 2; \
	} \
	for ( size_t i = 0; i < n; i++ ) tc[i].fn(); \
	return g_fail ? 1 : 0; \
}

#endif /* BLANG_TEST_UTIL_H */
