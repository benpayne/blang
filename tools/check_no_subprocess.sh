#!/bin/sh
# modules-v2-lsp U1 (invariant C4/I8, audit F3): binary-level proof that a
# program can never spawn a process or open a network connection. Lists the
# binary's undefined dynamic symbols (nm -D --undefined-only), strips the
# @VERSION suffix, and fails if any is EXACTLY one of the forbidden
# process/network entry points. Also fails when the undefined-symbol list is
# empty (a static link must not pass vacuously) or when nm is missing/fails.
#
# Usage: tools/check_no_subprocess.sh <binary>
# ctest: blangd_no_subprocess (blangd must pass) and blangd_no_subprocess_teeth
# (bcc, which really does fork/exec, must be caught — PASS_REGULAR_EXPRESSION).

bin="$1"
if [ -z "$bin" ]; then
	echo "usage: $0 <binary>" >&2
	exit 2
fi
if ! command -v nm >/dev/null 2>&1; then
	echo "no-subprocess: FAIL nm not found" >&2
	exit 2
fi
syms=$(nm -D --undefined-only "$bin" 2>/dev/null) || {
	echo "no-subprocess: FAIL nm failed on $bin" >&2
	exit 2
}
names=$(printf '%s\n' "$syms" | awk '{ print $NF }' | sed 's/@.*$//' | grep -v '^$')
if [ -z "$names" ]; then
	echo "no-subprocess: FAIL $bin has no undefined dynamic symbols (static link?)" >&2
	exit 2
fi

forbidden="popen system fork vfork execl execlp execle execv execvp execvpe execve fexecve posix_spawn posix_spawnp socket connect getaddrinfo"
offenders=""
for f in $forbidden; do
	if printf '%s\n' "$names" | grep -qx "$f"; then
		offenders="$offenders $f"
	fi
done
if [ -n "$offenders" ]; then
	echo "no-subprocess: FAIL $bin imports forbidden symbols:$offenders"
	exit 1
fi
echo "no-subprocess: OK $bin"
exit 0
