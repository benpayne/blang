#!/bin/bash
# Integration test for the chat example: build, then run a scripted
# server + two clients + quit lifecycle and check the broadcast and the
# server's account of it. Ports are randomized to tolerate reruns.
set -u
cd "$(dirname "$0")"

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
FAILS=0
check() {
	if eval "$2"; then echo -e "  ${GREEN}PASS${NC}  $1"
	else echo -e "  ${RED}FAIL${NC}  $1"; FAILS=$((FAILS+1)); fi
}

BCC=../../build/bcc
[ -x "$BCC" ] || { echo "bcc not built"; exit 1; }

$BCC main.b -o chat || { echo "build failed"; exit 1; }
check "chat builds" "[ -x ./chat ]"

PORT=$(( 9500 + RANDOM % 400 ))
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

./chat server $PORT > "$TMP/server.log" 2>&1 &
SRV=$!
sleep 0.8

# Listener client connects first and blocks reading one broadcast...
./chat client $PORT - 1 > "$TMP/listener.log" 2>&1 &
LISTENER=$!
sleep 0.5

# ...then the sender connects and speaks.
./chat client $PORT hello-from-sender 0 > "$TMP/sender.log" 2>&1
check "sender exits cleanly" "[ $? -eq 0 ]"

wait $LISTENER
check "listener exits cleanly" "[ $? -eq 0 ]"
check "listener received the broadcast" "grep -q hello-from-sender '$TMP/listener.log'"

sleep 0.5
./chat quit $PORT
wait $SRV
SRV_EXIT=$?
check "server shut down cleanly on /quit" "[ $SRV_EXIT -eq 0 ]"
check "server saw both clients join" "grep -q 'client joined (2 connected)' '$TMP/server.log'"
check "server relayed the line" "grep -q 'relayed: hello-from-sender' '$TMP/server.log'"
check "server saw clients leave" "grep -q 'client left' '$TMP/server.log'"
check "server reported its relay count" "grep -q 'relayed 1 lines' '$TMP/server.log'"

echo ""
if [ $FAILS -eq 0 ]; then
	echo -e "${GREEN}All chat checks passed${NC}"; exit 0
else
	echo -e "${RED}$FAILS chat check(s) failed${NC}"; exit 1
fi
