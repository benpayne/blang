#!/bin/bash
# Integration test for the kv example: builds it with bcc, drives every command
# through a scratch store file, checks outputs and exit codes, then runs the
# colocated `test` blocks via `bcc test`. Requires bcc built with LLVM.

set -u
APP_DIR="$(cd "$(dirname "$0")" && pwd)"
BCC="${APP_DIR}/../../build/bcc"
STORE="$(mktemp -u /tmp/kv_test_XXXX.store)"

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
PASS=0; FAIL=0
check() {  # check "name" expected actual
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
trap 'rm -f kv "$STORE"' EXIT

echo "=== building kv ==="
if ! "$BCC" build > /tmp/kv_build.log 2>&1; then
	echo -e "${RED}build failed${NC}"; tail -5 /tmp/kv_build.log; exit 1
fi

echo "=== CLI behavior (store: $STORE) ==="
KV="./kv --file=$STORE"

$KV set name ada
check "set + get" "ada" "$($KV get name)"

$KV set lang blang
check "list shows both" "name=ada
lang=blang" "$($KV list)"

$KV set name grace
check "set replaces in place" "grace" "$($KV get name)"
check "replace does not duplicate" "name=grace
lang=blang" "$($KV list)"

check "keys" "name
lang" "$($KV keys)"

$KV has name; check "has present -> exit 0" "0" "$?"
$KV has nope 2>/dev/null; check "has missing -> exit 1" "1" "$?"

$KV get nope >/dev/null 2>&1; check "get missing -> exit 1" "1" "$?"

$KV del lang; check "del present -> exit 0" "0" "$?"
check "del removed the key" "name=grace" "$($KV list)"
$KV del lang >/dev/null 2>&1; check "del missing -> exit 1" "1" "$?"

$KV set url "http://x?a=1&b=2"
check "value may contain equals" "http://x?a=1&b=2" "$($KV get url)"

./kv badcmd >/dev/null 2>&1; check "unknown command -> exit 2" "2" "$?"
./kv >/dev/null 2>&1; check "no args -> usage, exit 2" "2" "$?"

# KV_FILE environment fallback (no --file flag).
ENVSTORE="$(mktemp -u /tmp/kv_env_XXXX.store)"
KV_FILE="$ENVSTORE" ./kv set from env
check "KV_FILE env is honored" "env" "$(KV_FILE=$ENVSTORE ./kv get from)"
rm -f "$ENVSTORE"

echo "=== unit tests (bcc test) ==="
if "$BCC" test > /tmp/kv_unit.log 2>&1; then
	grep -E "passed" /tmp/kv_unit.log
	echo -e "  ${GREEN}PASS${NC}  bcc test green"; PASS=$((PASS + 1))
else
	tail -12 /tmp/kv_unit.log
	echo -e "  ${RED}FAIL${NC}  bcc test reported failures"; FAIL=$((FAIL + 1))
fi

echo
echo "Passed: $PASS  Failed: $FAIL"
[ "$FAIL" -eq 0 ]
