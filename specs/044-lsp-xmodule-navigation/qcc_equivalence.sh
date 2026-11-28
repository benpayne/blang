#!/usr/bin/env bash
# modules-v2-lsp U4 (T002/T009/T049): byte-for-byte qcc equivalence vs the 043 qcc.
#
# The two shared-compiler changes U4 makes (CompileContext::useCapabilityTypes lifted
# out of qcc.cpp; parser type-reference recording behind a plan flag qcc never sets)
# must not move ONE byte of qcc output. This harness proves it:
#
#   * qcc command lines are taken ONCE, from the LLVM build's bcc (`bcc -v -S` for
#     single files, `bcc build -v` for projects) — the reference compile lines;
#   * each line is replayed against BOTH qcc binaries of a pair (with --parse-only
#     appended for a non-LLVM pair), in a pristine scratch tree, and the exit code,
#     stdout, stderr and every file the run created or changed are compared with cmp;
#   * categories: (a) every test_files/codegen_*.b via its `bcc -v -S` qcc line,
#     (b) every test_files/{pass,fail,fail/sema,cgfail}/*.b under plain qcc,
#     (c) every test_build/* library's `--emit-bmod` line, (d) every test_build/*
#     binary's final qcc line; a per-category count is printed and a 0 count FAILS.
#
# Usage: qcc_equivalence.sh --ref <043 build dir> --new <build dir> [--parse-only]
#                           [--bcc <LLVM build dir with bcc>] [--selfcheck]
set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
exec python3 -I - "$ROOT" "$@" <<'PY'
import argparse, hashlib, os, shlex, shutil, subprocess, sys, tempfile

root = sys.argv[1]
ap = argparse.ArgumentParser(prog="qcc_equivalence.sh")
ap.add_argument("--ref", required=True)
ap.add_argument("--new", required=True)
ap.add_argument("--parse-only", action="store_true")
ap.add_argument("--bcc", default=os.path.join(root, "build"),
                help="LLVM build dir whose bcc prints the reference qcc lines")
ap.add_argument("--selfcheck", action="store_true")
args = ap.parse_args(sys.argv[2:])

ref_qcc = os.path.join(os.path.abspath(args.ref), "qcc")
new_qcc = os.path.join(os.path.abspath(args.new), "qcc")
bcc_dir = os.path.abspath(args.bcc)
bcc = os.path.join(bcc_dir, "bcc")
for p in (ref_qcc, new_qcc, bcc):
    if not os.access(p, os.X_OK):
        sys.exit("qcc_equivalence: missing executable " + p)

work = tempfile.mkdtemp(prefix="u4-equiv-")
home = os.path.join(work, "home")           # fresh bcc cache: every dep line is printed
os.makedirs(home)
env = dict(os.environ, HOME=home, XDG_CACHE_HOME=os.path.join(home, ".cache"))
env.pop("BLANG_STDLIB_DIR", None)

def qcc_lines(output):
    """The qcc command lines bcc -v printed (first token is a path ending in /qcc)."""
    out = []
    for line in output.splitlines():
        toks = line.split()
        if toks and toks[0].endswith("/qcc"):
            out.append(toks)
    return out

# ---------------------------------------------------------------- collect commands
cmds = []   # (category, label, cwd, argv-after-qcc, tree)  tree: dir to snapshot/restore

