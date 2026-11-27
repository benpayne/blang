#!/usr/bin/env python3
"""LSP diagnostic corpus check (python3 stdlib only) — modules-v2-lsp U2.

For every file in the committed corpus list, open it through blangd and assert
that the diagnostics blangd publishes for the document are EXACTLY the
diagnostics of the reference compile: errors and warnings, same severity, line,
column and message (overview done condition 2; spec FR-003/FR-010).

The reference compile for a single file is the `qcc` command line that
`bcc -v -S <file>` prints. The tool runs that line with `--json` appended
(`--json` is a rendering-only flag; U1's project_compile ctest asserts both
renderings carry the same diagnostic set).

Usage:
  lsp_diag_corpus.py --build-dir DIR --root ROOT --corpus stdlib
                     [--list FILE] [--check-list] [--print-qualifying] [--only PATH]

Exit codes: 0 all pass, 1 any FAIL, 2 usage/environment error.
See specs/042-lsp-stdlib-context/contracts/lsp-test-tools.md.
"""

import argparse
import glob
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lsp_client import Driver, server_environment  # noqa: E402  (shared framing)

# The overview's false-diagnostics table (overview done condition 2).
OVERVIEW_FILES = [
    "test_files/codegen_prelude_no_import.b",
    "test_build/myapp/main.b",
    "test_files/codegen_http_routing.b",
    "test_build/counterapp/main.b",
    "examples/kv/main.b",
    "examples/todo_app/main.b",
]


def die(msg, code=2):
    sys.stderr.write("lsp_diag_corpus: %s\n" % msg)
    sys.exit(code)


