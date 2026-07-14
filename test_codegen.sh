#!/bin/bash
# Test the full compilation pipeline: BLang source -> LLVM IR -> native binary -> run
# Requires: LLVM dev headers installed and qcc built with -DBLANG_HAS_LLVM
#
# Usage:
#   ./test_codegen.sh                   # Run ALL codegen_*.b tests
#   ./test_codegen.sh [test_file...]    # Run specific test file(s)
#   ./test_codegen.sh --verbose         # Run all tests with IR output
#   BUILD_DIR=path ./test_codegen.sh    # Use a different build directory

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/build}"
QCC="${BUILD_DIR}/qcc"
RUNTIME_LIB="${BUILD_DIR}/libblang_runtime.a"
STRING_LIB="${BUILD_DIR}/libblang_string.a"
ARRAY_LIB="${BUILD_DIR}/libblang_array.a"
BUFFER_LIB="${BUILD_DIR}/libblang_buffer.a"
JSON_LIB="${BUILD_DIR}/libblang_json.a"
NET_LIB="${BUILD_DIR}/libblang_net.a"
FS_LIB="${BUILD_DIR}/libblang_fs.a"
SYS_LIB="${BUILD_DIR}/libblang_sys.a"
STDLIB_IO="${SCRIPT_DIR}/stdlib/io.b"
STDLIB_NET="${SCRIPT_DIR}/stdlib/net.b"
STDLIB_FS="${SCRIPT_DIR}/stdlib/fs.b"
STDLIB_SYS="${SCRIPT_DIR}/stdlib/sys.b"
STDLIB_BUFFER="${SCRIPT_DIR}/stdlib/buffer.b"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

VERBOSE=0
FILE_ARGS=()
LEAK_CHECK=0
VALGRIND=0

for arg in "$@"; do
	case "$arg" in
		--verbose) VERBOSE=1 ;;
		--leak-check) LEAK_CHECK=1 ;;
		--valgrind) VALGRIND=1 ;;
		--help)
			echo "Usage: $0 [--verbose] [--leak-check] [--valgrind] [test_file...]"
			echo "  --verbose     Show IR output for each test"
			echo "  --leak-check  Link with AddressSanitizer and report memory leaks"
			echo "  --valgrind    Run each binary under Valgrind to detect memory leaks"
			echo "  test_file...  Run only the specified test file(s)"
			echo ""
			echo "With no test files, runs all test_files/codegen_*.b tests."
			echo "Environment: BUILD_DIR overrides the build directory (default: ./build)."
			exit 0
			;;
		*) FILE_ARGS+=("$arg") ;;
	esac
done

if [ "$VALGRIND" -eq 1 ]; then
	if ! command -v valgrind &>/dev/null; then
		echo -e "${RED}Error: valgrind not found on PATH${NC}"
		exit 1
	fi
fi

if [ ! -x "$QCC" ]; then
	echo -e "${RED}Error: qcc not found at $QCC${NC}"
	echo "Build with: cd build && cmake .. -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && make"
	exit 1
fi

