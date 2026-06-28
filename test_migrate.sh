#!/bin/bash
# Integration test for `bcc migrate` (tasks 162/166/167/202).
# Exercises CREATE TABLE, ADD COLUMN, the destructive-change guard, and the
# stored-schema snapshot, end to end against a real SQLite database.
#
# Requires: bcc built with LLVM + SQLite (build/bcc).

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BCC="${SCRIPT_DIR}/build/bcc"

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
PASS=0; FAIL=0

check() {  # check "name" expected_exit actual_exit
	if [ "$2" -eq "$3" ]; then
		echo -e "  ${GREEN}PASS${NC}  $1"
		PASS=$((PASS + 1))
	else
		echo -e "  ${RED}FAIL${NC}  $1 (expected exit $2, got $3)"
		FAIL=$((FAIL + 1))
	fi
}

check_contains() {  # check_contains "name" "needle" "haystack"
	if echo "$3" | grep -qF "$2"; then
		echo -e "  ${GREEN}PASS${NC}  $1"
		PASS=$((PASS + 1))
	else
		echo -e "  ${RED}FAIL${NC}  $1 (missing: $2)"
		FAIL=$((FAIL + 1))
	fi
}

if [ ! -x "$BCC" ]; then
	echo -e "${RED}Error: bcc not found at $BCC${NC}"
	exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK" || exit 1

cat > blang.toml <<EOF
[project]
name = "migtest"
version = "0.1.0"
type = "bin"

[database]
driver = "sqlite"
url = "migtest.db"
EOF

echo "=== bcc migrate integration tests ==="

# 1. Initial schema: CREATE TABLE
cat > app.b <<'EOF'
table struct User {
	int id;
	string name;
}
fn main() -> int { return 0; }
EOF

out="$($BCC migrate --preview app.b)"
check_contains "preview shows CREATE TABLE" "Create table User" "$out"

out="$($BCC migrate --generate app.b)"
check_contains "generate emits SQL" "CREATE TABLE IF NOT EXISTS user" "$out"

$BCC migrate --apply app.b > /dev/null; check "apply create table" 0 $?

out="$($BCC migrate --preview app.b)"
check_contains "re-preview is clean" "No schema changes" "$out"

# 2. ADD COLUMN
cat > app.b <<'EOF'
table struct User {
	int id;
	string name;
	string email;
}
fn main() -> int { return 0; }
EOF
out="$($BCC migrate --preview app.b)"
check_contains "preview shows ADD COLUMN" "Add column email" "$out"
$BCC migrate --apply app.b > /dev/null; check "apply add column" 0 $?

# 3. Destructive change guard
cat > app.b <<'EOF'
table struct User {
	int id;
	string name;
}
fn main() -> int { return 0; }
EOF
$BCC migrate --apply app.b > /dev/null 2>&1
check "destructive apply refused without flag" 1 $?
$BCC migrate --apply --allow-destructive app.b > /dev/null 2>&1
check "destructive apply allowed with flag" 0 $?

echo ""
echo "Passed: $PASS  Failed: $FAIL"
[ "$FAIL" -eq 0 ]
