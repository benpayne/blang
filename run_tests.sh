#!/bin/bash
#
# BLang Compiler Test Runner
#
# Runs qcc against test files organized in three categories:
#   test_files/pass/  - Should parse successfully (exit 0)
#   test_files/fail/  - Should fail to parse (exit non-zero)
#   test_files/xfail/ - Known-broken features, expected to fail (exit non-zero)
#
# Usage: ./run_tests.sh [--verbose] [--build]
#        BUILD_DIR=path ./run_tests.sh   # Use a different build directory

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
QCC="$BUILD_DIR/qcc"

# Default sanitizer runtime options. run_tests.sh is a parse/sema CORRECTNESS
# gate, not a leak gate (leak-checking is test_codegen.sh --leak-check, U4), and
# qcc links LLVM which does not free everything at exit — so LeakSanitizer is
# turned OFF here to avoid non-bug "leak" noise, while real memory errors (ASan)
# and undefined behavior (UBSan) stay FATAL. These env vars are ignored by
# uninstrumented binaries, so this is a strict no-op for the default/parse-only
# builds. Respect any values the caller already exported.
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1:halt_on_error=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

VERBOSE=0
DO_BUILD=0

for arg in "$@"; do
	case "$arg" in
		--verbose) VERBOSE=1 ;;
		--build)   DO_BUILD=1 ;;
		--help)
			echo "Usage: $0 [--verbose] [--build]"
			echo "  --verbose  Show detailed output for each test"
			echo "  --build    Build the project before running tests"
			exit 0
			;;
	esac
done

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

PASS_COUNT=0
FAIL_COUNT=0
XFAIL_COUNT=0
XPASS_COUNT=0
TOTAL=0

# Build if requested
if [ "$DO_BUILD" -eq 1 ]; then
	echo -e "${CYAN}Building project...${NC}"
	mkdir -p "$BUILD_DIR"
	(cd "$BUILD_DIR" && cmake .. -DCMAKE_BUILD_TYPE=Debug > /dev/null 2>&1 && make -j$(nproc) > /dev/null 2>&1)
	if [ $? -ne 0 ]; then
		echo -e "${RED}Build failed!${NC}"
		exit 1
	fi
	echo -e "${GREEN}Build succeeded.${NC}"
	echo ""
fi

if [ ! -x "$QCC" ]; then
	echo -e "${RED}Error: qcc not found at $QCC${NC}"
	echo "Run with --build to build first, or build manually."
	exit 1
fi

# Resolve the expected-error pattern for a negative test (U2 harness).
# Precedence: companion "<test>.expected" file (first non-empty, non-# line)
#   > inline "// EXPECT-ERROR: <pattern>" comment in the .b source > none.
# Prints the ERE pattern (empty if none declared).
resolve_expected_pattern() {
	local file="$1"
	local expfile="${file}.expected"
	if [ -f "$expfile" ]; then
		# First line that is non-empty and does not start with '#'.
		grep -m1 -E '^[[:space:]]*[^#[:space:]]' "$expfile" | sed 's/[[:space:]]*$//'
		return
	fi
	local line
	line=$(grep -m1 'EXPECT-ERROR:' "$file" 2>/dev/null)
	if [ -n "$line" ]; then
		# Everything after the marker, leading/trailing whitespace trimmed.
		printf '%s' "$line" | sed 's/.*EXPECT-ERROR:[[:space:]]*//; s/[[:space:]]*$//'
	fi
}

