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
UPDATE_GOLDENS=0
SELFCHECK=0

for arg in "$@"; do
	case "$arg" in
		--verbose) VERBOSE=1 ;;
		--leak-check) LEAK_CHECK=1 ;;
		--valgrind) VALGRIND=1 ;;
		--update-goldens) UPDATE_GOLDENS=1 ;;
		--selfcheck) SELFCHECK=1 ;;
		--help)
			echo "Usage: $0 [--verbose] [--leak-check] [--valgrind] [--update-goldens] [--selfcheck] [test_file...]"
			echo "  --verbose         Show IR output for each test"
			echo "  --leak-check      Link with AddressSanitizer and report memory leaks"
			echo "  --valgrind        Run each binary under Valgrind to detect memory leaks"
			echo "  --update-goldens  Regenerate stdout goldens (test_files/<name>.expected.out)"
			echo "                    for deterministic (non-quarantined) tests; never touches"
			echo "                    quarantined tests."
			echo "  --selfcheck       Prove the golden comparator has teeth: corrupt a real"
			echo "                    committed golden in a TEMP copy, assert the suite goes red,"
			echo "                    print 'SELFCHECK: OK', and exit non-zero. Never mutates a"
			echo "                    committed golden."
			echo "  test_file...      Run only the specified test file(s)"
			echo ""
			echo "By default each non-quarantined test's stdout is compared exactly (modulo a"
			echo "single trailing newline) against its golden test_files/<name>.expected.out;"
			echo "a mismatch fails the test with a diff. Quarantined tests"
			echo "(test_files/codegen_quarantine.txt) run for exit code only."
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

QUARANTINE_FILE="${SCRIPT_DIR}/test_files/codegen_quarantine.txt"
# When set, run_one_test compares against this golden path instead of the default
# test_files/<name>.expected.out. Used only by --selfcheck (never in normal runs).
GOLDEN_OVERRIDE=""

# Strip exactly ONE trailing newline from a file's contents (the only permitted
# normalization, per design.md D2) and write the result to stdout with no added
# newline. Uses a sentinel so command substitution does not eat other newlines.
strip_one_trailing_nl() {
	local content
	content=$(cat "$1"; printf x)
	content=${content%x}
	content=${content%$'\n'}
	printf '%s' "$content"
}

# Exact-match compare (after single-trailing-newline strip) of an actual-stdout
# file vs a golden file. Returns 0 on match, 1 on mismatch. No loose/substring/
# regex/whitespace matching — cmp on the normalized bytes.
golden_matches() {
	local a="$1" g="$2" na ng rc
	na="$(mktemp)"; ng="$(mktemp)"
	strip_one_trailing_nl "$a" > "$na"
	strip_one_trailing_nl "$g" > "$ng"
	cmp -s "$na" "$ng"; rc=$?
	rm -f "$na" "$ng"
	return $rc
}

# Is a test (base name, e.g. codegen_http) quarantined from golden comparison?
# Reads codegen_quarantine.txt; '#' comments and blank lines ignored; entries may
# be listed with or without the .b extension.
is_quarantined() {
	local name="$1" entry
	[ -f "$QUARANTINE_FILE" ] || return 1
	while IFS= read -r entry || [ -n "$entry" ]; do
		entry="${entry%%#*}"
		entry="$(printf '%s' "$entry" | tr -d '[:space:]')"
		[ -z "$entry" ] && continue
		if [ "$entry" = "$name" ] || [ "$entry" = "${name}.b" ]; then
			return 0
		fi
	done < "$QUARANTINE_FILE"
	return 1
}

