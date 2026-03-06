#!/bin/bash
# Test the full compilation pipeline: BLang source -> LLVM IR -> native binary -> run
# Requires: LLVM dev headers installed and qcc built with -DBLANG_HAS_LLVM
#
# Usage:
#   ./test_codegen.sh                   # Run ALL codegen_*.b tests
#   ./test_codegen.sh [test_file]       # Run a single test file
#   ./test_codegen.sh --verbose         # Run all tests with IR output

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
QCC="${SCRIPT_DIR}/build/qcc"
RUNTIME_LIB="${SCRIPT_DIR}/build/libblang_runtime.a"
JSON_LIB="${SCRIPT_DIR}/build/libblang_json.a"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

VERBOSE=0
SINGLE_FILE=""

for arg in "$@"; do
	case "$arg" in
		--verbose) VERBOSE=1 ;;
		--help)
			echo "Usage: $0 [--verbose] [test_file]"
			echo "  --verbose    Show IR output for each test"
			echo "  test_file    Run only the specified test file"
			echo ""
			echo "With no arguments, runs all test_files/codegen_*.b tests."
			exit 0
			;;
		*) SINGLE_FILE="$arg" ;;
	esac
done

if [ ! -x "$QCC" ]; then
	echo -e "${RED}Error: qcc not found at $QCC${NC}"
	echo "Build with: cd build && cmake .. -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && make"
	exit 1
fi

PASS_COUNT=0
FAIL_COUNT=0
TOTAL=0

