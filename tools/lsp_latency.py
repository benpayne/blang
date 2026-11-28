#!/usr/bin/env python3
"""LSP latency floor (python3 stdlib only) — modules-v2-lsp U2 (REQ-009).

Per file, one blangd session: didOpen, then N full-text didChange edits (edit i
appends "\\n// edit i\\n" — still valid source), each timed from send to that
document's next publishDiagnostics. Prints the median and fails if it exceeds
the budget.

Usage: lsp_latency.py --server BLANGD --root ROOT [--edits 20] [--budget-ms 500] FILE...
Exit codes: 0 every median within budget, 1 otherwise.
The tool never skips; test_lsp.sh skips the leg under a sanitizer build.
"""

import argparse
import os
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lsp_client import Driver, server_environment  # noqa: E402  (shared framing)


def wait_publish(driver, uri):
    while True:
        msg = driver.receive_one()
        if (msg.get("method") == "textDocument/publishDiagnostics"
                and msg["params"]["uri"] == uri):
            return msg


def measure(server, root, rel, edits):
    driver = Driver(server, root, server_environment(root))
    driver.send({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                 "params": {"processId": None, "rootUri": "file://" + root,
                            "capabilities": {}}})
    driver.wait_id(1)
    driver.send({"jsonrpc": "2.0", "method": "initialized", "params": {}})
    path = os.path.join(root, rel)
    uri = "file://" + path
    with open(path, "r") as f:
        base = f.read()
    driver.send({"jsonrpc": "2.0", "method": "textDocument/didOpen",
                 "params": {"textDocument": {"uri": uri, "languageId": "blang",
                                             "version": 1, "text": base}}})
    wait_publish(driver, uri)
    samples = []
    for i in range(1, edits + 1):
        text = base + "\n// edit %d\n" % i
        start = time.monotonic()
        driver.send({"jsonrpc": "2.0", "method": "textDocument/didChange",
                     "params": {"textDocument": {"uri": uri, "version": i + 1},
                                "contentChanges": [{"text": text}]}})
        wait_publish(driver, uri)
        samples.append((time.monotonic() - start) * 1000.0)
        driver.transcript = []
    driver.send({"jsonrpc": "2.0", "id": 2, "method": "shutdown"})
    driver.wait_id(2)
    driver.send({"jsonrpc": "2.0", "method": "exit"})
    driver.expect_exit(0)
    return samples


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--edits", type=int, default=20)
    ap.add_argument("--budget-ms", type=float, default=500.0)
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    ok = True
    for rel in args.files:
        samples = measure(args.server, root, rel, args.edits)
        median = statistics.median(samples)
        verdict = "OK" if median <= args.budget_ms else "OVER BUDGET"
        print("%s: median %.1f ms (min %.1f, max %.1f, n=%d) budget %.0f ms %s"
              % (rel, median, min(samples), max(samples), len(samples),
                 args.budget_ms, verdict))
        ok = ok and median <= args.budget_ms
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