PASS_COUNT=0
FAIL_COUNT=0
LEAK_TOTAL=0
TOTAL=0
GOLDEN_PASS_COUNT=0
NOGOLDEN_COUNT=0
QUARANTINE_COUNT=0

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
		# Capture stdout and stderr separately: the golden compares against
		# stdout only (per REQ-001 / design.md), while stderr is retained for
		# leak-mode parsing and verbose display.
		local stdout_cap="/tmp/${base_name}.stdout.$$"
		local stderr_cap="/tmp/${base_name}.stderr.$$"
		timeout 10 env ${run_env} "${bin_file}" >"${stdout_cap}" 2>"${stderr_cap}"
		local exit_code=$?
		run_output="$(cat "${stdout_cap}" "${stderr_cap}" 2>/dev/null)"

		# With leak check, LSan returns exit code 23 (forced via LSAN_OPTIONS=exitcode=23).
		# Leak semantics are intentionally UNCHANGED here (U4 owns making leaks fatal).
		if [ "$LEAK_CHECK" -eq 1 ] && [ $exit_code -eq 23 ]; then
			local leak_summary
			leak_summary=$(echo "$run_output" | grep 'SUMMARY:' | head -1)
			echo -e "  ${YELLOW}LEAK${NC}  ${test_file}  ($leak_summary)"
			if [ "$VERBOSE" -eq 1 ]; then
				echo "$run_output" | grep -A1 'Direct leak\|Indirect leak' | sed 's/^/    /'
			fi
			PASS_COUNT=$((PASS_COUNT + 1))
			LEAK_TOTAL=$((LEAK_TOTAL + 1))
			rm -f "${ir_file}" "${obj_file}" "${bin_file}" "${stdout_cap}" "${stderr_cap}"
			return 0
		fi

		if [ $exit_code -eq 124 ]; then
			echo -e "  ${RED}FAIL${NC}  ${test_file}  (timeout — binary hung)"
			FAIL_COUNT=$((FAIL_COUNT + 1))
			rm -f "${ir_file}" "${obj_file}" "${bin_file}" "${stdout_cap}" "${stderr_cap}"
			return 1
		elif [ $exit_code -ne 0 ]; then
			echo -e "  ${RED}FAIL${NC}  ${test_file}  (runtime exit $exit_code)"
			if [ "$VERBOSE" -eq 1 ]; then
				echo "$run_output" | tail -5 | sed 's/^/    /'
			fi
			FAIL_COUNT=$((FAIL_COUNT + 1))
			rm -f "${ir_file}" "${obj_file}" "${bin_file}" "${stdout_cap}" "${stderr_cap}"
			return 1
		fi

		# --- Exit code 0. ---
		# In leak-check mode we do NOT golden-compare: output carries sanitizer
		# noise and leak correctness (not stdout correctness) is the concern.
		if [ "$LEAK_CHECK" -eq 1 ]; then
			echo -e "  ${GREEN}CLEAN${NC} ${test_file}"
			PASS_COUNT=$((PASS_COUNT + 1))
			rm -f "${ir_file}" "${obj_file}" "${bin_file}" "${stdout_cap}" "${stderr_cap}"
			return 0
		fi

		# --- Golden-output verification (normal mode only) ---
		# Quarantined tests: exit code already checked above; skip golden compare.
		if is_quarantined "${base_name}"; then
			echo -e "  ${GREEN}PASS${NC}  ${test_file}  ${CYAN}(quarantined: exit-code only)${NC}"
			QUARANTINE_COUNT=$((QUARANTINE_COUNT + 1))
			PASS_COUNT=$((PASS_COUNT + 1))
			rm -f "${ir_file}" "${obj_file}" "${bin_file}" "${stdout_cap}" "${stderr_cap}"
			return 0
		fi

		local golden="${GOLDEN_OVERRIDE:-${SCRIPT_DIR}/test_files/${base_name}.expected.out}"

		# --update-goldens: (re)write the golden from current stdout; do not compare.
		# Never writes a golden for a quarantined test (handled above).
		if [ "$UPDATE_GOLDENS" -eq 1 ]; then
			cp "${stdout_cap}" "${SCRIPT_DIR}/test_files/${base_name}.expected.out"
			echo -e "  ${CYAN}WROTE${NC} ${test_file}  (golden updated)"
			PASS_COUNT=$((PASS_COUNT + 1))
			rm -f "${ir_file}" "${obj_file}" "${bin_file}" "${stdout_cap}" "${stderr_cap}"
			return 0
		fi

		# Missing golden: VISIBLE, non-fatal (still exit-code-checked). Never a
		# silent golden pass — once a golden exists, wrong output is fatal below.
		if [ ! -f "${golden}" ]; then
			echo -e "  ${YELLOW}NO GOLDEN${NC}  ${test_file}  (exit-code only; run --update-goldens to create)"
			NOGOLDEN_COUNT=$((NOGOLDEN_COUNT + 1))
			PASS_COUNT=$((PASS_COUNT + 1))
			rm -f "${ir_file}" "${obj_file}" "${bin_file}" "${stdout_cap}" "${stderr_cap}"
			return 0
		fi

		# Exact-match comparison; ONLY normalization is stripping one trailing newline.
		if golden_matches "${stdout_cap}" "${golden}"; then
			echo -e "  ${GREEN}PASS${NC}  ${test_file}"
			GOLDEN_PASS_COUNT=$((GOLDEN_PASS_COUNT + 1))
			PASS_COUNT=$((PASS_COUNT + 1))
			rm -f "${ir_file}" "${obj_file}" "${bin_file}" "${stdout_cap}" "${stderr_cap}"
			return 0
		else
			echo -e "  ${RED}FAIL${NC}  ${test_file}  (stdout does not match golden)"
			local na ng
			na="$(mktemp)"; ng="$(mktemp)"
			strip_one_trailing_nl "${golden}"     > "${ng}"
			strip_one_trailing_nl "${stdout_cap}" > "${na}"
			echo "    --- diff (- expected golden / + actual stdout) ---"
			diff -u "${ng}" "${na}" 2>/dev/null | tail -n +3 | head -30 | sed 's/^/    /'
			rm -f "${na}" "${ng}"
			FAIL_COUNT=$((FAIL_COUNT + 1))
			rm -f "${ir_file}" "${obj_file}" "${bin_file}" "${stdout_cap}" "${stderr_cap}"
			return 1
		fi
	fi

	# Cleanup (only the valgrind branch falls through to here; the normal branch
	# returns explicitly above).
	rm -f "${ir_file}" "${obj_file}" "${bin_file}"
	return 0
}

