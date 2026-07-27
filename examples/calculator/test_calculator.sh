#!/bin/bash
# Integration test for the calculator example. Builds it with bcc, runs the demo
# and checks a couple of representative results, then runs the colocated `test`
# blocks via `bcc test` (exercising the built-in test framework end to end).
# Requires bcc built with LLVM.

set -u
APP_DIR="$(cd "$(dirname "$0")" && pwd)"
BCC="${APP_DIR}/../../build/bcc"

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'

if [ ! -x "$BCC" ]; then
	echo -e "${RED}bcc not found at $BCC (build with LLVM first)${NC}"; exit 1
fi

cd "$APP_DIR" || exit 1
trap 'rm -f calculator' EXIT

fails=0

echo "=== building calculator ==="
if ! "$BCC" build > /tmp/calc_build.log 2>&1; then
	echo -e "${RED}build failed${NC}"; tail -5 /tmp/calc_build.log; exit 1
fi

echo "=== running demo ==="
out="$(./calculator 2>&1)"; ec=$?
echo "$out"
if [ $ec -ne 0 ]; then
	echo -e "  ${RED}FAIL${NC} demo exited $ec"; fails=$((fails + 1))
fi
# Spot-check a couple of results (precedence + error reporting).
echo "$out" | grep -qF "1 + 2 * 3 = 7" || { echo -e "  ${RED}FAIL${NC} precedence"; fails=$((fails + 1)); }
echo "$out" | grep -qF "1 / 0 -> error: division by zero" || { echo -e "  ${RED}FAIL${NC} div-by-zero error"; fails=$((fails + 1)); }

echo "=== running test suite (bcc test) ==="
if "$BCC" test; then
	echo -e "  ${GREEN}PASS${NC} test suite green"
else
	echo -e "  ${RED}FAIL${NC} test suite reported failures"; fails=$((fails + 1))
fi

echo
if [ "$fails" -eq 0 ]; then
	echo -e "${GREEN}All calculator checks passed${NC}"
else
	echo -e "${RED}${fails} calculator check(s) failed${NC}"
fi
[ "$fails" -eq 0 ]
