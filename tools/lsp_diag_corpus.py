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
  lsp_diag_corpus.py --build-dir DIR --root ROOT --corpus stdlib|project
                     [--list FILE] [--check-list] [--print-qualifying] [--only PATH]

--corpus project (modules-v2-lsp U3; contract lsp-test-tools-u3.md): `project
<dir>` entries hold blangd to the PROJECT reference compile — the final qcc
command `bcc build -v <dir>` prints — and `xmodule <fixture>` entries hold it to
`qcc consumer.b lib.bmod` with the fixture synthesized as a two-project layout.
Everything runs in a scratch copy with a scratch XDG_CACHE_HOME (the repository
is never written). Needs an LLVM qcc and llc (bcc build archives each library
dependency), else exit 2.

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
    # The reference tools are the ORACLE, not the system under test: under a
    # sanitizer build their own pre-existing error-exit leak report would land
    # in the stderr parsed as JSON, so leak detection is off for these children
    # only (the same rule U1's project_compile oracle uses). blangd keeps it.
    env = dict(os.environ)
    env["ASAN_OPTIONS"] = "detect_leaks=0"
    try:
        out = subprocess.run(
            [os.path.join(build_dir, "bcc"), "-v", "-S", abs_path,
             "-o", os.path.join(tmp, "r.ll")],
            cwd=tmp, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        line = None
        for raw in out.stdout.decode(errors="replace").splitlines():
            if "/qcc " in raw:
                line = raw.strip()
                break
        if line is None:
            die("bcc -v -S printed no qcc line for %s" % abs_path)
        argv = shlex.split(line) + ["--json"]
        ref = subprocess.run(argv, cwd=tmp, env=env, stdout=subprocess.DEVNULL,
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

    def __init__(self, server, root, extra_env=None):
        self.driver = Driver(server, root, server_environment(root, extra_env))
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


def project_member_with_deps(path):
    """True when `path` is a root source of the nearest-ancestor blang.toml project
    and that project declares [deps] (blangd then compiles it in PROJECT context,
    so its single-file reference no longer applies — U3 research R1)."""
    d = os.path.dirname(os.path.abspath(path))
    while True:
        toml = os.path.join(d, "blang.toml")
        if os.path.exists(toml):
            if os.path.dirname(os.path.abspath(path)) != d:
                return False   # in a subdirectory: not a member (R5)
            in_deps, has = False, False
            with open(toml, errors="replace") as f:
                for line in f:
                    line = line.split("#")[0].strip()
                    if line.startswith("["):
                        in_deps = line == "[deps]"
                    elif in_deps and "=" in line:
                        has = True
            return has
        parent = os.path.dirname(d)
        if parent == d:
            return False
        d = parent


def qualifying(build_dir, root):
    """The files the corpus must contain (overview done condition 2), minus
    project members with dependencies (those are in the project corpus)."""
    files = set(f for f in OVERVIEW_FILES
                if not project_member_with_deps(os.path.join(root, f)))
    files.update(os.path.relpath(p, root)
                 for p in glob.glob(os.path.join(root, "examples", "*", "main.b")))
    for p in sorted(glob.glob(os.path.join(root, "test_files", "codegen_*.b"))):
        rc, _ = reference_compile(build_dir, p)
        if rc == 0:
            files.add(os.path.relpath(p, root))
    return sorted(files)


# ---------------------------------------------------------------------------
# --corpus project (U3)

def toml_type(toml):
    with open(toml, errors="replace") as f:
        for line in f:
            line = line.split("#")[0].strip()
            if line.replace(" ", "").startswith("type="):
                return line.split("=", 1)[1].strip().strip('"')
    return "bin"


def qualifying_project(root):
    """`project` entries: every test_build/* binary, every examples/* project and
    the multi-file fixture; `xmodule` entries: every fail/xmodule fixture."""
    proj = []
    for toml in sorted(glob.glob(os.path.join(root, "test_build", "*", "blang.toml"))):
        if toml_type(toml) == "bin":
            proj.append(os.path.relpath(os.path.dirname(toml), root))
    for toml in sorted(glob.glob(os.path.join(root, "examples", "*", "blang.toml"))):
        proj.append(os.path.relpath(os.path.dirname(toml), root))
    proj.append("test_files/lsp/projects/multifile")
    xm = sorted(os.path.relpath(d, root) for d in
                glob.glob(os.path.join(root, "test_files", "fail", "xmodule", "*"))
                if os.path.isdir(d))
    return sorted(proj), xm


def have_llvm_toolchain(build_dir):
    out = subprocess.run([os.path.join(build_dir, "qcc"), "--help"],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if b"--emit-ir" not in out.stdout:
        return False
    return any(shutil.which(t) for t in ("llc-18", "llc"))


def run(argv, cwd, env):
    return subprocess.run(argv, cwd=cwd, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)


def json_diags(stderr_bytes):
    text = stderr_bytes.decode(errors="replace").strip()
    return json.loads(text) if text else []


def project_reference(build_dir, proj_dir, env):
    """The final qcc line `bcc build -v <dir>` prints, run with --json."""
    out = subprocess.run([os.path.join(build_dir, "bcc"), "build", "-v", proj_dir],
                         cwd=proj_dir, env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT)
    line = None
    for raw in out.stdout.decode(errors="replace").splitlines():
        if "/qcc " in raw:
            line = raw.strip()   # the LAST one: dependencies build first
    if line is None:
        return None, [], out.stdout.decode(errors="replace")[-400:]
    ref = run(shlex.split(line) + ["--json"], proj_dir, env)
    return ref.returncode, json_diags(ref.stderr), ""


def compare(want, got):
    if want == got:
        return None
    missing = [k for k in want if k not in got]
    extra = [k for k in got if k not in want]
    if missing:
        return "missing from blangd: %d:%d: %s: %s" % (missing[0][1], missing[0][2],
                                                      missing[0][0], missing[0][3])
    if extra:
        return "extra in blangd: %d:%d: %s: %s" % (extra[0][1], extra[0][2],
                                                  extra[0][0], extra[0][3])
    return "multiplicity differs (%d reference vs %d blangd)" % (len(want), len(got))


def run_project(args, root, build_dir, list_path):
    if not have_llvm_toolchain(build_dir):
        die("--corpus project needs an LLVM qcc and llc (bcc build archives each "
            "library dependency); run it where they exist (CI build-system)")
    proj_entries, d1 = read_list(list_path, "project")
    xm_entries, d2 = read_list(list_path, "xmodule")
    failures = 0
    if args.check_list:
        for rel in d1 + d2:
            print("FAIL list: duplicate entry %s" % rel)
            failures += 1
        want_p, want_x = qualifying_project(root)
        for rel in want_p:
            if rel not in proj_entries:
                print("FAIL list: qualifying project missing: %s" % rel)
                failures += 1
        for rel in want_x:
            if rel not in xm_entries:
                print("FAIL list: qualifying xmodule fixture missing: %s" % rel)
                failures += 1
    if args.only:
        proj_entries = [e for e in proj_entries if e == args.only]
        xm_entries = [e for e in xm_entries if e == args.only]

    scratch = tempfile.mkdtemp(prefix="lsp_corpus_project_")
    try:
        for sub in ("test_build", "examples", os.path.join("test_files", "lsp", "projects")):
            src = os.path.join(root, sub)
            if os.path.isdir(src):
                shutil.copytree(src, os.path.join(scratch, sub), symlinks=True)
        env = dict(os.environ)
        env.pop("BLANG_STDLIB_DIR", None)
        env["XDG_CACHE_HOME"] = os.path.join(scratch, "cache")
        env["ASAN_OPTIONS"] = "detect_leaks=0"   # the reference tools are the oracle
        session = Session(os.path.join(build_dir, "blangd"), scratch,
                          {"XDG_CACHE_HOME": env["XDG_CACHE_HOME"]})
        passed = 0
        for rel in proj_entries:
            pdir = os.path.join(scratch, rel)
            main_b = os.path.join(pdir, "main.b")
            rc, ref, tail = project_reference(build_dir, pdir, env)
            if rc is None:
                print("FAIL project %s: bcc build -v printed no qcc line: %s" % (rel, tail))
                failures += 1
                continue
            if rc != 0:
                print("FAIL project %s: reference compile exits %d (corpus projects must build)" % (rel, rc))
                failures += 1
                continue
            outside = [d for d in ref if d.get("file") != main_b]
            if outside:   # analyze L4: nothing may hide outside main.b
                print("FAIL project %s: reference diagnostic outside main.b: %s:%s:%s %s"
                      % (rel, outside[0].get("file"), outside[0].get("line"),
                         outside[0].get("col"), outside[0].get("message")))
                failures += 1
                continue
            want = sorted(key_of_reference(d) for d in ref)
            got = sorted(key_of_lsp(d) for d in session.diagnostics(main_b))
            why = compare(want, got)
            if why is None:
                passed += 1
                print("PASS project %s (%d diagnostics)" % (rel, len(want)))
            else:
                failures += 1
                print("FAIL project %s: %s" % (rel, why))

        for rel in xm_entries:
            fx = os.path.join(root, rel)
            base = os.path.join(scratch, "xm", os.path.basename(rel))
            lib, app, refd = (os.path.join(base, n) for n in ("lib", "app", "ref"))
            for d in (lib, app, refd):
                os.makedirs(d)
            shutil.copy(os.path.join(fx, "lib.b"), os.path.join(lib, "lib.b"))
            shutil.copy(os.path.join(fx, "consumer.b"), os.path.join(app, "consumer.b"))
            with open(os.path.join(lib, "blang.toml"), "w") as f:
                f.write('[project]\nname = "lib"\nversion = "0.1.0"\ntype = "lib"\n')
            with open(os.path.join(app, "blang.toml"), "w") as f:
                f.write('[project]\nname = "app"\nversion = "0.1.0"\ntype = "bin"\n\n'
                        '[deps]\nlib = { path = "../lib" }\n')
            qcc = os.path.join(build_dir, "qcc")
            bmod = os.path.join(refd, "lib.bmod")
            run([qcc, "--parse-only", "--emit-bmod", bmod, os.path.join(lib, "lib.b")], refd, env)
            consumer = os.path.join(app, "consumer.b")
            ref = run([qcc, "--parse-only", "--json", consumer, bmod], refd, env)
            ref_err = sorted(key_of_reference(d) for d in json_diags(ref.stderr)
                             if d.get("file") == consumer and d.get("severity") == "error")
            if not ref_err:   # non-vacuous: every xmodule fixture is a negative test
                print("FAIL xmodule %s: the reference reports no error (fixture broken?)" % rel)
                failures += 1
                continue
            pub = session.diagnostics(consumer)
            got_err = sorted(k for k in (key_of_lsp(d) for d in pub) if k[0] == "error")
            extra_warn = [k for k in (key_of_lsp(d) for d in pub) if k[0] == "warning"]
            why = compare(ref_err, got_err)   # missing AND extra errors fail (analyze M4)
            if why is None:
                passed += 1
                note = (" (+%d extra warnings tolerated)" % len(extra_warn)) if extra_warn else ""
                print("PASS xmodule %s (%d errors)%s" % (rel, len(ref_err), note))
            else:
                failures += 1
                print("FAIL xmodule %s: %s" % (rel, why))
        session.close()
        # FR-005: every dependency interface blangd derived equals the .bmod the
        # cold bcc builds above wrote, byte for byte.
        parity = os.path.join(build_dir, "interfaceParityTest")
        if not os.access(parity, os.X_OK):
            print("FAIL interface parity: %s missing" % parity)
            failures += 1
        else:
            pr = run([parity, os.path.join(scratch, "test_build")], scratch, env)
            for line in pr.stdout.decode(errors="replace").splitlines():
                print("  " + line.replace(scratch, "<scratch>"))
            if pr.returncode != 0:
                failures += 1
            else:
                passed += 1
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    print("corpus project: %d passed, %d failed" % (passed, failures))
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--corpus", required=True, choices=["stdlib", "project"])
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

    if args.print_qualifying and args.corpus == "stdlib":
        for rel in qualifying(build_dir, root):
            print("%s %s" % (args.corpus, rel))
        return 0

    list_path = args.list or os.path.join(root, "test_files", "lsp", "corpus.txt")
    if args.corpus == "project":
        if args.print_qualifying:
            p, x = qualifying_project(root)
            for rel in p:
                print("project %s" % rel)
            for rel in x:
                print("xmodule %s" % rel)
            return 0
        return run_project(args, root, build_dir, list_path)
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
