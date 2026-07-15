#ifndef BLANG_TESTRUNNER_H
#define BLANG_TESTRUNNER_H

/*
 * blang_testrunner — the C runtime driver behind `bcc test`.
 *
 * Codegen (under qcc --emit-test-main) emits a main() that:
 *   1. calls __blang_test_register(name, fn) once per test{} block, and
 *   2. tail-calls __blang_test_main(argc, argv) and returns its result.
 *
 * The driver runs each selected test in a forked child so a failing assert
 * (which exits the child non-zero) is recorded as a FAIL and does not abort
 * sibling tests. It counts pass/fail, honors --filter <substr>, prints a
 * summary line containing "<N> passed", and returns non-zero iff any test
 * failed. Output is plain ASCII (no ANSI) so CI greps match on piped output.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* A generated test function: takes no args, returns void. A failing assert
 * inside it exits the process (in test mode, the child) with a non-zero code. */
typedef void (*blang_test_fn)(void);

/* Register a test block by name with its generated function. Called from the
 * codegen-emitted main() before __blang_test_main. */
void __blang_test_register(const char *name, blang_test_fn fn);

/* Run all registered tests (optionally filtered by a --filter <substr> arg in
 * argv). Prints per-test PASS/FAIL and a summary; returns 1 iff any failed. */
int __blang_test_main(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* BLANG_TESTRUNNER_H */
