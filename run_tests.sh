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

run_test() {
	local file="$1"
	local expect="$2"  # "pass", "fail", or "xfail"
	local name
	name="$(basename "$file")"
	TOTAL=$((TOTAL + 1))

	# Run qcc with a timeout, discard verbose output
	local output
	output=$(timeout 10 "$QCC" "$file" 2>/dev/null)
	local exit_code=$?

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
					timeout 10 "$QCC" "$file" 2>&1 >/dev/null | tail -5 | sed 's/^/    /'
				fi
			fi
			;;
		fail)
			if [ $passed -eq 0 ]; then
				echo -e "  ${GREEN}PASS${NC}  $file  (correctly rejected)"
				PASS_COUNT=$((PASS_COUNT + 1))
			else
				echo -e "  ${RED}FAIL${NC}  $file  (expected failure, but parsed OK)"
				FAIL_COUNT=$((FAIL_COUNT + 1))
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
FAIL_FILES=$(find "$SCRIPT_DIR/test_files/fail" -name '*.b' 2>/dev/null | sort)
if [ -n "$FAIL_FILES" ]; then
	echo -e "${CYAN}--- Tests expected to FAIL (negative tests) ---${NC}"
	while IFS= read -r f; do
		run_test "$f" "fail"
	done <<< "$FAIL_FILES"
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
			run_test "$f" "fail"
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
