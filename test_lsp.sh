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
NO_PROJECT_CORPUS=0
FILE_ARGS=()

for arg in "$@"; do
	case "$arg" in
		--verbose) VERBOSE=1 ;;
		--update-goldens) UPDATE_GOLDENS=1 ;;
		--selfcheck) SELFCHECK=1 ;;
		--no-project-corpus) NO_PROJECT_CORPUS=1 ;;
		--help)
			echo "Usage: $0 [--verbose] [--update-goldens] [--selfcheck] [fixture...]"
			echo "  --verbose         Show the transcript diff on failure (full)"
			echo "  --update-goldens  Regenerate transcript goldens"
			echo "  --selfcheck       Prove the comparator has teeth: corrupt a TEMP COPY of a"
			echo "                    real golden, require the suite to go red, print"
			echo "                    'SELFCHECK: OK', and exit non-zero. Never mutates a"
			echo "                    committed golden."
			echo "  --no-project-corpus  Skip the U3 project-corpus leg (CI runs it in the"
			echo "                    LLVM build-system job: its reference needs llc)"
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

# --- fail/sema piggyback ---
# Every semantic negative fixture must ALSO surface through the LSP: compile
# it via a live blangd didOpen, render the published diagnostics as canonical
# file:line:col lines, and hold them to the same expected-error pattern
# run_tests.sh enforces (companion .b.expected ERE, else // EXPECT-ERROR:,
# plus the canonical located-error shape). New fail/sema fixtures are covered
# here automatically. Skipped when specific fixtures were requested on the
# command line.
resolve_expected_pattern() {
	local file="$1"
	local expfile="${file}.expected"
	if [ -f "$expfile" ]; then
		grep -m1 -E '^[[:space:]]*[^#[:space:]]' "$expfile" | sed 's/[[:space:]]*$//'
		return
	fi
	local line
	line=$(grep -m1 'EXPECT-ERROR:' "$file" 2>/dev/null)
	if [ -n "$line" ]; then
		printf '%s' "$line" | sed 's/.*EXPECT-ERROR:[[:space:]]*//; s/[[:space:]]*$//'
	fi
}