# --selfcheck: prove the golden comparator has TEETH. Corrupt a real committed
# golden in a TEMP copy only, drive run_one_test with that corrupted golden, and
# assert the suite went red. The committed golden is never mutated. On success
# prints the literal line 'SELFCHECK: OK' and exits non-zero; if the comparator
# fails to detect the corruption (or mutates the committed file), a DISTINCT
# message is printed and 'SELFCHECK: OK' is NOT emitted.
run_selfcheck() {
	echo "==========================================="
	echo " Golden-comparison self-check (teeth proof)"
	echo "==========================================="
	local golden_file="" tb tf gf
	for gf in $(ls "${SCRIPT_DIR}"/test_files/codegen_*.expected.out 2>/dev/null | sort); do
		tb="$(basename "$gf" .expected.out)"
		is_quarantined "$tb" && continue
		if [ -f "${SCRIPT_DIR}/test_files/${tb}.b" ]; then
			golden_file="$gf"; break
		fi
	done
	if [ -z "$golden_file" ]; then
		echo "SELFCHECK: FAILED — no committed non-quarantined golden found to corrupt"
		exit 3
	fi
	tb="$(basename "$golden_file" .expected.out)"
	tf="${SCRIPT_DIR}/test_files/${tb}.b"
	echo "  Using golden: test_files/${tb}.expected.out"

	local before_sum after_sum
	before_sum="$(sha256sum "$golden_file" | awk '{print $1}')"

	# Phase A: the real golden must PASS (harness matches correct output).
	PASS_COUNT=0; FAIL_COUNT=0; TOTAL=0; GOLDEN_OVERRIDE=""
	run_one_test "$tf" 0 >/dev/null 2>&1
	local real_ret=$?

	# Phase B: corrupt a TEMP COPY; the comparison must go RED.
	local tmp_golden
	tmp_golden="$(mktemp)"
	cat "$golden_file" > "$tmp_golden"
	printf '\nSELFCHECK-CORRUPTION-%s\n' "$$" >> "$tmp_golden"
	PASS_COUNT=0; FAIL_COUNT=0; TOTAL=0
	GOLDEN_OVERRIDE="$tmp_golden"
	run_one_test "$tf" 0 >/dev/null 2>&1
	local corrupt_ret=$?
	GOLDEN_OVERRIDE=""
	rm -f "$tmp_golden"

	after_sum="$(sha256sum "$golden_file" | awk '{print $1}')"

	if [ "$before_sum" != "$after_sum" ]; then
		echo "SELFCHECK: FAILED — committed golden was mutated during self-check"
		exit 4
	fi
	if [ "$real_ret" -ne 0 ]; then
		echo "SELFCHECK: FAILED — real golden did not match its own test (harness broken)"
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
if [ "$LEAK_CHECK" -eq 0 ] && [ "$VALGRIND" -eq 0 ] && [ "$UPDATE_GOLDENS" -eq 0 ]; then
echo -e "  ${CYAN}Golden-checked:${NC} $GOLDEN_PASS_COUNT   ${YELLOW}No golden:${NC} $NOGOLDEN_COUNT   ${CYAN}Quarantined:${NC} $QUARANTINE_COUNT"
fi
echo "  Total:   $TOTAL"
echo "==========================================="

if [ $FAIL_COUNT -gt 0 ]; then
	exit 1
fi
exit 0