run_test() {
	local file="$1"
	local expect="$2"    # "pass", "fail", or "xfail"
	local qcc_flag="$3"  # optional invocation flag, e.g. "--parse-only"
	local name
	name="$(basename "$file")"
	TOTAL=$((TOTAL + 1))

	# For negative tests we capture stderr so the declared diagnostic pattern
	# can be asserted; for pass/xfail we only care about the exit code.
	local exit_code stderr_out
	if [ "$expect" = "fail" ]; then
		stderr_out=$(timeout 10 "$QCC" $qcc_flag "$file" 2>&1 >/dev/null)
		exit_code=$?
	else
		timeout 10 "$QCC" $qcc_flag "$file" >/dev/null 2>&1
		exit_code=$?
	fi

	local passed=0
	if [ $exit_code -eq 0 ]; then
		passed=1
	fi

	case "$expect" in
		pass)
			if [ $passed -eq 1 ]; then
				echo -e "  ${GREEN}PASS${NC}  $file"
				PASS_COUNT=$((PASS_COUNT + 1))
			else
				echo -e "  ${RED}FAIL${NC}  $file  (expected pass, got exit $exit_code)"
				FAIL_COUNT=$((FAIL_COUNT + 1))
				if [ "$VERBOSE" -eq 1 ]; then
					echo "    stderr:"
					timeout 10 "$QCC" $qcc_flag "$file" 2>&1 >/dev/null | tail -5 | sed 's/^/    /'
				fi
			fi
			;;
		fail)
			if [ $passed -eq 1 ]; then
				# A negative test that compiled cleanly is always a failure,
				# regardless of any declared pattern.
				echo -e "  ${RED}FAIL${NC}  $file  (expected failure, but parsed OK)"
				FAIL_COUNT=$((FAIL_COUNT + 1))
			else
				# Canonical located-error gate for the fail/sema/ category (U3,
				# contracts/fail-sema-harness.md, FR-014): every file under
				# test_files/fail/sema/ must additionally emit a diagnostic
				# matching "<file>.b:<line>:<col>: error: " — the exact per-file
				# check the epic done-condition #3 requires. Files elsewhere under
				# fail/ keep the U2 behavior untouched.
				local canonical_ok=1
				case "$file" in
					*/test_files/fail/sema/*)
						if ! printf '%s' "$stderr_out" | grep -Eq -- '^[^:]+\.b:[0-9]+:[0-9]+: error: '; then
							canonical_ok=0
						fi
						;;
				esac

				local pattern
				pattern="$(resolve_expected_pattern "$file")"
				if [ "$canonical_ok" -eq 0 ]; then
					echo -e "  ${RED}FAIL${NC}  $file  (missing canonical file:line:col diagnostic)"
					FAIL_COUNT=$((FAIL_COUNT + 1))
					if [ "$VERBOSE" -eq 1 ]; then
						echo "    expected canonical regex: ^[^:]+\\.b:[0-9]+:[0-9]+: error: "
						echo "    actual stderr:"
						printf '%s\n' "$stderr_out" | tail -5 | sed 's/^/    /'
					fi
				elif [ -n "$pattern" ]; then
					if printf '%s' "$stderr_out" | grep -Eq -- "$pattern"; then
						echo -e "  ${GREEN}PASS${NC}  $file  (correctly rejected, diagnostic matched)"
						PASS_COUNT=$((PASS_COUNT + 1))
					else
						echo -e "  ${RED}FAIL${NC}  $file  (wrong diagnostic)"
						FAIL_COUNT=$((FAIL_COUNT + 1))
						if [ "$VERBOSE" -eq 1 ]; then
							echo "    expected pattern: $pattern"
							echo "    actual stderr:"
							printf '%s\n' "$stderr_out" | tail -5 | sed 's/^/    /'
						fi
					fi
				else
					echo -e "  ${GREEN}PASS${NC}  $file  (correctly rejected)"
					PASS_COUNT=$((PASS_COUNT + 1))
				fi
			fi
			;;
		xfail)
			if [ $passed -eq 0 ]; then
				echo -e "  ${YELLOW}XFAIL${NC} $file  (known failure)"
				XFAIL_COUNT=$((XFAIL_COUNT + 1))
			else
				echo -e "  ${CYAN}XPASS${NC} $file  (unexpectedly passed!)"
				XPASS_COUNT=$((XPASS_COUNT + 1))
			fi
			;;
	esac
}

echo "==========================================="
echo " BLang Compiler Test Suite"
echo "==========================================="
echo ""

# --- Pass tests ---
PASS_FILES=$(find "$SCRIPT_DIR/test_files/pass" -name '*.b' 2>/dev/null | sort)
if [ -n "$PASS_FILES" ]; then
	echo -e "${CYAN}--- Tests expected to PASS ---${NC}"
	while IFS= read -r f; do
		run_test "$f" "pass"
	done <<< "$PASS_FILES"
	echo ""
fi

# --- Fail tests ---
# fail/warn/ is a distinct WARNING category (below): those fixtures compile with
# exit 0 (a warning is not an error) unless -Werror is given, so they must not be
# swept into the exit-non-zero fail glob.
FAIL_FILES=$(find "$SCRIPT_DIR/test_files/fail" -name '*.b' -not -path '*/fail/warn/*' 2>/dev/null | sort)
if [ -n "$FAIL_FILES" ]; then
	echo -e "${CYAN}--- Tests expected to FAIL (negative tests) ---${NC}"
	while IFS= read -r f; do
		run_test "$f" "fail" "--parse-only"
	done <<< "$FAIL_FILES"
	echo ""
fi

# --- Warning tests (fail/warn/) ---
# Each fixture must: (a) without -Werror, exit 0 AND print a `warning:` line;
# (b) with -Werror, exit non-zero (promotion). This gives the warning severity +
# -Werror contract teeth in the standard suite (U1, diagnostics).
WARN_FILES=$(find "$SCRIPT_DIR/test_files/fail/warn" -name '*.b' 2>/dev/null | sort)
if [ -n "$WARN_FILES" ]; then
	echo -e "${CYAN}--- Warning tests (fail/warn/) ---${NC}"
	while IFS= read -r f; do
		[ -z "$f" ] && continue
		w_out=$(timeout 10 "$QCC" --parse-only "$f" 2>&1 >/dev/null)
		w_rc=$?
		timeout 10 "$QCC" -Werror --parse-only "$f" >/dev/null 2>&1
		we_rc=$?
		if [ "$w_rc" -eq 0 ] && printf '%s' "$w_out" | grep -q 'warning:' && [ "$we_rc" -ne 0 ]; then
			echo -e "  ${GREEN}PASS${NC}  $f  (warns; -Werror promotes)"
			PASS_COUNT=$((PASS_COUNT + 1))
		else
			echo -e "  ${RED}FAIL${NC}  $f  (rc=$w_rc warn?=$(printf '%s' "$w_out" | grep -qc 'warning:') werror_rc=$we_rc)"
			FAIL_COUNT=$((FAIL_COUNT + 1))
		fi
		TOTAL=$((TOTAL + 1))
	done <<< "$WARN_FILES"
	echo ""
fi

# --- Codegen fail tests (only when built with LLVM) ---
# Detect LLVM support: run qcc --help and check for --emit-ir
HAS_LLVM=0
if "$QCC" --help 2>&1 | grep -q "emit-ir"; then
	HAS_LLVM=1
fi

CGFAIL_FILES=$(find "$SCRIPT_DIR/test_files/cgfail" -name '*.b' 2>/dev/null | sort)
if [ -n "$CGFAIL_FILES" ]; then
	if [ "$HAS_LLVM" -eq 1 ]; then
		echo -e "${CYAN}--- Tests expected to FAIL at codegen (requires LLVM) ---${NC}"
		while IFS= read -r f; do
			run_test "$f" "fail" ""
		done <<< "$CGFAIL_FILES"
		echo ""
	else
		SKIP_COUNT=$(echo "$CGFAIL_FILES" | wc -l)
		echo -e "${YELLOW}--- Skipping $SKIP_COUNT codegen-fail tests (no LLVM) ---${NC}"
		echo ""
	fi
fi

# --- Expected failure tests ---
XFAIL_FILES=$(find "$SCRIPT_DIR/test_files/xfail" -name '*.b' 2>/dev/null | sort)
if [ -n "$XFAIL_FILES" ]; then
	echo -e "${CYAN}--- Tests for known-broken features (XFAIL) ---${NC}"
	while IFS= read -r f; do
		run_test "$f" "xfail"
	done <<< "$XFAIL_FILES"
	echo ""
fi

# --- --dump-locations goldens ---
# Every test_files/golden/<name>.locations pins the exact --dump-locations
# output for test_files/pass/<name>.b (paths in the golden are relative, so
# qcc runs from the repo root). Regenerate intentionally with:
#   ./build/qcc --dump-locations test_files/pass/<name>.b > test_files/golden/<name>.locations
GOLDEN_FILES=$(find "$SCRIPT_DIR/test_files/golden" -name '*.locations' 2>/dev/null | sort)
if [ -n "$GOLDEN_FILES" ]; then
	echo -e "${CYAN}--- --dump-locations goldens ---${NC}"
	while IFS= read -r g; do
		[ -z "$g" ] && continue
		TOTAL=$((TOTAL + 1))
		gname=$(basename "$g" .locations)
		gsrc="test_files/pass/$gname.b"
		if [ ! -f "$SCRIPT_DIR/$gsrc" ]; then
			echo -e "  ${RED}FAIL${NC}  $gsrc  (golden $g has no source file)"
			FAIL_COUNT=$((FAIL_COUNT + 1))
			continue
		fi
		g_actual=$(cd "$SCRIPT_DIR" && timeout 10 "$QCC" --dump-locations "$gsrc" 2>/dev/null)
		if [ "$g_actual" = "$(cat "$g")" ]; then
			echo -e "  ${GREEN}PASS${NC}  $gsrc  (locations match golden)"
			PASS_COUNT=$((PASS_COUNT + 1))
		else
			echo -e "  ${RED}FAIL${NC}  $gsrc  (locations differ from $g)"
			if [ "$VERBOSE" -eq 1 ]; then
				diff <(cat "$g") <(printf '%s\n' "$g_actual") | head -20 | sed 's/^/    /'
			fi
			FAIL_COUNT=$((FAIL_COUNT + 1))
		fi
	done <<< "$GOLDEN_FILES"
	echo ""
fi

# --- Summary ---
echo "==========================================="
echo " Results"
echo "==========================================="
EFFECTIVE_PASS=$((PASS_COUNT + XFAIL_COUNT))
EFFECTIVE_FAIL=$((FAIL_COUNT + XPASS_COUNT))
echo -e "  ${GREEN}Passed:${NC}           $PASS_COUNT"
echo -e "  ${RED}Failed:${NC}           $FAIL_COUNT"
echo -e "  ${YELLOW}Expected failures:${NC} $XFAIL_COUNT"
if [ $XPASS_COUNT -gt 0 ]; then
	echo -e "  ${CYAN}Unexpected passes:${NC} $XPASS_COUNT (features fixed!)"
fi
echo "  Total:            $TOTAL"
echo "==========================================="

if [ $FAIL_COUNT -gt 0 ]; then
	exit 1
fi
exit 0
