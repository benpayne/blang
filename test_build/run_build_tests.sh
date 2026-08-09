#!/bin/bash
# Build-system integration test: library -> .a + .bmod -> consumer binary.
# Exercises cross-module GENERICS end to end: the .bmod ships generic bodies,
# the consumer monomorphizes them (int/double/string + a generic struct with
# methods), and linkonce_odr dedups the instantiation the library itself uses.
set -u
cd "$(dirname "$0")"
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'; NC='\033[0m'
FAILS=0
check() {
	if eval "$2"; then echo -e "  ${GREEN}PASS${NC}  $1"
	else echo -e "  ${RED}FAIL${NC}  $1"; FAILS=$((FAILS+1)); fi
}

# EPIC-WIDE STANDING CHECK (modules-v2-exports, from U2 onward).
#
# Every .bmod any fixture produces must parse STANDALONE. Three P9-class breaks
# in this epic were all "a library whose interface no consumer can read", and
# each was invisible until someone re-parsed the file:
#   - `table pub struct` (emitted in the inverse of source order)
#   - a conformance record naming a user-defined protocol, emitted before the
#     protocol it names (a forward reference)
#   - a record naming a non-exported protocol (a dangling reference)
# A per-unit reminder would not have caught them; this runs over every library
# the script builds, so U3-U5 inherit it without restating it.
bmod_parses() {
	# $1 = path to a .bmod
	check "[$1] re-parses standalone (epic standing check)" \
		"\"$QCC\" --parse-only \"$1\" > /dev/null 2>&1"
}

BCC=../build/bcc
[ -x "$BCC" ] || { echo "bcc not built"; exit 1; }
BCC="$(cd .. && pwd)/build/bcc"
QCC="$(cd .. && pwd)/build/qcc"

# Fresh build: no stale cache artifacts.
"$BCC" clean > /dev/null 2>&1

( cd mathlib && rm -f libmathlib.a mathlib.bmod && "$BCC" build > /dev/null 2>&1 )
check "mathlib builds (lib)" "[ -f mathlib/libmathlib.a ] && [ -f mathlib/mathlib.bmod ]"
bmod_parses mathlib/mathlib.bmod
check "bmod ships generic fn body" "grep -q 'pub fn largest<T>(T a, T b) -> T {' mathlib/mathlib.bmod"
check "bmod ships generic struct impl" "grep -q 'impl Pair {' mathlib/mathlib.bmod"
check "bmod keeps non-generics signature-only" "grep -q 'pub fn add(int a, int b) -> int;' mathlib/mathlib.bmod"
# A GENERIC struct's conformance record (M-1). Without this the "+ conformances"
# claim on the golden check below guarded a file containing none.
check "bmod ships a generic struct's conformance record" \
	"grep -q '^impl Summable for Pair {' mathlib/mathlib.bmod"
check "generic conformance names a protocol declared earlier in the file" \
	"[ \"\$(grep -n 'pub protocol Summable' mathlib/mathlib.bmod | cut -d: -f1)\" -lt \"\$(grep -n 'impl Summable for Pair' mathlib/mathlib.bmod | cut -d: -f1)\" ]"

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
bmod_parses counterlib/counterlib.bmod
check "bmod ships non-generic init signature (with its pub marker, format 3)" \
	"grep -q '^	pub init(int start, string name);' counterlib/counterlib.bmod"
check "bmod ships non-generic method signatures (pub-marked)" \
	"grep -q '^	pub fn bump(self) -> int;' counterlib/counterlib.bmod"
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
XTMP=$(mktemp -d)
XLL="$XTMP/consumer.ll"
"$(cd .. && pwd)/build/qcc" --combine counterapp/main.b counterlib/counterlib.bmod \
	-o "$XLL" > /dev/null 2>&1
check "consumer DECLARES the factory (never defines it)" \
	"grep -q '^declare ptr @__Counter_new(' \"\$XLL\""
check "consumer DECLARES imported methods (no empty 'define ... ret 0')" \
	"grep -q '^declare i32 @Counter_bump(' \"\$XLL\""
check "consumer never generates the imported struct's destructor" \
	"! grep -q '@__Counter_dtor' \"\$XLL\""
check "consumer never allocates the imported struct itself" \
	"! grep -q 'call ptr @__blang_rc_alloc_dtor' \"\$XLL\""
check "construction lowers to a factory call" \
	"grep -q 'call ptr @__Counter_new(' \"\$XLL\""

# Leak/ASan leg. This fixture is the language's first program whose destructor
# is installed by one module (counterlib, via the factory) and invoked by
# another (counterapp, via __blang_rc_release reading the allocation header).
# Counter.label is a refcounted string, so a broken hand-off leaks or
# double-frees. Requires ASan-instrumented runtime archives:
#   cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined && make -C build-asan
# Skips loudly (yellow) rather than silently passing when they are absent —
# EXCEPT under CI, where a skip is a provisioning regression and must be a hard
# failure. Otherwise the job would print SKIP forever while reporting green,
# which is exactly how this leg would stop protecting anything.
asan_unavailable() {
	if [ "${CI:-}" = "true" ]; then
		echo -e "  ${RED}FAIL${NC}  cross-module ASan leg unavailable under CI ($1)"
		echo "        CI must provision the sanitizer archives:"
		echo "          cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined"
		echo "          cmake --build build-asan --parallel"
		FAILS=$((FAILS+1))
	else
		echo -e "  ${YELLOW}SKIP${NC}  cross-module ASan leg ($1)"
	fi
}

ASAN_DIR="${ASAN_BUILD_DIR:-$(cd .. && pwd)/build-asan}"
LLC="$(command -v llc-18 || command -v llc || true)"
if [ -d "$ASAN_DIR" ] && ls "$ASAN_DIR"/libblang_*.a > /dev/null 2>&1 && [ -n "$LLC" ]; then
	# No EXIT trap here: the git-dep leg below installs its own, which would
	# replace ours. Cleaned up explicitly at the end of this block instead.
	ATMP=$(mktemp -d)
	"$QCC" --combine counterapp/main.b counterlib/counterlib.bmod \
		-o "$ATMP/app.ll" > /dev/null 2>&1
	"$QCC" counterlib/counter.b -o "$ATMP/lib.ll" > /dev/null 2>&1
	"$LLC" -filetype=obj -relocation-model=pic "$ATMP/app.ll" -o "$ATMP/app.o" > /dev/null 2>&1
	"$LLC" -filetype=obj -relocation-model=pic "$ATMP/lib.ll" -o "$ATMP/lib.o" > /dev/null 2>&1
	# -luv: the runtime archive references libuv (async event loop). Without it the
	# link fails (undefined uv_run/uv_default_loop/uv_queue_work) and this leg
	# SKIPs -- a HARD FAILURE under CI=true (rev finding B).
	cc -fsanitize=address,undefined -o "$ATMP/counterapp_asan" "$ATMP/app.o" "$ATMP/lib.o" \
		-Wl,--start-group "$ASAN_DIR"/libblang_*.a -Wl,--end-group \
		-lpthread -lm -luv > /dev/null 2>&1
	if [ -x "$ATMP/counterapp_asan" ]; then
		# Self-proof: a negative grep for sanitizer output passes trivially on an
		# UNINSTRUMENTED binary, so assert the instrumentation is actually there
		# before trusting the clean report.
		check "ASan leg is actually instrumented (__asan_init present)" \
			"nm \"$ATMP/counterapp_asan\" | grep -q '__asan_init'"
		ASAN_OUT=$(ASAN_OPTIONS=detect_leaks=1 "$ATMP/counterapp_asan" 2>&1)
		ASAN_EXIT=$?
		check "cross-module destructor is ASan/LSan clean" \
			"[ $ASAN_EXIT -eq 0 ] && ! echo \"\$ASAN_OUT\" | grep -q 'LeakSanitizer\|AddressSanitizer\|runtime error'"
		check "ASan build still produces correct output" \
			"[ \"\$ASAN_OUT\" = \"\$CEXPECTED\" ]"
	else
		asan_unavailable "link failed"
	fi
	rm -rf "$ATMP"
else
	asan_unavailable "no $ASAN_DIR archives or no llc"
fi
rm -rf "$XTMP"

# ---------------------------------------------------------------------------
# The .bmod as a true interface (modules-v2-exports U2)
#
# Covers: protocol conformance records crossing the boundary (D16), the
# `pub table struct` emission order, the format-version marker, and golden
# .bmod content.
# ---------------------------------------------------------------------------

( cd printlib && rm -f libprintlib.a printlib.bmod && "$BCC" build > /dev/null 2>&1 )
check "printlib builds (table struct + Printable conformance)" \
	"[ -f printlib/libprintlib.a ] && [ -f printlib/printlib.bmod ]"
bmod_parses printlib/printlib.bmod

# The emitted order must be `pub table struct`, matching source. The inverse
# (`table pub struct`, emitted before U2) is a HARD PARSE ERROR --
# "Expected 'struct' after 'table'" -- so a table struct's interface was
# unreadable by any consumer.
check "bmod emits 'pub table struct' (round-trips)" \
	"grep -q '^pub table struct Point {' printlib/printlib.bmod"
check "bmod does NOT emit the unparseable 'table pub struct'" \
	"! grep -q 'table pub struct' printlib/printlib.bmod"
check "bmod carries the format-version marker" \
	"grep -q '^// blang-bmod-format: [0-9]' printlib/printlib.bmod"
check "bmod carries the protocol conformance record" \
	"grep -q '^impl Printable for Point {' printlib/printlib.bmod"
check "bmod does not duplicate a protocol-satisfying method" \
	"[ \"\$(grep -c 'fn to_string(self) -> string;' printlib/printlib.bmod)\" = '1' ]"

# Emission must be deterministic, or goldens are noise.
"$QCC" --emit-bmod /tmp/bmod_det_a.bmod printlib/shape.b > /dev/null 2>&1
"$QCC" --emit-bmod /tmp/bmod_det_b.bmod printlib/shape.b > /dev/null 2>&1
check "bmod emission is deterministic" \
	"cmp -s /tmp/bmod_det_a.bmod /tmp/bmod_det_b.bmod"
rm -f /tmp/bmod_det_a.bmod /tmp/bmod_det_b.bmod

# Golden .bmod: the format is the interface, so a format change must be visible
# in review rather than inferred from greps. Regenerate with --update-goldens.
GOLDEN_DIR="../test_files/golden/bmod"
if [ "${UPDATE_GOLDENS:-0}" = "1" ]; then
	mkdir -p "$GOLDEN_DIR"
	cp printlib/printlib.bmod "$GOLDEN_DIR/printlib.bmod"
	cp counterlib/counterlib.bmod "$GOLDEN_DIR/counterlib.bmod"
	cp mathlib/mathlib.bmod "$GOLDEN_DIR/mathlib.bmod"
	echo "  updated .bmod goldens"
fi
check "printlib.bmod matches its golden" \
	"diff -u \"$GOLDEN_DIR/printlib.bmod\" printlib/printlib.bmod"
check "counterlib.bmod matches its golden" \
	"diff -u \"$GOLDEN_DIR/counterlib.bmod\" counterlib/counterlib.bmod"
check "mathlib.bmod matches its golden (generic bodies + generic conformance record)" \
	"diff -u \"$GOLDEN_DIR/mathlib.bmod\" mathlib/mathlib.bmod"

( cd printapp && rm -f printapp && "$BCC" build > /dev/null 2>&1 )
check "printapp builds (imports a table struct + Printable type)" "[ -x printapp/printapp ]"

POUT=$(cd printapp && ./printapp)
PEXPECTED='Point(3, 4)
sum = 7'
# print("{}", x) on an IMPORTED Printable type -- only works because the .bmod
# carries the conformance record (D16). Epic done-condition 5.
check "imported Printable dispatches through print (DC5)" \
	"[ \"\$POUT\" = \"\$PEXPECTED\" ]"

# NEGATIVE leg: the conformance record must be LOAD-BEARING, not decorative.
# Strip `impl Printable for Point { }` from the interface and the consumer must
# refuse to compile, with a located diagnostic naming the missing conformance.
# Without this, dispatch could keep working off a "does it have a method called
# to_string" scan and the record would prove nothing -- and that scan breaks
# outright once U3's `pub` filter removes non-public methods from the interface.
NTMP=$(mktemp -d)
cp printlib/printlib.bmod "$NTMP/stripped.bmod"
python3 - "$NTMP/stripped.bmod" <<'PYEOF'
import sys, re
p = sys.argv[1]
s = open(p).read()
open(p, 'w').write(re.sub(r'impl Printable for Point \{\n\}\n', '', s))
PYEOF
check "stripping the conformance record removes it from the .bmod" \
	"! grep -q 'impl Printable for Point' \"$NTMP/stripped.bmod\""
NEG_OUT=$("$QCC" --combine printapp/main.b "$NTMP/stripped.bmod" -o "$NTMP/neg.ll" 2>&1 >/dev/null)
NEG_EXIT=$?
check "consumer REJECTS an imported Printable with no conformance record" \
	"[ $NEG_EXIT -ne 0 ]"
check "the rejection is a located diagnostic naming the conformance" \
	"echo \"\$NEG_OUT\" | grep -Eq '^[^:]+\.b:[0-9]+:[0-9]+: error: .*not printable.*impl Printable for Point'"
rm -rf "$NTMP"

# ---------------------------------------------------------------------------
# D15 data-contract metadata across a .bmod boundary (modules-v2-exports U5)
#
# An imported @json table struct is QUERYABLE and SERIALIZABLE from a consumer
# module: its field SHAPE crosses the .bmod as compiler-facing metadata, so
# `query T |> where { .field }` and `to_json(x)` compile, link, and run against
# it. The reverse direction — naming a field in ordinary source is a located
# error — is fail/xmodule/imported_datacontract_field (run by run_tests.sh).
# Together they are D15 both ways. Epic done-condition 6.
# ---------------------------------------------------------------------------
( cd todolib && rm -f libtodolib.a todolib.bmod && "$BCC" build > /dev/null 2>&1 )
check "todolib builds (@json table struct)" \
	"[ -f todolib/libtodolib.a ] && [ -f todolib/todolib.bmod ]"
bmod_parses todolib/todolib.bmod
# A data-contract struct KEEPS its field declarations in the .bmod (its shape IS
# its contract, D15) — unlike a plain struct, whose fields are dropped (format 4).
check "data-contract .bmod keeps field metadata (D15)" \
	"grep -q '	int id;' todolib/todolib.bmod && grep -q '	string title;' todolib/todolib.bmod"

( cd todoapp && rm -f todoapp todoapp.db && rm -rf .blang && "$BCC" build > /dev/null 2>&1 )
check "todoapp builds (queries + to_json an imported @json table struct)" \
	"[ -x todoapp/todoapp ]"
# Create the schema from the table struct's canonical definition (it lives in the
# lib): `bcc migrate` extracts a table struct from any source passed to it. The
# imported struct's own field shape is what defines the table.
( cd todoapp && rm -f todoapp.db && "$BCC" migrate --apply ../todolib/todo.b > /dev/null 2>&1 )
TOUT=$(cd todoapp && ./todoapp)
TEXPECTED='{"id":1,"title":"buy milk","done":false}
{"id":2,"title":"walk dog","done":true}
first done: {"id":2,"title":"walk dog","done":true}'
check "imported @json table struct: query .field + to_json run cross-module (DC6)" \
	"[ \"\$TOUT\" = \"\$TEXPECTED\" ]"
( cd todoapp && rm -f todoapp.db && rm -rf .blang )

# A conformance record naming a USER-DEFINED protocol. Every other fixture
# conforms only to `Printable` — a builtin pre-registered in every scope, so its
# record resolved wherever it appeared. A user-defined protocol does not, and
# emitting structs before protocols made every such record a forward reference
# that the consumer's parser rejected ("Unknown protocol 'Sizeable' in impl
# block") -- an interface no consumer could read, the same defect class as the
# `table pub struct` break. Without this fixture the whole class is untested.
( cd sizelib && rm -f libsizelib.a sizelib.bmod && "$BCC" build > /dev/null 2>&1 )
check "sizelib builds (user-defined pub protocol)" \
	"[ -f sizelib/libsizelib.a ] && [ -f sizelib/sizelib.bmod ]"
check "protocol is declared BEFORE the conformance record that names it" \
	"[ \"\$(grep -n 'pub protocol Sizeable' sizelib/sizelib.bmod | cut -d: -f1)\" -lt \"\$(grep -n 'impl Sizeable for Box' sizelib/sizelib.bmod | cut -d: -f1)\" ]"
# The non-exported-protocol conformance this fixture used to carry is now
# REJECTED at the library build by P9 (U3), so the emitter's skip is unreachable
# from valid source; the rejection is covered by
# test_files/fail/sema/p9_private_protocol_conformance.b.
check "only the declared conformances appear (Sizeable + Labelled, no stray)" \
	"[ \"\$(grep -c '^impl .* for Box' sizelib/sizelib.bmod)\" = '2' ]"
bmod_parses sizelib/sizelib.bmod

( cd sizeapp && rm -f sizeapp && "$BCC" build > /dev/null 2>&1 )
check "sizeapp builds against a user-defined-protocol interface" "[ -x sizeapp/sizeapp ]"
SOUT=$(cd sizeapp && ./sizeapp)
SEXPECTED='size = 11
label = box'
check "sizeapp output exact" "[ \"\$SOUT\" = \"\$SEXPECTED\" ]"

# F-1 NEGATIVE LEG: a visibility filter must be proven by ABSENCE, not only by
# what survives it. `secret` is a real method on Box, reachable only through the
# non-`pub` protocol `Hidden`; it must not appear in the interface at all.
check "non-pub method is ABSENT from the .bmod (pub filter)" \
	"! grep -q 'secret' sizelib/sizelib.bmod"
check "pub method IS present (filter is not blanket suppression)" \
	"grep -q 'pub fn size(self) -> int;' sizelib/sizelib.bmod"
check "pub init is emitted with its pub marker (format 3)" \
	"grep -q 'pub init(int v);' sizelib/sizelib.bmod"
check "a second user-defined conformance record crosses the boundary" \
	"grep -q '^impl Labelled for Box {' sizelib/sizelib.bmod"
check "its pub backing method is in the interface" \
	"grep -q 'pub fn label(self) -> string;' sizelib/sizelib.bmod"

# FORMAT-VERSION VALIDATION (U3). The marker must be READ, not merely written:
# a format-2 .bmod read by a format-3 compiler would silently invert the meaning
# of every unmarked `init` (exported in 2, private in 3). Take a good interface,
# rewind its version, and require a located rejection naming the fix.
FTMP=$(mktemp -d)
sed 's/blang-bmod-format: [0-9][0-9]*/blang-bmod-format: 2/' \
	sizelib/sizelib.bmod > "$FTMP/stale.bmod"
check "a stale-format .bmod is not silently accepted" \
	"! \"$QCC\" --parse-only \"$FTMP/stale.bmod\" > /dev/null 2>&1"
FOUT=$("$QCC" --parse-only "$FTMP/stale.bmod" 2>&1 >/dev/null)
check "the stale-format rejection is located and names the fix" \
	"echo \"\$FOUT\" | grep -Eq '^[^:]+\.bmod:[0-9]+:[0-9]+: error: .*format 2.*expects 4.*rebuild'"
rm -rf "$FTMP"

# Prefix-aware factory mangling (known-issues KI-5 action 2). The factory symbol
# carries the defining module's codegen prefix, mirroring method mangling, so two
# namespaced modules that both define a `Socket` get distinct factories instead
# of silently sharing one symbol. No stdlib module has an init-bearing struct
# today, so without this the branch is dead code — and an untested branch is not
# a working branch.
#
# Under --combine every non-user module gets a prefix, which is exactly the
# condition the branch keys on.
PTMP=$(mktemp -d)
cat > "$PTMP/alpha.b" <<'BLANG'
pub struct Widget {
	int n;
}
impl Widget {
	init(int v) { self.n = v; }
	fn get(self) -> int { return self.n; }
}
BLANG
cat > "$PTMP/beta.b" <<'BLANG'
pub struct Widget {
	int m;
}
impl Widget {
	init(int v) { self.m = v * 2; }
	fn get(self) -> int { return self.m; }
}
BLANG
cat > "$PTMP/user.b" <<'BLANG'
fn main() -> int { return 0; }
BLANG
"$QCC" --combine "$PTMP/alpha.b" "$PTMP/beta.b" "$PTMP/user.b" \
	-o "$PTMP/pfx.ll" > /dev/null 2>&1
check "factory symbol carries the defining module's prefix" \
	"grep -q '^define ptr @__alpha__Widget_new(' \"$PTMP/pfx.ll\""
check "two same-named structs get DISTINCT factory symbols" \
	"grep -q '^define ptr @__beta__Widget_new(' \"$PTMP/pfx.ll\""
check "no unprefixed factory leaks out of a namespaced module" \
	"! grep -q '^define ptr @__Widget_new(' \"$PTMP/pfx.ll\""
check "each prefixed factory calls its OWN module's init" \
	"sed -n '/define ptr @__beta__Widget_new/,/^}/p' \"$PTMP/pfx.ll\" | grep -q 'call void @beta__Widget_init'"
rm -rf "$PTMP"

# Build-cache format-version salt (REQ-009, done-condition 7) is proven by the
# dedicated unit test `build_cache_key` (ctest), which calls the real
# BuildCache::computeKey with two format versions and asserts the keys differ --
# and that the default is the shipped constant. It lives there rather than here
# because it needs no toolchain and runs in milliseconds.

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
