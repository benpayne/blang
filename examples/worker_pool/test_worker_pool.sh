#!/bin/bash
# Integration test for the worker_pool concurrency example: builds it with bcc
# and runs it repeatedly, checking the prime count is correct and stable
# (concurrency bugs are often intermittent). Requires bcc built with LLVM.

set -u
APP_DIR="$(cd "$(dirname "$0")" && pwd)"
BCC="${APP_DIR}/../../build/bcc"
RUNS=10
EXPECT="Found 9592 primes below 100000"

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'

if [ ! -x "$BCC" ]; then
	echo -e "${RED}bcc not found at $BCC (build with LLVM first)${NC}"; exit 1
fi

cd "$APP_DIR" || exit 1
trap 'rm -f worker_pool' EXIT

echo "=== building worker_pool ==="
if ! "$BCC" build > /tmp/wp_build.log 2>&1; then
	echo -e "${RED}build failed${NC}"; tail -5 /tmp/wp_build.log; exit 1
fi

echo "=== running ${RUNS}x ==="
fails=0
for i in $(seq 1 "$RUNS"); do
	out="$(timeout 30 ./worker_pool 2>&1)"; ec=$?
	if [ $ec -ne 0 ] || ! echo "$out" | grep -qF "$EXPECT"; then
		echo -e "  ${RED}FAIL${NC} run $i (exit $ec): $out"
		fails=$((fails + 1))
	fi
done

if [ "$fails" -eq 0 ]; then
	echo -e "  ${GREEN}PASS${NC} ${RUNS}/${RUNS} runs correct and stable"
	echo "  $(./worker_pool)"
else
	echo -e "  ${RED}${fails}/${RUNS} runs failed${NC}"
fi
[ "$fails" -eq 0 ]
