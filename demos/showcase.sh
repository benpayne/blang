#!/bin/bash
#
# BLang showcase — a guided, reproducible tour of what the compiler does now.
#
# Every item below runs a COMMITTED artifact (a test fixture in test_files/, a
# demo in demos/, or the example app in examples/) — nothing is staged just for
# the tour, so this stays honest against the real compiler and green with CI.
#
# Three acts:
#   1. The compiler catches your mistakes   (test_files/fail/sema/*.b)
#   2. Real programs run end-to-end          (make -C demos run, the todo app)
#   3. Features that were impossible before  (channels, timers)
#
# Usage:  ./demos/showcase.sh            # from the repo root, after building
#         BUILD_DIR=path ./demos/showcase.sh
#
# Requires an LLVM build of qcc/bcc (cd build && cmake .. -DLLVM_DIR=... && make).

set -u
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
QCC="$BUILD_DIR/qcc"

if [ -t 1 ]; then
	B=$'\033[1m'; DIM=$'\033[2m'; GRN=$'\033[0;32m'; CYAN=$'\033[0;36m'; YEL=$'\033[0;33m'; NC=$'\033[0m'
else
	B=''; DIM=''; GRN=''; CYAN=''; YEL=''; NC=''
fi

if [ ! -x "$QCC" ]; then
	echo "qcc not found at $QCC — build first:" >&2
	echo "  cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build build -j\"\$(nproc)\"" >&2
	exit 1
fi

hr() { printf '%s\n' "${DIM}────────────────────────────────────────────────────────────${NC}"; }
act() { echo; hr; printf '%s%s%s\n' "$B" "$1" "$NC"; hr; }

# Run one negative fixture through parse+sema and show the located error, with a
# note on how the SAME program behaved before this work.
diag() {
	local file="$1" before="$2"
	local err lineno src
	err="$("$QCC" --parse-only "$file" 2>&1 | grep -m1 -E ':[0-9]+:[0-9]+: error: ')"
	# show the exact source line the error points at
	lineno="$(printf '%s' "$err" | sed -nE 's/.*\.b:([0-9]+):[0-9]+: error:.*/\1/p')"
	if [ -n "$lineno" ]; then
		src="$(sed -n "${lineno}p" "$file" | sed 's/^[[:space:]]*//')"
	else
		src="$(grep -m1 -vE '^\s*//|^\s*$' "$file")"
	fi
	printf '  %sprogram%s   %s\n' "$CYAN" "$NC" "$src"
	printf '  %sbefore%s    %s%s%s\n' "$YEL" "$NC" "$DIM" "$before" "$NC"
	printf '  %snow%s       %s%s%s\n\n' "$GRN" "$NC" "$GRN" "$err" "$NC"
}

echo "${B}BLang showcase${NC} — same compiler, run against committed artifacts."

# ─── Act 1 ────────────────────────────────────────────────────────────────
act "1. The compiler catches your mistakes  (test_files/fail/sema/)"
echo "Each of these is a committed regression test that CI asserts on every run."
echo
diag test_files/fail/sema/audit_04.b \
	'compiled clean, then CORRUPTED THE HEAP at runtime (munmap_chunk: invalid pointer)'
diag test_files/fail/sema/return_string_from_int.b \
	'emitted inttoptr — a fabricated garbage pointer; raw LLVM verifier text shown to you'
diag test_files/fail/sema/unknown_field.b \
	'the field access silently VANISHED — no error at all'
diag test_files/fail/sema/audit_05.b \
	'no arity check — the malformed call crashed LLVM'
diag test_files/fail/sema/audit_09.b \
	'exhaustiveness unenforced — the missing case silently fell through'
diag test_files/fail/sema/own_use_after_move.b \
	'caught only at codegen, with NO source location'

echo "${DIM}Note: clean file:line:col errors, no compiler-internal noise, no debug spew.${NC}"

# ─── Act 2 ────────────────────────────────────────────────────────────────
act "2. Real programs run end-to-end  (demos/, examples/)"
echo "The curated example programs — build, run, and check their output:"
echo
make -C demos run 2>&1 | grep -E 'PASS|FAIL|Results:'
echo
echo "${DIM}Plus a DB-backed web app that persists across restarts:${NC}"
echo "  examples/todo_app/  —  run 'bash examples/todo_app/test_todo_app.sh' for its E2E"

# ─── Act 3 ────────────────────────────────────────────────────────────────
act "3. Features that were impossible before"
echo "Concurrency that passes data, and timers — neither worked end-to-end before:"
echo
for t in codegen_channel codegen_timer_event; do
	if ./test_codegen.sh "test_files/$t.b" >/tmp/showcase_$t.log 2>&1; then
		printf '  %sPASS%s  test_files/%s.b\n' "$GRN" "$NC" "$t"
	else
		printf '  %sFAIL%s  test_files/%s.b (see /tmp/showcase_%s.log)\n' "$YEL" "$NC" "$t" "$t"
	fi
done

echo
hr
echo "${B}That's the tour.${NC} Every line above ran a committed artifact — re-run"
echo "this anytime; it stays green with CI."
