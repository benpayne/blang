# Quickstart: Validating U3 (Semantic Pass Skeleton) by hand

Build both configurations, then run the checks below. All are also covered by
`run_tests.sh` / `test_codegen.sh`.

## Build

```bash
cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build build -j"$(nproc)"
cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF && cmake --build build-parse -j"$(nproc)"
```

## 1. Unknown member is now rejected (the core change) — SC-001/SC-002

```bash
cat > /tmp/unk.b <<'EOF'
struct Point { int x; int y; }
fn main() -> int {
	Point p = Point(1, 2);
	return p.nonexistent;
}
EOF
# Today: exits 0 (silent). After U3: located error, non-zero, in BOTH builds.
build/qcc       --parse-only /tmp/unk.b ; echo "llvm  exit=$?"
build-parse/qcc --parse-only /tmp/unk.b ; echo "parse exit=$?"
# Expect one line matching: ^[^:]+\.b:[0-9]+:[0-9]+: error:  naming 'nonexistent'
```

Repeat with an unknown method (`p.frobnicate();`) — same expectation.

## 2. Undefined variable / function still located (both builds) — REQ-006

```bash
build-parse/qcc --parse-only test_files/fail/undefined_var.b ; echo "exit=$?"
# ^[^:]+\.b:[0-9]+:[0-9]+: error: Failed to find symbol 'x'
```

## 3. Valid programs unaffected (no false positives) — SC-005

```bash
build/qcc       --parse-only test_files/pass/method_call.b ; echo "exit=$?"   # 0, silent
build-parse/qcc --parse-only test_files/pass/field_access.b ; echo "exit=$?"  # 0, silent
out=$(build/qcc --parse-only test_files/pass/func_simple.b 2>&1); test -z "$out" && echo "quiet OK"  # SC-007
```

## 4. fail/sema/ suite + harness — SC-003/SC-004

```bash
./run_tests.sh 2>&1 | grep '/fail/sema/'                 # each PASS (diagnostic matched)
BUILD_DIR=build-parse ./run_tests.sh 2>&1 | grep -c '/fail/sema/'
# Self-check: break a pattern, expect a FAIL, then revert
f=test_files/fail/sema/unknown_field.b.expected
cp "$f" /tmp/uf.bak; printf '# mutate\nZZZ_NO_MATCH\n' > "$f"
./run_tests.sh >/dev/null 2>&1; echo "mutated exit=$? (expect 1)"
cp /tmp/uf.bak "$f"; ./run_tests.sh >/dev/null 2>&1; echo "reverted exit=$? (expect 0)"
```

## 5. Gates — SC-005/SC-006

```bash
./run_tests.sh && ./test_codegen.sh ; echo "Gate A exit=$?"        # 162+ / 63, exit 0
BUILD_DIR=build-parse ./run_tests.sh ; echo "Gate B exit=$?"       # exit 0
```

(Counts grow by the number of `fail/sema/` fixtures added; no pre-U3 test
changes verdict.)
