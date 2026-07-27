#!/bin/bash
# Integration test for the wordfreq example: builds with bcc, checks the demo
# output (sorted word counts), and runs the colocated `test` blocks.

set -u
APP_DIR="$(cd "$(dirname "$0")" && pwd)"
BCC="${APP_DIR}/../../build/bcc"

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
PASS=0; FAIL=0
check() {
	if [ "$2" = "$3" ]; then
		echo -e "  ${GREEN}PASS${NC}  $1"; PASS=$((PASS + 1))
	else
		echo -e "  ${RED}FAIL${NC}  $1"
		echo "        expected: $2"
		echo "        actual:   $3"
		FAIL=$((FAIL + 1))
	fi
}

if [ ! -x "$BCC" ]; then
	echo -e "${RED}bcc not found at $BCC (build with LLVM first)${NC}"; exit 1
fi

cd "$APP_DIR" || exit 1
trap 'rm -f wordfreq' EXIT

echo "=== building wordfreq ==="
if ! "$BCC" build > /tmp/wordfreq_build.log 2>&1; then
	echo -e "${RED}build failed${NC}"; tail -5 /tmp/wordfreq_build.log; exit 1
fi

echo "=== demo output ==="
expected="barks: 1
brown: 1
dog: 2
fox: 2
hill: 1
jumps: 1
lazy: 1
quick: 1
runs: 1
9 distinct words (3 stop words ignored)"
check "sorted word counts" "$expected" "$(./wordfreq)"

echo "=== unit tests (bcc test) ==="
if "$BCC" test > /tmp/wordfreq_unit.log 2>&1; then
	grep -E "passed" /tmp/wordfreq_unit.log
	echo -e "  ${GREEN}PASS${NC}  bcc test green"; PASS=$((PASS + 1))
else
	tail -10 /tmp/wordfreq_unit.log
	echo -e "  ${RED}FAIL${NC}  bcc test reported failures"; FAIL=$((FAIL + 1))
fi

echo
echo "Passed: $PASS  Failed: $FAIL"
[ "$FAIL" -eq 0 ]