def reference_compile(build_dir, abs_path):
    """Run the reference compile; return (exit_code, [diagnostic dicts])."""
    tmp = tempfile.mkdtemp(prefix="lsp_corpus_")
    try:
        out = subprocess.run(
            [os.path.join(build_dir, "bcc"), "-v", "-S", abs_path,
             "-o", os.path.join(tmp, "r.ll")],
            cwd=tmp, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        line = None
        for raw in out.stdout.decode(errors="replace").splitlines():
            if "/qcc " in raw:
                line = raw.strip()
                break
        if line is None:
            die("bcc -v -S printed no qcc line for %s" % abs_path)
        argv = shlex.split(line) + ["--json"]
        ref = subprocess.run(argv, cwd=tmp, stdout=subprocess.DEVNULL,
                             stderr=subprocess.PIPE)
        text = ref.stderr.decode(errors="replace").strip()
        diags = json.loads(text) if text else []
        return ref.returncode, diags
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def key_of_reference(d):
    return (d["severity"], int(d["line"]), int(d["col"]), d["message"])


def key_of_lsp(d):
    sev = "error" if d.get("severity", 1) == 1 else "warning"
    start = d["range"]["start"]
    return (sev, start["line"] + 1, start["character"] + 1, d["message"])


class Session:
    """One blangd session reused for every corpus file."""

    def __init__(self, server, root):
        self.driver = Driver(server, root, server_environment(root))
        self.driver.send({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                          "params": {"processId": None,
                                     "rootUri": "file://" + root,
                                     "capabilities": {}}})
        self.driver.wait_id(1)
        self.driver.send({"jsonrpc": "2.0", "method": "initialized", "params": {}})

    def _wait_publish(self, uri):
        d = self.driver
        for i, msg in enumerate(d.pending):
            if (msg.get("method") == "textDocument/publishDiagnostics"
                    and msg["params"]["uri"] == uri):
                del d.pending[i]
                return msg
        while True:
            msg = d.receive_one()
            if (msg.get("method") == "textDocument/publishDiagnostics"
                    and msg["params"]["uri"] == uri):
                return msg
            d.pending.append(msg)

    def diagnostics(self, abs_path):
        uri = "file://" + abs_path
        with open(abs_path, "r") as f:
            text = f.read()
        self.driver.send({"jsonrpc": "2.0", "method": "textDocument/didOpen",
                          "params": {"textDocument": {"uri": uri,
                                                      "languageId": "blang",
                                                      "version": 1,
                                                      "text": text}}})
        published = self._wait_publish(uri)["params"]["diagnostics"]
        self.driver.send({"jsonrpc": "2.0", "method": "textDocument/didClose",
                          "params": {"textDocument": {"uri": uri}}})
        self._wait_publish(uri)  # the clearing publish
        self.driver.transcript = []  # keep memory flat over a long corpus
        return published

    def close(self):
        self.driver.send({"jsonrpc": "2.0", "id": 2, "method": "shutdown"})
        self.driver.wait_id(2)
        self.driver.send({"jsonrpc": "2.0", "method": "exit"})
        self.driver.expect_exit(0)


def read_list(path, corpus):
    entries, seen, dups = [], set(), []
    with open(path, "r") as f:
        for raw in f:
            raw = raw.strip()
            if not raw or raw.startswith("#"):
                continue
            parts = raw.split(None, 1)
            if len(parts) != 2:
                die("malformed list line: %r" % raw)
            kind, rel = parts
            if kind != corpus:
                continue
            if rel in seen:
                dups.append(rel)
            seen.add(rel)
            entries.append(rel)
    return entries, dups


def qualifying(build_dir, root):
    """The files the corpus must contain (overview done condition 2)."""
    files = set(OVERVIEW_FILES)
    files.update(os.path.relpath(p, root)
                 for p in glob.glob(os.path.join(root, "examples", "*", "main.b")))
    for p in sorted(glob.glob(os.path.join(root, "test_files", "codegen_*.b"))):
        rc, _ = reference_compile(build_dir, p)
        if rc == 0:
            files.add(os.path.relpath(p, root))
    return sorted(files)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--corpus", required=True, choices=["stdlib"])
    ap.add_argument("--list")
    ap.add_argument("--check-list", action="store_true")
    ap.add_argument("--print-qualifying", action="store_true")
    ap.add_argument("--only", help="check a single repo-relative path")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    build_dir = os.path.abspath(args.build_dir)
    for tool in ("blangd", "bcc", "qcc"):
        if not os.access(os.path.join(build_dir, tool), os.X_OK):
            die("missing %s in %s (the corpus check needs it; it never skips)"
                % (tool, build_dir))

    if args.print_qualifying:
        for rel in qualifying(build_dir, root):
            print("%s %s" % (args.corpus, rel))
        return 0

    list_path = args.list or os.path.join(root, "test_files", "lsp", "corpus.txt")
    entries, dups = read_list(list_path, args.corpus)
    failures = 0

    if args.check_list:
        for rel in dups:
            print("FAIL list: duplicate entry %s" % rel)
            failures += 1
        listed = set(entries)
        for rel in qualifying(build_dir, root):
            if rel not in listed:
                print("FAIL list: qualifying file missing from %s: %s"
                      % (os.path.relpath(list_path, root), rel))
                failures += 1

    if args.only:
        entries = [args.only]

    session = Session(os.path.join(build_dir, "blangd"), root)
    passed = 0
    for rel in entries:
        abs_path = os.path.join(root, rel)
        rc, ref = reference_compile(build_dir, abs_path)
        outside = [d for d in ref if d.get("file") != abs_path]
        if outside:
            print("FAIL %s: reference diagnostic outside the document: %s:%s:%s %s"
                  % (rel, outside[0].get("file"), outside[0].get("line"),
                     outside[0].get("col"), outside[0].get("message")))
            failures += 1
            continue
        want = sorted(key_of_reference(d) for d in ref)
        got = sorted(key_of_lsp(d) for d in session.diagnostics(abs_path))
        if want == got:
            passed += 1
            print("PASS %s (%d diagnostics)" % (rel, len(want)))
            continue
        failures += 1
        missing = [k for k in want if k not in got]
        extra = [k for k in got if k not in want]
        if missing:
            detail = "missing from blangd: %s:%d:%d: %s: %s" % ((rel, missing[0][1],
                     missing[0][2], missing[0][0], missing[0][3]))
        elif extra:
            detail = "extra in blangd: %s:%d:%d: %s: %s" % ((rel, extra[0][1],
                     extra[0][2], extra[0][0], extra[0][3]))
        else:
            detail = "multiplicity differs (%d reference vs %d blangd)" % (len(want), len(got))
        print("FAIL %s: %s" % (rel, detail))
    session.close()

    print("corpus %s: %d passed, %d failed" % (args.corpus, passed, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