if [ ${#FILE_ARGS[@]} -eq 0 ] && [ "$UPDATE_GOLDENS" -eq 0 ]; then
	SEMA_FILES=$(find "$SCRIPT_DIR/test_files/fail/sema" -name '*.b' 2>/dev/null | sort)
	if [ -n "$SEMA_FILES" ]; then
		echo ""
		echo "--- fail/sema fixtures via LSP publishDiagnostics ---"
		while IFS= read -r f; do
			[ -z "$f" ] && continue
			TOTAL=$((TOTAL + 1))
			rel="${f#"$SCRIPT_DIR"/}"
			# modules-v2-lsp U2: fixtures that assert no-prelude behavior run via
			# the stdlib-not-found route (test_files/lsp/sema_no_prelude.txt).
			stdlib_args=()
			if grep -v '^[[:space:]]*#' "$SCRIPT_DIR/test_files/lsp/sema_no_prelude.txt" 2>/dev/null \
				| grep -qxF "$rel"; then
				stdlib_args=(--stdlib-path "$SCRIPT_DIR/test_files/lsp/src")
			fi
			out="$(python3 "$DRIVER" --server "$BLANGD" --root "$SCRIPT_DIR" \
				--sema-fixture "$rel" "${stdlib_args[@]}" 2>&1)"
			drc=$?
			if [ $drc -ne 0 ]; then
				echo -e "  ${RED}FAIL${NC}  $rel  (driver failed)"
				printf '%s\n' "$out" | head -5 | sed 's/^/    /'
				FAIL_COUNT=$((FAIL_COUNT + 1))
				continue
			fi
			# Canonical located-error shape (the epic's per-file requirement).
			if ! printf '%s\n' "$out" | grep -qE '^[^:]+\.b:[0-9]+:[0-9]+: error: '; then
				echo -e "  ${RED}FAIL${NC}  $rel  (no canonical error line via LSP)"
				printf '%s\n' "$out" | head -5 | sed 's/^/    /'
				FAIL_COUNT=$((FAIL_COUNT + 1))
				continue
			fi
			pattern="$(resolve_expected_pattern "$f")"
			if [ -n "$pattern" ] && ! printf '%s\n' "$out" | grep -qE "$pattern"; then
				echo -e "  ${RED}FAIL${NC}  $rel  (diagnostics do not match expected pattern)"
				echo "    pattern: $pattern"
				printf '%s\n' "$out" | head -5 | sed 's/^/    /'
				FAIL_COUNT=$((FAIL_COUNT + 1))
				continue
			fi
			echo -e "  ${GREEN}PASS${NC}  $rel"
			PASS_COUNT=$((PASS_COUNT + 1))
		done <<< "$SEMA_FILES"
	fi
fi

# --- modules-v2-lsp U2: diagnostic corpus + latency floor ---
# The corpus leg holds blangd to the reference compile (the qcc line
# `bcc -v -S <file>` prints) over test_files/lsp/corpus.txt: same errors and
# warnings, same severity/line/column/message. It runs in BOTH build modes and
# never skips (a missing bcc/qcc fails it). The latency leg is skipped on a
# sanitizer build, whose timings are not representative.
if [ ${#FILE_ARGS[@]} -eq 0 ] && [ "$UPDATE_GOLDENS" -eq 0 ]; then
	echo ""
	echo "--- LSP diagnostic corpus (tools/lsp_diag_corpus.py --corpus stdlib) ---"
	TOTAL=$((TOTAL + 1))
	corpus_out="$(python3 "$SCRIPT_DIR/tools/lsp_diag_corpus.py" --build-dir "$BUILD_DIR" \
		--root "$SCRIPT_DIR" --corpus stdlib --check-list 2>&1)"
	if [ $? -eq 0 ]; then
		echo -e "  ${GREEN}PASS${NC}  $(printf '%s\n' "$corpus_out" | tail -1)"
		PASS_COUNT=$((PASS_COUNT + 1))
	else
		echo -e "  ${RED}FAIL${NC}  corpus stdlib"
		printf '%s\n' "$corpus_out" | grep -E '^FAIL|corpus |lsp_diag_corpus' | head -20 | sed 's/^/    /'
		FAIL_COUNT=$((FAIL_COUNT + 1))
	fi

	# modules-v2-lsp U3: the PROJECT corpus — blangd vs the project reference
	# compile (the final qcc line `bcc build -v <dir>` prints) for every
	# test_build/* binary, examples/* project and the multi-file fixture, plus the
	# 11 fail/xmodule fixtures as synthesized lib+bin layouts and the dependency
	# interface byte-parity check. The reference needs an LLVM qcc and llc (bcc
	# build archives each library dependency), so it is skipped elsewhere — a FAIL
	# under CI=true (CI runs it in the LLVM build-system job; research R2).
	echo ""
	echo "--- LSP project corpus (tools/lsp_diag_corpus.py --corpus project) ---"
	have_llc=0
	if command -v llc-18 >/dev/null 2>&1 || command -v llc >/dev/null 2>&1; then have_llc=1; fi
	if [ "$NO_PROJECT_CORPUS" -eq 1 ]; then
		echo -e "  ${YELLOW}SKIP${NC}: project corpus (--no-project-corpus; runs in CI build-system)"
	elif [ $have_llc -eq 1 ] && "$BUILD_DIR/qcc" --help 2>&1 | grep -q -- "--emit-ir"; then
		TOTAL=$((TOTAL + 1))
		project_out="$(python3 "$SCRIPT_DIR/tools/lsp_diag_corpus.py" --build-dir "$BUILD_DIR" \
			--root "$SCRIPT_DIR" --corpus project --check-list 2>&1)"
		if [ $? -eq 0 ]; then
			echo -e "  ${GREEN}PASS${NC}  $(printf '%s\n' "$project_out" | tail -1)"
			PASS_COUNT=$((PASS_COUNT + 1))
		else
			echo -e "  ${RED}FAIL${NC}  corpus project"
			printf '%s\n' "$project_out" | grep -E 'FAIL|corpus |lsp_diag_corpus' | head -20 | sed 's/^/    /'
			FAIL_COUNT=$((FAIL_COUNT + 1))
		fi
	elif [ "${CI:-}" = "true" ]; then
		TOTAL=$((TOTAL + 1))
		echo -e "  ${RED}FAIL${NC}  project corpus skipped under CI=true (needs llc + an LLVM qcc)"
		FAIL_COUNT=$((FAIL_COUNT + 1))
	else
		echo -e "  ${YELLOW}SKIP${NC}: project corpus (needs llc + an LLVM qcc; runs in CI build-system)"
	fi

	# modules-v2-lsp U3 (FR-018, SC-003): the git-shim leg. A tripwire `git` is put
	# first on blangd's PATH and the unfetched-git-dependency fixture is opened:
	# blangd must report the dependency as not fetched and must NEVER run git.
	echo ""
	echo "--- LSP git-shim leg (blangd never runs git) ---"
	TOTAL=$((TOTAL + 1))
	shim="$(mktemp -d)"
	printf '#!/bin/sh\ntouch "%s/git-invoked"\nexit 1\n' "$shim" > "$shim/git"
	chmod +x "$shim/git"
	shim_out="$(python3 "$SCRIPT_DIR/tools/lsp_client.py" --server "$BLANGD" --root "$SCRIPT_DIR" \
		--script "$SCRIPT_DIR/test_files/lsp/project_git_unfetched.lsp.jsonl" \
		--server-env "PATH=$shim:$PATH" 2>&1)"
	shim_rc=$?
	shim_hits="$(printf '%s\n' "$shim_out" | grep -o "git dependency 'greet' is not fetched" | wc -l)"
	if [ $shim_rc -ne 0 ]; then
		echo -e "  ${RED}FAIL${NC}  git-shim: session failed"
		printf '%s\n' "$shim_out" | head -5 | sed 's/^/    /'
		FAIL_COUNT=$((FAIL_COUNT + 1))
	elif [ -e "$shim/git-invoked" ]; then
		echo -e "  ${RED}FAIL${NC}  git-shim: blangd invoked git"
		FAIL_COUNT=$((FAIL_COUNT + 1))
	elif [ "$shim_hits" -ne 1 ]; then
		echo -e "  ${RED}FAIL${NC}  git-shim: expected exactly one 'not fetched' diagnostic, got $shim_hits"
		FAIL_COUNT=$((FAIL_COUNT + 1))
	else
		echo -e "  ${GREEN}PASS${NC}  git-shim: one 'not fetched' diagnostic, git never invoked"
		PASS_COUNT=$((PASS_COUNT + 1))
	fi
	rm -rf "$shim"

	# modules-v2-lsp U3 (FR-010, Quality Gate 7): hostile project inputs. Each
	# session must exit 0 with strict framing (lsp_client fails on any stray
	# stdout/stderr byte) and publish at least one diagnostic; the directory case
	# pins the unreadable-blang.toml Warning text (analyze N4).
	echo ""
	echo "--- LSP hostile-input leg (garbage / directory / symlink-loop blang.toml) ---"
	hostile="$(mktemp -d)"
	mkdir -p "$hostile/garbage" "$hostile/dirtoml/blang.toml" "$hostile/symloop"
	python3 -c 'import random,sys; r=random.Random(7); sys.stdout.buffer.write(bytes(r.randrange(256) for _ in range(5*1024*1024)))' \
		> "$hostile/garbage/blang.toml"
	printf '[project]\nname = "symloop"\ntype = "bin"\n\n[deps]\nloop = { path = "../loop" }\n' > "$hostile/symloop/blang.toml"
	ln -s loop "$hostile/loop"
	for p in garbage dirtoml symloop; do
		printf 'import loop;\nfn main() -> int {\n\tint x = "s";\n\treturn x;\n}\n' > "$hostile/$p/main.b"
	done
	for p in garbage dirtoml symloop; do
		TOTAL=$((TOTAL + 1))
		h_out="$(python3 "$SCRIPT_DIR/tools/lsp_client.py" --server "$BLANGD" --root "$hostile" \
			--sema-fixture "$p/main.b" 2>&1)"
		h_rc=$?
		h_n="$(printf '%s\n' "$h_out" | grep -c "^$p/main.b:")"
		h_ok=1
		[ $h_rc -eq 0 ] && [ "$h_n" -ge 1 ] || h_ok=0
		if [ "$p" = dirtoml ] && ! printf '%s\n' "$h_out" | \
			grep -qxF "dirtoml/main.b:1:1: warning: $hostile/dirtoml/blang.toml: cannot read blang.toml"; then
			h_ok=0
		fi
		if [ $h_ok -eq 1 ]; then
			echo -e "  ${GREEN}PASS${NC}  hostile $p: exit 0, strict framing, $h_n diagnostic(s)"
			PASS_COUNT=$((PASS_COUNT + 1))
		else
			echo -e "  ${RED}FAIL${NC}  hostile $p (rc=$h_rc, diagnostics=$h_n)"
			printf '%s\n' "$h_out" | head -5 | sed 's/^/    /'
			FAIL_COUNT=$((FAIL_COUNT + 1))
		fi
	done
	rm -rf "$hostile"

	echo ""
	echo "--- LSP latency floor (tools/lsp_latency.py) ---"
	sanitize="$(grep -m1 '^BLANG_SANITIZE:' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | sed 's/^[^=]*=//')"
	if [ -n "$sanitize" ]; then
		echo -e "  ${YELLOW}SKIP${NC}: latency leg (sanitizer build: $BUILD_DIR)"
	else
		TOTAL=$((TOTAL + 1))
		LATENCY_FILES=(examples/todo_app/main.b test_build/usebox/main.b)
		latency_out="$(python3 "$SCRIPT_DIR/tools/lsp_latency.py" --server "$BLANGD" \
			--root "$SCRIPT_DIR" "${LATENCY_FILES[@]}" 2>&1)"
		if [ $? -eq 0 ]; then
			echo -e "  ${GREEN}PASS${NC}  $latency_out"
			PASS_COUNT=$((PASS_COUNT + 1))
		else
			echo -e "  ${RED}FAIL${NC}  latency"
			printf '%s\n' "$latency_out" | head -10 | sed 's/^/    /'
			FAIL_COUNT=$((FAIL_COUNT + 1))
		fi
	fi
fi

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