# Run one test through the full pipeline: qcc -> llc -> cc -> run
# Returns 0 on success, 1 on failure
run_one_test() {
	local test_file="$1"
	local show_ir="$2"  # 1 to show IR, 0 to suppress
	local base_name
	base_name="$(basename "${test_file}" .b)"
	local ir_file="/tmp/${base_name}.ll"
	local obj_file="/tmp/${base_name}.o"
	local bin_file="/tmp/${base_name}"

	TOTAL=$((TOTAL + 1))

	# Step 1: Parse and generate IR
	local qcc_output
	qcc_output=$("${QCC}" "${test_file}" 2>&1)
	local qcc_exit=$?
	if [ $qcc_exit -ne 0 ]; then
		echo -e "  ${RED}FAIL${NC}  ${test_file}  (qcc failed, exit $qcc_exit)"
		if [ "$VERBOSE" -eq 1 ]; then
			echo "$qcc_output" | tail -5 | sed 's/^/    /'
		fi
		FAIL_COUNT=$((FAIL_COUNT + 1))
		return 1
	fi

	# Find the generated .ll file
	local src_ll
	src_ll="$(dirname "${test_file}")/${base_name}.ll"
	if [ -f "${src_ll}" ]; then
		cp "${src_ll}" "${ir_file}"
	elif [ -f "${base_name}.ll" ]; then
		cp "${base_name}.ll" "${ir_file}"
	fi

	if [ ! -f "${ir_file}" ]; then
		echo -e "  ${RED}FAIL${NC}  ${test_file}  (no .ll file generated)"
		FAIL_COUNT=$((FAIL_COUNT + 1))
		return 1
	fi

	if [ "$show_ir" -eq 1 ]; then
		echo "--- IR for ${base_name} ---"
		cat "${ir_file}"
		echo ""
	fi

	# Step 2: Compile IR to object file
	local llc_output
	llc_output=$(llc-18 -filetype=obj -relocation-model=pic "${ir_file}" -o "${obj_file}" 2>&1) || \
	llc_output=$(llc -filetype=obj -relocation-model=pic "${ir_file}" -o "${obj_file}" 2>&1)
	if [ $? -ne 0 ]; then
		echo -e "  ${RED}FAIL${NC}  ${test_file}  (llc failed)"
		if [ "$VERBOSE" -eq 1 ]; then
			echo "$llc_output" | tail -5 | sed 's/^/    /'
		fi
		rm -f "${ir_file}"
		FAIL_COUNT=$((FAIL_COUNT + 1))
		return 1
	fi

	# Step 3: Link to native binary
	local cc_output
	local extra_libs=""
	# Detect libuv for async/await event loop support
	if pkg-config --exists libuv 2>/dev/null; then
		extra_libs="-luv"
	fi
	local json_link=""
	if [ -f "${JSON_LIB}" ]; then
		json_link="${JSON_LIB}"
	fi
	if [ -f "${RUNTIME_LIB}" ]; then
		cc_output=$(cc "${obj_file}" "${RUNTIME_LIB}" ${json_link} -lpthread ${extra_libs} -o "${bin_file}" 2>&1)
	else
		cc_output=$(cc "${obj_file}" ${json_link} -o "${bin_file}" 2>&1)
	fi
	if [ $? -ne 0 ]; then
		echo -e "  ${RED}FAIL${NC}  ${test_file}  (link failed)"
		if [ "$VERBOSE" -eq 1 ]; then
			echo "$cc_output" | tail -5 | sed 's/^/    /'
		fi
		rm -f "${ir_file}" "${obj_file}"
		FAIL_COUNT=$((FAIL_COUNT + 1))
		return 1
	fi

	# Step 4: Run with timeout (catches hangs from missing runtime shutdown, etc.)
	local run_output
	run_output=$(timeout 10 "${bin_file}" 2>&1)
	local exit_code=$?
	if [ $exit_code -eq 124 ]; then
		echo -e "  ${RED}FAIL${NC}  ${test_file}  (timeout — binary hung)"
		FAIL_COUNT=$((FAIL_COUNT + 1))
		rm -f "${ir_file}" "${obj_file}" "${bin_file}"
		return 1
	elif [ $exit_code -ne 0 ]; then
		echo -e "  ${RED}FAIL${NC}  ${test_file}  (runtime exit $exit_code)"
		if [ "$VERBOSE" -eq 1 ]; then
			echo "$run_output" | tail -5 | sed 's/^/    /'
		fi
		FAIL_COUNT=$((FAIL_COUNT + 1))
		rm -f "${ir_file}" "${obj_file}" "${bin_file}"
		return 1
	fi

	echo -e "  ${GREEN}PASS${NC}  ${test_file}"
	PASS_COUNT=$((PASS_COUNT + 1))

	# Cleanup
	rm -f "${ir_file}" "${obj_file}" "${bin_file}"
	return 0
}

echo "==========================================="
echo " BLang Codegen E2E Test Suite"
echo "==========================================="
echo ""

if [ -n "$SINGLE_FILE" ]; then
	# Single-file mode: show IR by default
	echo -e "${CYAN}--- Single test: ${SINGLE_FILE} ---${NC}"
	run_one_test "$SINGLE_FILE" 1
else
	# Multi-file mode: run all codegen_*.b tests
	TEST_FILES=$(find "$SCRIPT_DIR/test_files" -maxdepth 1 -name 'codegen_*.b' 2>/dev/null | sort)
	if [ -z "$TEST_FILES" ]; then
		echo -e "${YELLOW}No codegen test files found in test_files/codegen_*.b${NC}"
		exit 0
	fi

	echo -e "${CYAN}--- E2E codegen tests (parse → IR → compile → link → run) ---${NC}"
	while IFS= read -r f; do
		run_one_test "$f" "$VERBOSE"
	done <<< "$TEST_FILES"
fi

echo ""
echo "==========================================="
echo " Results"
echo "==========================================="
echo -e "  ${GREEN}Passed:${NC}  $PASS_COUNT"
echo -e "  ${RED}Failed:${NC}  $FAIL_COUNT"
echo "  Total:   $TOTAL"
echo "==========================================="

if [ $FAIL_COUNT -gt 0 ]; then
	exit 1
fi
exit 0
