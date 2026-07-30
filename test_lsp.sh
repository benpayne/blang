#!/bin/bash
#
# BLang LSP Golden-Transcript Test Suite
#
# For each fixture test_files/lsp/<name>.lsp.jsonl, drives blangd through the
# scripted conversation (tools/lsp_client.py) and compares the canonical
# transcript against the committed golden test_files/lsp/<name>.expected.out
# (exact match; the only normalization is stripping one trailing newline).
# The driver itself hard-fails on any output byte outside a well-formed
# Content-Length frame or on any stderr output — the stdout-pollution teeth.
#
# Usage: ./test_lsp.sh [--verbose] [--update-goldens] [--selfcheck] [fixture...]
#        BUILD_DIR=path ./test_lsp.sh    # use a different build directory

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
BLANGD="$BUILD_DIR/blangd"
DRIVER="$SCRIPT_DIR/tools/lsp_client.py"
LSP_DIR="$SCRIPT_DIR/test_files/lsp"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

VERBOSE=0
UPDATE_GOLDENS=0
SELFCHECK=0
FILE_ARGS=()

for arg in "$@"; do
	case "$arg" in
		--verbose) VERBOSE=1 ;;
		--update-goldens) UPDATE_GOLDENS=1 ;;
		--selfcheck) SELFCHECK=1 ;;
		--help)
			echo "Usage: $0 [--verbose] [--update-goldens] [--selfcheck] [fixture...]"
			echo "  --verbose         Show the transcript diff on failure (full)"
			echo "  --update-goldens  Regenerate transcript goldens"
			echo "  --selfcheck       Prove the comparator has teeth: corrupt a TEMP COPY of a"
			echo "                    real golden, require the suite to go red, print"
			echo "                    'SELFCHECK: OK', and exit non-zero. Never mutates a"
			echo "                    committed golden."
			exit 0
			;;
		*) FILE_ARGS+=("$arg") ;;
	esac
done

if [ ! -x "$BLANGD" ]; then
	echo -e "${RED}Error: blangd not found at $BLANGD${NC}"
	echo "Build it first: cmake --build $BUILD_DIR --target blangd"
	exit 1
fi

PASS_COUNT=0
FAIL_COUNT=0
NOGOLDEN_COUNT=0
TOTAL=0

# Corrupted-golden path injected by --selfcheck (never used in normal runs).
GOLDEN_OVERRIDE=""

strip_one_trailing_nl() {
	# Print file content with AT MOST one trailing newline removed.
	awk 'NR>1{print prev} {prev=$0} END{if(NR>0)printf "%s",prev}' "$1"
}

run_one_fixture() {
	local fixture="$1"
	local name
	name="$(basename "$fixture" .lsp.jsonl)"
	TOTAL=$((TOTAL + 1))

	local actual
	actual="$(mktemp)"
	if ! python3 "$DRIVER" --server "$BLANGD" --root "$SCRIPT_DIR" \
			--script "$fixture" > "$actual" 2> "${actual}.err"; then
		echo -e "  ${RED}FAIL${NC}  $name  (driver failed)"
		sed 's/^/    /' "${actual}.err" | head -10
		FAIL_COUNT=$((FAIL_COUNT + 1))
		rm -f "$actual" "${actual}.err"
		return 1
	fi
	rm -f "${actual}.err"

	local golden="${GOLDEN_OVERRIDE:-$LSP_DIR/${name}.expected.out}"

	if [ "$UPDATE_GOLDENS" -eq 1 ]; then
		cp "$actual" "$LSP_DIR/${name}.expected.out"
		echo -e "  ${CYAN}WROTE${NC} $name  (golden updated)"
		PASS_COUNT=$((PASS_COUNT + 1))
		rm -f "$actual"
		return 0
	fi

	if [ ! -f "$golden" ]; then
		echo -e "  ${YELLOW}NO GOLDEN${NC}  $name  (run --update-goldens to create)"
		NOGOLDEN_COUNT=$((NOGOLDEN_COUNT + 1))
		PASS_COUNT=$((PASS_COUNT + 1))
		rm -f "$actual"
		return 0
	fi

	local na ng
	na="$(mktemp)"; ng="$(mktemp)"
	strip_one_trailing_nl "$golden" > "$ng"
	strip_one_trailing_nl "$actual" > "$na"
	if cmp -s "$ng" "$na"; then
		echo -e "  ${GREEN}PASS${NC}  $name"
		PASS_COUNT=$((PASS_COUNT + 1))
		rm -f "$actual" "$na" "$ng"
		return 0
	fi

	echo -e "  ${RED}FAIL${NC}  $name  (transcript does not match golden)"
	echo "    --- diff (- expected golden / + actual transcript) ---"
	local limit=30
	[ "$VERBOSE" -eq 1 ] && limit=100000
	diff -u "$ng" "$na" 2>/dev/null | tail -n +3 | head -$limit | sed 's/^/    /'
	FAIL_COUNT=$((FAIL_COUNT + 1))
	rm -f "$actual" "$na" "$ng"
	return 1
}