PASS_COUNT=0
FAIL_COUNT=0
LEAK_TOTAL=0
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
	# Use --combine to include stdlib files (sys.b always, net.b for networking tests)
	local qcc_args=()
	local need_combine=0
	local stdlib_files=()
	if [ -f "${STDLIB_SYS}" ]; then
		stdlib_files+=("${STDLIB_SYS}")
		need_combine=1
	fi
	# Buffer is a fundamental type used by fs.b and net.b — always include it
	if [ -f "${STDLIB_BUFFER}" ]; then
		stdlib_files+=("${STDLIB_BUFFER}")
		need_combine=1
	fi
	if [[ "${base_name}" == *"tcp"* ]] || [[ "${base_name}" == *"selector"* ]] || [[ "${base_name}" == *"net"* ]] || [[ "${base_name}" == *"sys_args"* ]] || [[ "${base_name}" == *"http"* ]]; then
		if [ -f "${STDLIB_NET}" ]; then
			stdlib_files+=("${STDLIB_NET}")
			need_combine=1
		fi
	fi
	if [[ "${base_name}" == *"file"* ]] || [[ "${base_name}" == *"fs"* ]]; then
		if [ -f "${STDLIB_FS}" ]; then
			stdlib_files+=("${STDLIB_FS}")
			need_combine=1
		fi
	fi
	if [ $need_combine -eq 1 ]; then
		qcc_args+=("--combine" "${stdlib_files[@]}")
	fi
	local qcc_output
	qcc_output=$("${QCC}" "${qcc_args[@]}" "${test_file}" 2>&1)
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
	local sanitize_flags=""
	# Detect libuv for async/await event loop support
	if pkg-config --exists libuv 2>/dev/null; then
		extra_libs="-luv"
	fi
	if [ "$LEAK_CHECK" -eq 1 ]; then
		sanitize_flags="-fsanitize=address,leak"
	fi
	local json_link=""
	if [ -f "${JSON_LIB}" ]; then
		json_link="${JSON_LIB}"
	fi
	local string_link=""
	if [ -f "${STRING_LIB}" ]; then
		string_link="${STRING_LIB}"
	fi
	local array_link=""
	if [ -f "${ARRAY_LIB}" ]; then
		array_link="${ARRAY_LIB}"
	fi
	local buffer_link=""
	if [ -f "${BUFFER_LIB}" ]; then
		buffer_link="${BUFFER_LIB}"
	fi
	local net_link=""
	if [ -f "${NET_LIB}" ]; then
		net_link="${NET_LIB}"
	fi
	local fs_link=""
	if [ -f "${FS_LIB}" ]; then
		fs_link="${FS_LIB}"
	fi
	local sys_link=""
	if [ -f "${SYS_LIB}" ]; then
		sys_link="${SYS_LIB}"
	fi
	if [ -f "${RUNTIME_LIB}" ]; then
		cc_output=$(cc ${sanitize_flags} "${obj_file}" "${RUNTIME_LIB}" ${sys_link} ${fs_link} ${net_link} ${json_link} ${buffer_link} ${array_link} ${string_link} -lpthread ${extra_libs} -o "${bin_file}" 2>&1)
	else
		cc_output=$(cc ${sanitize_flags} "${obj_file}" ${sys_link} ${fs_link} ${net_link} ${json_link} ${buffer_link} ${array_link} ${string_link} -o "${bin_file}" 2>&1)
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
	local run_env=""
	if [ "$LEAK_CHECK" -eq 1 ]; then
		run_env="ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS=exitcode=23"
	fi

	if [ "$VALGRIND" -eq 1 ]; then
		run_output=$(timeout 30 valgrind --leak-check=full \
			--error-exitcode=42 "${bin_file}" 2>&1)
		local exit_code=$?

		# Parse Valgrind summary
		local definitely_lost
		definitely_lost=$(echo "$run_output" | grep -oP 'definitely lost: \K[0-9,]+' | tr -d ',')
		local indirectly_lost
		indirectly_lost=$(echo "$run_output" | grep -oP 'indirectly lost: \K[0-9,]+' | tr -d ',')
		local total_leaked=$(( ${definitely_lost:-0} + ${indirectly_lost:-0} ))

		if [ $exit_code -eq 124 ]; then
			echo -e "  ${RED}FAIL${NC}  ${test_file}  (timeout — binary hung)"
			FAIL_COUNT=$((FAIL_COUNT + 1))
			rm -f "${ir_file}" "${obj_file}" "${bin_file}"
			return 1
		fi

		# Valgrind exit code 42 = errors detected; other non-zero = program failure
		if [ $exit_code -ne 0 ] && [ $exit_code -ne 42 ]; then
			echo -e "  ${RED}FAIL${NC}  ${test_file}  (runtime exit $exit_code)"
			if [ "$VERBOSE" -eq 1 ]; then
				echo "$run_output" | tail -10 | sed 's/^/    /'
			fi
			FAIL_COUNT=$((FAIL_COUNT + 1))
			rm -f "${ir_file}" "${obj_file}" "${bin_file}"
			return 1
		fi

		if [ "$total_leaked" -gt 0 ]; then
			local leak_summary
			leak_summary=$(echo "$run_output" | grep 'definitely lost:\|indirectly lost:' | head -2)
			echo -e "  ${YELLOW}LEAK${NC}  ${test_file}  (${definitely_lost:-0} direct + ${indirectly_lost:-0} indirect bytes)"
			if [ "$VERBOSE" -eq 1 ]; then
				echo "$run_output" | sed 's/^/    /'
			fi
			PASS_COUNT=$((PASS_COUNT + 1))
			LEAK_TOTAL=$((LEAK_TOTAL + 1))
			rm -f "${ir_file}" "${obj_file}" "${bin_file}"
			return 0
		fi

		echo -e "  ${GREEN}CLEAN${NC} ${test_file}"
		PASS_COUNT=$((PASS_COUNT + 1))
	else
		run_output=$(timeout 10 env ${run_env} "${bin_file}" 2>&1)
		local exit_code=$?

		# With leak check, LSan returns exit code 23 (forced via LSAN_OPTIONS=exitcode=23)
		local leak_count=0
		if [ "$LEAK_CHECK" -eq 1 ]; then
			leak_count=$(echo "$run_output" | grep -c 'SUMMARY: AddressSanitizer:' 2>/dev/null || true)
			# If the program logic passed but leaks detected (exit 23)
			if [ $exit_code -eq 23 ]; then
				# Leaks detected but program logic passed
				local leak_summary
				leak_summary=$(echo "$run_output" | grep 'SUMMARY:' | head -1)
				echo -e "  ${YELLOW}LEAK${NC}  ${test_file}  ($leak_summary)"
				if [ "$VERBOSE" -eq 1 ]; then
					echo "$run_output" | grep -A1 'Direct leak\|Indirect leak' | sed 's/^/    /'
				fi
				PASS_COUNT=$((PASS_COUNT + 1))
				LEAK_TOTAL=$((LEAK_TOTAL + 1))
				rm -f "${ir_file}" "${obj_file}" "${bin_file}"
				return 0
			fi
		fi

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

		if [ "$LEAK_CHECK" -eq 1 ]; then
			echo -e "  ${GREEN}CLEAN${NC} ${test_file}"
		else
			echo -e "  ${GREEN}PASS${NC}  ${test_file}"
		fi
		PASS_COUNT=$((PASS_COUNT + 1))
	fi

	# Cleanup
	rm -f "${ir_file}" "${obj_file}" "${bin_file}"
	return 0
}

echo "==========================================="
echo " BLang Codegen E2E Test Suite"
echo "==========================================="
echo ""

if [ ${#FILE_ARGS[@]} -eq 1 ]; then
	# Single-file mode: show IR by default
	echo -e "${CYAN}--- Single test: ${FILE_ARGS[0]} ---${NC}"
	run_one_test "${FILE_ARGS[0]}" 1
elif [ ${#FILE_ARGS[@]} -gt 1 ]; then
	# Explicit file list
	echo -e "${CYAN}--- Selected tests (${#FILE_ARGS[@]} files) ---${NC}"
	for f in "${FILE_ARGS[@]}"; do
		run_one_test "$f" "$VERBOSE"
	done
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
if [ "$LEAK_CHECK" -eq 1 ]; then
echo -e "  ${YELLOW}Leaks:${NC}   $LEAK_TOTAL"
fi
echo "  Total:   $TOTAL"
echo "==========================================="

if [ $FAIL_COUNT -gt 0 ]; then
	exit 1
fi
exit 0
