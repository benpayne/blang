#!/bin/bash
# Test the full compilation pipeline: BLang source -> LLVM IR -> native binary
# Requires: LLVM dev headers installed and qcc built with -DBLANG_HAS_LLVM
#
# Usage: ./test_codegen.sh [test_file]
#   default test_file: test_files/codegen_simple.c

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
QCC="${SCRIPT_DIR}/build/qcc"
TEST_FILE="${1:-${SCRIPT_DIR}/test_files/codegen_simple.c}"
BASE_NAME="$(basename "${TEST_FILE}" .c)"
IR_FILE="/tmp/${BASE_NAME}.ll"
OBJ_FILE="/tmp/${BASE_NAME}.o"
BIN_FILE="/tmp/${BASE_NAME}"

echo "=== BLang Codegen Test ==="
echo "Source: ${TEST_FILE}"
echo ""

# Step 1: Parse and generate IR
echo "--- Step 1: Parsing and generating LLVM IR ---"
"${QCC}" "${TEST_FILE}" 2>&1

# The .ll file is written next to the source; move it
SRC_LL="$(dirname "${TEST_FILE}")/${BASE_NAME}.ll"
if [ -f "${SRC_LL}" ]; then
	cp "${SRC_LL}" "${IR_FILE}"
elif [ -f "${BASE_NAME}.ll" ]; then
	cp "${BASE_NAME}.ll" "${IR_FILE}"
fi

if [ ! -f "${IR_FILE}" ]; then
	echo "ERROR: No .ll file generated"
	exit 1
fi

echo ""
echo "--- Generated IR ---"
cat "${IR_FILE}"

# Step 2: Compile IR to object file
echo ""
echo "--- Step 2: Compiling IR to object file ---"
llc-18 -filetype=obj "${IR_FILE}" -o "${OBJ_FILE}" 2>&1 || llc -filetype=obj "${IR_FILE}" -o "${OBJ_FILE}" 2>&1
echo "OK: ${OBJ_FILE}"

# Step 3: Link to native binary
echo ""
echo "--- Step 3: Linking to native binary ---"
cc "${OBJ_FILE}" -o "${BIN_FILE}" 2>&1
echo "OK: ${BIN_FILE}"

# Step 4: Run
echo ""
echo "--- Step 4: Running ---"
"${BIN_FILE}"
EXIT_CODE=$?
echo "Exit code: ${EXIT_CODE}"

# Cleanup
rm -f "${IR_FILE}" "${OBJ_FILE}" "${BIN_FILE}"

echo ""
echo "=== Test complete ==="
