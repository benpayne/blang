#!/bin/bash
# End-to-end integration test for the BLang todo app: builds it with bcc, starts
# the server, drives the REST API with curl, and asserts the responses.
#
# Exercises the data layer in a real app: table struct -> SQLite, insert/update/
# delete with bound params, query Todo -> Array<Todo>, @json serialization, and
# the HTTP stdlib. Requires bcc built with LLVM + SQLite.

set -u
APP_DIR="$(cd "$(dirname "$0")" && pwd)"
BCC="${APP_DIR}/../../build/bcc"
PORT=8080
BASE="http://localhost:${PORT}"

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
PASS=0; FAIL=0
check() {  # check "name" expected actual
	if [ "$2" = "$3" ]; then
		echo -e "  ${GREEN}PASS${NC}  $1"
		PASS=$((PASS + 1))
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
rm -f todos.db

echo "=== building todo_app ==="
if ! "$BCC" build > /tmp/todo_build.log 2>&1; then
	echo -e "${RED}build failed${NC}"; tail -5 /tmp/todo_build.log; exit 1
fi

echo "=== starting server ==="
./todo_app > /tmp/todo_run.log 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null; rm -f todos.db' EXIT

# Wait for the server to accept connections (up to ~5s).
for _ in $(seq 1 50); do
	if curl -s -o /dev/null "${BASE}/todos" 2>/dev/null; then break; fi
	sleep 0.1
done

echo "=== REST CRUD ==="
check "GET /todos empty" "[]" "$(curl -s ${BASE}/todos)"

r=$(curl -s -X POST ${BASE}/todos -H 'Content-Type: application/json' -d '{"title":"buy milk"}')
check "POST creates todo #1" '[{"id":1,"title":"buy milk","done":false}]' "$r"

r=$(curl -s -X POST ${BASE}/todos -H 'Content-Type: application/json' -d '{"title":"walk dog"}')
check "POST creates todo #2" '[{"id":1,"title":"buy milk","done":false},{"id":2,"title":"walk dog","done":false}]' "$r"

r=$(curl -s -X PUT ${BASE}/todos/1 -H 'Content-Type: application/json' -d '{"done":true}')
check "PUT toggles #1 done" '[{"id":1,"title":"buy milk","done":true},{"id":2,"title":"walk dog","done":false}]' "$r"

r=$(curl -s -X DELETE ${BASE}/todos/2)
check "DELETE removes #2" '[{"id":1,"title":"buy milk","done":true}]' "$r"

code=$(curl -s -o /dev/null -w "%{http_code}" ${BASE}/nope)
check "unknown route -> 404" "404" "$code"

code=$(curl -s -o /dev/null -w "%{http_code}" ${BASE}/)
check "GET / serves frontend (200)" "200" "$code"

# Persistence: restart and confirm the surviving todo loads from SQLite.
kill $SRV 2>/dev/null; wait $SRV 2>/dev/null
./todo_app > /tmp/todo_run2.log 2>&1 &
SRV=$!
for _ in $(seq 1 50); do
	if curl -s -o /dev/null "${BASE}/todos" 2>/dev/null; then break; fi
	sleep 0.1
done
check "data persists across restart" '[{"id":1,"title":"buy milk","done":true}]' "$(curl -s ${BASE}/todos)"

echo ""
echo "Passed: $PASS  Failed: $FAIL"
[ "$FAIL" -eq 0 ]
