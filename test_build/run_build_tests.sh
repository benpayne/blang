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

# ---------------------------------------------------------------------------
# Cross-module construction ABI (modules-v2-exports U1)
#
# Before the library-emitted factory, a consumer of a .bmod could reach into an
# imported struct's fields but could neither construct it (`Counter(5)` did not
# parse) nor call anything on it (`no method 'bump'`) — design record P8. The
# consumer now calls __Counter_new, which the LIBRARY emits; it never computes
# Counter's size and never generates Counter's destructor.
# ---------------------------------------------------------------------------

( cd counterlib && rm -f libcounterlib.a counterlib.bmod && "$BCC" build > /dev/null 2>&1 )
check "counterlib builds (lib)" "[ -f counterlib/libcounterlib.a ] && [ -f counterlib/counterlib.bmod ]"
check "bmod ships non-generic init signature" \
	"grep -q 'init(int start, string name);' counterlib/counterlib.bmod"
check "bmod ships non-generic method signatures" \
	"grep -q 'fn bump(self) -> int;' counterlib/counterlib.bmod"
check "bmod method signatures are bodyless" \
	"! grep -q 'fn bump(self) -> int {' counterlib/counterlib.bmod"

( cd counterapp && rm -f counterapp && "$BCC" build > /dev/null 2>&1 )
check "counterapp builds (constructs + calls an imported struct)" "[ -x counterapp/counterapp ]"

COUT=$(cd counterapp && ./counterapp)
CEXPECTED='start = 5
bump = 6
bump = 7
label = hits
other = 100 other'
check "counterapp output exact" "[ \"\$COUT\" = \"\$CEXPECTED\" ]"

# The ABI invariants, read off the CONSUMER'S OWN IR. (Reading the linked
# binary would not distinguish these: the library's archive contributes
# __Counter_dtor to the final image, which is exactly as intended.)
XLL=$(mktemp -d)/consumer.ll
"$(cd .. && pwd)/build/qcc" --combine counterapp/main.b counterlib/counterlib.bmod \
	-o "$XLL" > /dev/null 2>&1
check "consumer DECLARES the factory (never defines it)" \
	"grep -q '^declare ptr @__Counter_new(' \"\$XLL\""
check "consumer DECLARES imported methods (no empty 'define ... ret 0')" \
	"grep -q '^declare i32 @Counter_bump(' \"\$XLL\""
check "consumer never generates the imported struct's destructor" \
	"! grep -q '@__Counter_dtor' \"\$XLL\""
check "consumer never allocates the imported struct itself" \
	"! grep -q 'call ptr @__blang_rc_alloc_dtor(i64 16' \"\$XLL\""
check "construction lowers to a factory call" \
	"grep -q 'call ptr @__Counter_new(' \"\$XLL\""

( cd timerapp && rm -f timerapp && "$BCC" build > /dev/null 2>&1 )
check "timerapp builds (stdlib import)" "[ -x timerapp/timerapp ]"

# --- Git dependency fetching -------------------------------------------------
# A throwaway local git repo stands in for a remote: lib project committed and
# tagged, consumer declares { git = "file://...", tag = "v1.0" }. Covers:
# fetch + build + run (generics included), warm-cache reuse (no re-fetch),
# and the unpinned-dep error.
GITTMP=$(mktemp -d)
trap 'rm -rf "$GITTMP"' EXIT
mkdir -p "$GITTMP/lib-src" "$GITTMP/app"
cat > "$GITTMP/lib-src/blang.toml" <<'TOML'
[project]
name = "greetlib"
version = "1.0.0"
type = "lib"
TOML
cat > "$GITTMP/lib-src/greet.b" <<'BLANG'
pub fn greeting(string name) -> string {
	return "hello, " + name + "!";
}

pub fn pick<T>(T a, T b, bool first) -> T {
	if first {
		return a;
	}
	return b;
}
BLANG
( cd "$GITTMP/lib-src" && git init -q && \
  git -c user.email=t@t -c user.name=t add -A && \
  git -c user.email=t@t -c user.name=t commit -qm v1 && git tag v1.0 )
cat > "$GITTMP/app/blang.toml" <<TOML
[project]
name = "gitapp"
version = "0.1.0"
type = "bin"

[deps]
greetlib = { git = "file://$GITTMP/lib-src", tag = "v1.0" }
TOML
cat > "$GITTMP/app/main.b" <<'BLANG'
import greetlib;

fn main() -> int {
	println("{}", greeting("git"));
	println("{}", pick("left", "right", true));
	println("{}", pick(10, 20, false));
	return 0;
}
BLANG

FETCH_LOG=$( cd "$GITTMP/app" && "$BCC" build -v 2>&1 )
check "git dep fetched on first build" "echo \"\$FETCH_LOG\" | grep -q 'fetching git dep'"
check "git-dep app builds" "[ -x \"$GITTMP/app/gitapp\" ]"
GITOUT=$(cd "$GITTMP/app" && ./gitapp)
GITEXPECTED='hello, git!
left
20'
check "git-dep app output exact (incl. generics from the dep)" "[ \"\$GITOUT\" = \"\$GITEXPECTED\" ]"

REBUILD_LOG=$( cd "$GITTMP/app" && rm -f gitapp && "$BCC" build -v 2>&1 )
check "warm checkout reused (no re-fetch)" \
	"echo \"\$REBUILD_LOG\" | grep -q 'cached at' && ! echo \"\$REBUILD_LOG\" | grep -q 'fetching git dep'"

sed 's/, tag = "v1.0"//' "$GITTMP/app/blang.toml" > "$GITTMP/app/blang.toml.tmp" \
	&& mv "$GITTMP/app/blang.toml.tmp" "$GITTMP/app/blang.toml"
UNPINNED_LOG=$( cd "$GITTMP/app" && "$BCC" build 2>&1 )
UNPINNED_EXIT=$?
check "unpinned git dep is a hard error" \
	"[ $UNPINNED_EXIT -ne 0 ] && echo \"\$UNPINNED_LOG\" | grep -q 'must pin a version'"

# Security regression: a git URL is passed to git verbatim, never a shell, so
# shell metacharacters in a (possibly transitive) dependency's URL cannot
# execute. bcc runs its subprocesses via fork+execvp with no /bin/sh. The
# clone is expected to FAIL (bogus host) — the point is that the $(...) payload
# leaves no side effect. The marker path is process-unique so the check is
# independent of any prior run.
INJ_MARKER="/tmp/bcc_inj_$$_$RANDOM"
rm -f "$INJ_MARKER"
INJTMP=$( mktemp -d )
cat > "$INJTMP/blang.toml" <<EOF
[project]
name = "injapp"
version = "0.1.0"
type = "bin"

[deps]
evil = { git = "https://example.invalid/\$(touch $INJ_MARKER).git", tag = "v1" }
EOF
echo 'fn main() -> int { return 0; }' > "$INJTMP/main.b"
( cd "$INJTMP" && "$BCC" build >/dev/null 2>&1 )
check "git URL is not shell-evaluated (no command injection)" \
	"[ ! -e '$INJ_MARKER' ]"
rm -f "$INJ_MARKER"; rm -rf "$INJTMP"

echo ""
if [ $FAILS -eq 0 ]; then echo -e "${GREEN}All build-system checks passed${NC}"; exit 0
else echo -e "${RED}$FAILS check(s) failed${NC}"; exit 1; fi
