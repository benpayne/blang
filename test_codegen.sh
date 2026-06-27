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
STRING_LIB="${SCRIPT_DIR}/build/libblang_string.a"
ARRAY_LIB="${SCRIPT_DIR}/build/libblang_array.a"
BUFFER_LIB="${SCRIPT_DIR}/build/libblang_buffer.a"
JSON_LIB="${SCRIPT_DIR}/build/libblang_json.a"
NET_LIB="${SCRIPT_DIR}/build/libblang_net.a"
SYS_LIB="${SCRIPT_DIR}/build/libblang_sys.a"
DB_LIB="${SCRIPT_DIR}/build/libblang_db.a"
STDLIB_NET="${SCRIPT_DIR}/stdlib/net.b"
STDLIB_SYS="${SCRIPT_DIR}/stdlib/sys.b"
STDLIB_TIMER="${SCRIPT_DIR}/stdlib/timer.b"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

VERBOSE=0
SINGLE_FILE=""
LEAK_CHECK=0

for arg in "$@"; do
	case "$arg" in
		--verbose) VERBOSE=1 ;;
		--leak-check) LEAK_CHECK=1 ;;
		--help)
			echo "Usage: $0 [--verbose] [--leak-check] [test_file]"
			echo "  --verbose     Show IR output for each test"
			echo "  --leak-check  Link with AddressSanitizer and report memory leaks"
			echo "  test_file     Run only the specified test file"
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
LEAK_TOTAL=0
SKIP_COUNT=0
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

	# Database tests need a SQLite-backed libblang_db. If the DB runtime was
	# built without SQLite (stub), skip rather than fail — there is no backend
	# to execute against.
	local is_db_test=0
	if [[ "${base_name}" == *"db"* ]] || [[ "${base_name}" == *"query"* ]]; then
		is_db_test=1
		if [ ! -f "${DB_LIB}" ] || ! nm "${DB_LIB}" 2>/dev/null | grep -q "U sqlite3_"; then
			echo -e "  ${YELLOW}SKIP${NC}  ${test_file}  (SQLite backend not built)"
			SKIP_COUNT=$((SKIP_COUNT + 1))
			return 0
		fi
	fi

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
	if [[ "${base_name}" == *"tcp"* ]] || [[ "${base_name}" == *"selector"* ]] || [[ "${base_name}" == *"net"* ]] || [[ "${base_name}" == *"sys_args"* ]] || [[ "${base_name}" == *"http"* ]]; then
		if [ -f "${STDLIB_NET}" ]; then
			stdlib_files+=("${STDLIB_NET}")
			need_combine=1
		fi
	fi
	if [[ "${base_name}" == *"timer"* ]] || [[ "${base_name}" == *"event"* ]]; then
		if [ -f "${STDLIB_TIMER}" ]; then
			stdlib_files+=("${STDLIB_TIMER}")
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
	local sys_link=""
	if [ -f "${SYS_LIB}" ]; then
		sys_link="${SYS_LIB}"
	fi
	# Database tests link the DB runtime + its SQLite backend. By this point a
	# db test is known to have a SQLite-enabled libblang_db (otherwise skipped
	# above), so resolve the sqlite link flags, preferring pkg-config but
	# falling back to a plain -lsqlite3 when pkg-config metadata is absent.
	local db_link=""
	local db_sys_flags=""
	if [ "$is_db_test" -eq 1 ]; then
		db_link="${DB_LIB}"
		if pkg-config --exists sqlite3 2>/dev/null; then
			db_sys_flags="$(pkg-config --libs sqlite3)"
		else
			db_sys_flags="-lsqlite3"
		fi
		echo "    [db-test] DB_LIB=${db_link} db_sys_flags='${db_sys_flags}'"
		echo "    [db-test] sqlite refs in lib: $(nm "${DB_LIB}" 2>/dev/null | grep -c 'U sqlite3_')"
	fi
	if [ -f "${RUNTIME_LIB}" ]; then
		cc_output=$(cc ${sanitize_flags} "${obj_file}" "${RUNTIME_LIB}" ${db_link} ${sys_link} ${net_link} ${json_link} ${buffer_link} ${array_link} ${string_link} ${db_sys_flags} -lpthread ${extra_libs} -o "${bin_file}" 2>&1)
	else
		cc_output=$(cc ${sanitize_flags} "${obj_file}" ${db_link} ${sys_link} ${net_link} ${json_link} ${buffer_link} ${array_link} ${string_link} ${db_sys_flags} -o "${bin_file}" 2>&1)
	fi
	if [ $? -ne 0 ]; then
		echo -e "  ${RED}FAIL${NC}  ${test_file}  (link failed)"
		# Always surface the linker error — link failures are environment-
		# specific (missing system libs) and hard to diagnose without it.
		echo "$cc_output" | tail -8 | sed 's/^/    /'
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
	# Database tests use an in-memory SQLite DB unless the test ships a blang.toml.
	if [[ "${base_name}" == *"db"* ]] || [[ "${base_name}" == *"query"* ]]; then
		run_env="${run_env} BLANG_DATABASE_URL=:memory: BLANG_DATABASE_DRIVER=sqlite"
	fi
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
if [ "$SKIP_COUNT" -gt 0 ]; then
echo -e "  ${YELLOW}Skipped:${NC} $SKIP_COUNT"
fi
if [ "$LEAK_CHECK" -eq 1 ]; then
echo -e "  ${YELLOW}Leaks:${NC}   $LEAK_TOTAL"
fi
echo "  Total:   $TOTAL"
echo "==========================================="

if [ $FAIL_COUNT -gt 0 ]; then
	exit 1
fi
exit 0
