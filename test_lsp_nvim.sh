#!/bin/bash
#
# Headless-Neovim smoke test for blangd — the only "real editor client" check
# in the suite (everything else is the scripted golden harness). Yellow SKIP
# when nvim is not installed locally; the CI lsp job installs neovim and
# treats a failure as red.
#
# Usage: ./test_lsp_nvim.sh
#        BUILD_DIR=path ./test_lsp_nvim.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
BLANGD="$BUILD_DIR/blangd"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

if ! command -v nvim > /dev/null 2>&1; then
	echo -e "${YELLOW}SKIP${NC}  headless-nvim smoke (nvim not installed)"
	exit 0
fi

if [ ! -x "$BLANGD" ]; then
	echo -e "${RED}Error: blangd not found at $BLANGD${NC}"
	exit 1
fi

out="$(cd "$SCRIPT_DIR" && timeout 60 nvim --headless -u NONE \
	-l ci/nvim_smoke.lua "$BLANGD" 2>&1)"
rc=$?

printf '%s\n' "$out"
if [ $rc -eq 0 ] && printf '%s' "$out" | grep -q "nvim_smoke: OK"; then
	echo -e "${GREEN}PASS${NC}  headless-nvim smoke"
	exit 0
fi
echo -e "${RED}FAIL${NC}  headless-nvim smoke (rc=$rc)"
exit 1