# --selfcheck: prove the comparator has TEETH. Phase A: a real committed
# golden must PASS against its own fixture. Phase B: a corrupted TEMP COPY of
# that golden must make the same fixture FAIL. The committed golden's sha256
# must be unchanged. Success prints 'SELFCHECK: OK' and exits non-zero (the
# suite went red by design), mirroring test_codegen.sh.
run_selfcheck() {
	echo "==========================================="
	echo " LSP golden self-check (teeth proof)"
	echo "==========================================="
	local golden_file=""
	local gf
	for gf in $(ls "$LSP_DIR"/*.expected.out 2>/dev/null | sort); do
		if [ -f "$LSP_DIR/$(basename "$gf" .expected.out).lsp.jsonl" ]; then
			golden_file="$gf"; break
		fi
	done
	if [ -z "$golden_file" ]; then
		echo "SELFCHECK: FAILED — no committed golden found to corrupt"
		exit 3
	fi
	local name fixture
	name="$(basename "$golden_file" .expected.out)"
	fixture="$LSP_DIR/${name}.lsp.jsonl"
	echo "  Using golden: test_files/lsp/${name}.expected.out"

	local before_sum after_sum
	before_sum="$(sha256sum "$golden_file" | awk '{print $1}')"

	PASS_COUNT=0; FAIL_COUNT=0; TOTAL=0; GOLDEN_OVERRIDE=""
	run_one_fixture "$fixture" >/dev/null 2>&1
	local real_ret=$?

	local tmp_golden
	tmp_golden="$(mktemp)"
	cat "$golden_file" > "$tmp_golden"
	printf '\nSELFCHECK-CORRUPTION-%s\n' "$$" >> "$tmp_golden"
	PASS_COUNT=0; FAIL_COUNT=0; TOTAL=0
	GOLDEN_OVERRIDE="$tmp_golden"
	run_one_fixture "$fixture" >/dev/null 2>&1
	local corrupt_ret=$?
	GOLDEN_OVERRIDE=""
	rm -f "$tmp_golden"

	after_sum="$(sha256sum "$golden_file" | awk '{print $1}')"

	if [ "$before_sum" != "$after_sum" ]; then
		echo "SELFCHECK: FAILED — committed golden was mutated during self-check"
		exit 4
	fi
	if [ "$real_ret" -ne 0 ]; then
		echo "SELFCHECK: FAILED — real golden did not match its own fixture (harness broken)"
		exit 5
	fi
	if [ "$corrupt_ret" -eq 0 ]; then
		echo "SELFCHECK: FAILED — comparator did NOT detect a corrupted golden (no teeth)"
		exit 6
	fi
	echo "  real golden      -> PASS (matched)"
	echo "  corrupted golden -> FAIL (mismatch detected, suite went red)"
	echo "  committed golden -> unchanged (sha256 stable)"
	echo "SELFCHECK: OK"
	exit 1
}

if [ "$SELFCHECK" -eq 1 ]; then
	run_selfcheck
fi

echo "==========================================="
echo " BLang LSP Golden-Transcript Test Suite"
echo "==========================================="
echo ""

if [ ${#FILE_ARGS[@]} -gt 0 ]; then
	FIXTURES=("${FILE_ARGS[@]}")
else
	FIXTURES=()
	while IFS= read -r f; do
		FIXTURES+=("$f")
	done < <(ls "$LSP_DIR"/*.lsp.jsonl 2>/dev/null | sort)
fi

if [ ${#FIXTURES[@]} -eq 0 ]; then
	echo -e "${RED}No fixtures found under test_files/lsp/${NC}"
	exit 1
fi

for f in "${FIXTURES[@]}"; do
	run_one_fixture "$f"
done

echo ""
echo "==========================================="
echo "  Passed:  $PASS_COUNT"
echo "  Failed:  $FAIL_COUNT"
if [ "$NOGOLDEN_COUNT" -gt 0 ]; then
	echo -e "  ${YELLOW}No golden: $NOGOLDEN_COUNT${NC}"
fi
echo "  Total:   $TOTAL"
echo "==========================================="

if [ "$FAIL_COUNT" -gt 0 ]; then
	exit 1
fi
exit 0
