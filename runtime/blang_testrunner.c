/*
 * blang_testrunner — fork-isolated test driver for `bcc test`.
 * See blang_testrunner.h for the codegen contract.
 *
 * Dependency-free (libc + POSIX fork/waitpid only), per design D7.
 */

#include "blang_testrunner.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#define BLANG_TEST_MAX 4096

static const char   *g_test_names[BLANG_TEST_MAX];
static blang_test_fn g_test_fns[BLANG_TEST_MAX];
static int           g_test_count = 0;

void __blang_test_register(const char *name, blang_test_fn fn)
{
	if (g_test_count < BLANG_TEST_MAX)
	{
		g_test_names[g_test_count] = name;
		g_test_fns[g_test_count] = fn;
		g_test_count++;
	}
}

int __blang_test_main(int argc, char **argv)
{
	const char *filter = NULL;

	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc)
			filter = argv[++i];
		else if (strncmp(argv[i], "--filter=", 9) == 0)
			filter = argv[i] + 9;
	}

	int passed = 0;
	int failed = 0;
	int ran = 0;

	for (int i = 0; i < g_test_count; i++)
	{
		if (filter != NULL && strstr(g_test_names[i], filter) == NULL)
			continue;

		ran++;

		/* Flush so buffered parent output is not duplicated into the child. */
		fflush(stdout);
		fflush(stderr);

		pid_t pid = fork();
		if (pid < 0)
		{
			/* Fork failed — treat as an infrastructure failure for this test. */
			failed++;
			printf("FAIL  %s (fork failed)\n", g_test_names[i]);
			continue;
		}

		if (pid == 0)
		{
			/* Child: run the test. A failing assert calls exit(non-zero) and
			 * prints its located diagnostic to the shared stdout. If the body
			 * returns, the test passed. Flush any buffered test output before
			 * _exit (which, unlike exit, does not flush stdio). */
			g_test_fns[i]();
			fflush(NULL);
			_exit(0);
		}

		/* Parent: wait for the isolated child and record the outcome. */
		int status = 0;
		while (waitpid(pid, &status, 0) < 0)
			; /* retry on EINTR */

		int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
		if (ok)
		{
			passed++;
			printf("PASS  %s\n", g_test_names[i]);
		}
		else
		{
			failed++;
			printf("FAIL  %s\n", g_test_names[i]);
		}
	}

	printf("\n%d passed, %d failed (%d total)\n", passed, failed, ran);
	fflush(stdout);

	return failed > 0 ? 1 : 0;
}
