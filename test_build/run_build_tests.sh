#!/bin/bash
# Build-system integration test: library -> .a + .bmod -> consumer binary.
# Exercises cross-module GENERICS end to end: the .bmod ships generic bodies,
# the consumer monomorphizes them (int/double/string + a generic struct with
# methods), and linkonce_odr dedups the instantiation the library itself uses.
set -u
cd "$(dirname "$0")"
RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
FAILS=0
check() {
	if eval "$2"; then echo -e "  ${GREEN}PASS${NC}  $1"
	else echo -e "  ${RED}FAIL${NC}  $1"; FAILS=$((FAILS+1)); fi
}

BCC=../build/bcc
[ -x "$BCC" ] || { echo "bcc not built"; exit 1; }
BCC="$(cd .. && pwd)/build/bcc"

# Fresh build: no stale cache artifacts.
"$BCC" clean > /dev/null 2>&1

( cd mathlib && rm -f libmathlib.a mathlib.bmod && "$BCC" build > /dev/null 2>&1 )
check "mathlib builds (lib)" "[ -f mathlib/libmathlib.a ] && [ -f mathlib/mathlib.bmod ]"
check "bmod ships generic fn body" "grep -q 'pub fn largest<T>(T a, T b) -> T {' mathlib/mathlib.bmod"
check "bmod ships generic struct impl" "grep -q 'impl Pair {' mathlib/mathlib.bmod"
check "bmod keeps non-generics signature-only" "grep -q 'pub fn add(int a, int b) -> int;' mathlib/mathlib.bmod"

( cd myapp && rm -f myapp && "$BCC" build > /dev/null 2>&1 )
check "myapp builds (bin)" "[ -x myapp/myapp ]"

OUT=$(cd myapp && ./myapp)
EXPECTED='3 + 4 = 7
5 * 6 = 30
largest int = 9
largest dbl = 7.25
sum = 42
swapped = 32 10
max3 = 11
largest str = pear
concat = ada lovelace
flipped = lovelaceada '
check "myapp output exact" "[ \"\$OUT\" = \"\$EXPECTED\" ]"
check "linkonce instantiations dedup (largest_int weak, single)" \
	"[ \"\$(nm myapp/myapp | grep -c ' W largest_int')\" = '1' ]"
check "no unresolved-param instantiation (largest_T absent)" \
	"! nm myapp/myapp | grep -q 'largest_T'"

( cd timerapp && rm -f timerapp && "$BCC" build > /dev/null 2>&1 )
check "timerapp builds (stdlib import)" "[ -x timerapp/timerapp ]"

echo ""
if [ $FAILS -eq 0 ]; then echo -e "${GREEN}All build-system checks passed${NC}"; exit 0
else echo -e "${RED}$FAILS check(s) failed${NC}"; exit 1; fi