# (a) codegen_*.b — each file copied into its own scratch dir so the .ll lands there.
a_root = os.path.join(work, "a")
for name in sorted(os.listdir(os.path.join(root, "test_files"))):
    if not (name.startswith("codegen_") and name.endswith(".b")):
        continue
    d = os.path.join(a_root, name[:-2])
    os.makedirs(d)
    shutil.copy(os.path.join(root, "test_files", name), d)
    r = subprocess.run([bcc, "-v", "-S", name], cwd=d, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    lines = qcc_lines(r.stdout)
    for f in os.listdir(d):          # drop bcc's own outputs; the replay regenerates them
        if f != name:
            p = os.path.join(d, f)
            shutil.rmtree(p) if os.path.isdir(p) else os.remove(p)
    if lines:
        cmds.append(("a", name, d, lines[0][1:], d))

# (b) parse/sema fixtures under plain qcc, each in its own scratch dir.
b_root = os.path.join(work, "b")
for sub in ("pass", "fail", "fail/sema", "cgfail"):
    sd = os.path.join(root, "test_files", sub)
    if not os.path.isdir(sd):
        continue
    for name in sorted(os.listdir(sd)):
        if not name.endswith(".b"):
            continue
        d = os.path.join(b_root, sub.replace("/", "_"), name[:-2])
        os.makedirs(d)
        shutil.copy(os.path.join(sd, name), d)
        cmds.append(("b", sub + "/" + name, d, [name], d))

# (c)/(d) test_build projects: a scratch copy, bcc build -v in every project dir.
tb = os.path.join(work, "tb")
shutil.copytree(os.path.join(root, "test_build"), tb, symlinks=True)
seen = set()
for name in sorted(os.listdir(tb)):
    pd = os.path.join(tb, name)
    toml = os.path.join(pd, "blang.toml")
    if not os.path.isfile(toml):
        continue
    r = subprocess.run([bcc, "build", "-v"], cwd=pd, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    for toks in qcc_lines(r.stdout):
        key = (pd, tuple(toks[1:]))
        if key in seen:
            continue
        seen.add(key)
        cat = "c" if "--emit-bmod" in toks else "d"
        cmds.append((cat, name + ": " + " ".join(os.path.basename(t) for t in toks[1:4]), pd, toks[1:], tb))

# Snapshot of every scratch tree as bcc left it (sources + .bmods + .a): each replay
# starts from this exact state.
pristine = os.path.join(work, "pristine")
for t in (a_root, b_root, tb):
    shutil.copytree(t, os.path.join(pristine, os.path.basename(t)), symlinks=True)

def restore(tree):
    shutil.rmtree(tree)
    shutil.copytree(os.path.join(pristine, os.path.relpath(tree, work)), tree, symlinks=True)

def tree_hashes(tree):
    h = {}
    for dp, dn, fn in os.walk(tree):
        for f in fn:
            p = os.path.join(dp, f)
            if os.path.islink(p):
                continue
            with open(p, "rb") as fh:
                h[p] = hashlib.sha256(fh.read()).hexdigest()
    return h

def run(qcc, cwd, argv, tree):
    restore(tree)
    before = tree_hashes(tree)
    full = [qcc] + argv + (["--parse-only"] if args.parse_only else [])
    r = subprocess.run(full, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    after = tree_hashes(tree)
    outs = {}
    for p, hv in after.items():
        if before.get(p) != hv:
            with open(p, "rb") as fh:
                outs[os.path.relpath(p, tree)] = fh.read()
    # Binary paths differ between the pair; normalize only qcc's own path in text.
    norm = lambda b: b.replace(qcc.encode(), b"<qcc>")
    return (r.returncode, norm(r.stdout), norm(r.stderr), outs)

counts = {"a": 0, "b": 0, "c": 0, "d": 0}
for c, *_ in cmds:
    counts[c] += 1
for c in ("a", "b", "c", "d"):
    print("EQUIV(%s): %d inputs" % (c, counts[c]))
zero = [c for c in ("a", "c", "d") if counts[c] == 0]
if zero or counts["b"] == 0:
    print("EQUIV: FAIL — zero inputs collected for categor%s %s" %
          ("y" if len(zero) == 1 else "ies", ",".join(zero or ["b"])))
    sys.exit(1)

flip_done = False
identical = 0
for cat, label, cwd, argv, tree in cmds:
    ref = run(ref_qcc, cwd, argv, tree)
    new = run(new_qcc, cwd, argv, tree)
    if args.selfcheck and not flip_done and ref[3]:
        # Teeth: corrupt one byte of one reference output; the compare must go red.
        k = sorted(ref[3])[0]
        b = bytearray(ref[3][k]); b[0] ^= 0x01
        ref[3][k] = bytes(b)
        flip_done = True
    diffs = []
    if ref[0] != new[0]:
        diffs.append("exit %d vs %d" % (ref[0], new[0]))
    if ref[1] != new[1]:
        diffs.append("stdout")
    if ref[2] != new[2]:
        diffs.append("stderr")
    if sorted(ref[3]) != sorted(new[3]):
        diffs.append("output set %s vs %s" % (sorted(ref[3]), sorted(new[3])))
    else:
        diffs += ["file " + k for k in sorted(ref[3]) if ref[3][k] != new[3][k]]
    if diffs:
        if args.selfcheck:
            print("SELFCHECK: OK (corrupted reference detected at %s: %s)" % (label, "; ".join(diffs)))
            shutil.rmtree(work)
            sys.exit(1)
        print("EQUIV: DIFF (%s) %s: %s" % (cat, label, "; ".join(diffs)))
        print("  (scratch kept: %s)" % work)
        sys.exit(1)
    identical += 1

shutil.rmtree(work)
if args.selfcheck:
    print("SELFCHECK: FAILED — corrupted reference was not detected")
    sys.exit(2)
print("EQUIV: %d/%d identical (%s)" % (identical, len(cmds),
      "parse-only" if args.parse_only else "llvm"))
PY
