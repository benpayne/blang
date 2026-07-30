#!/usr/bin/env python3
"""Golden-transcript driver for blangd (python3 stdlib only).

Speaks LSP base-protocol frames to a server subprocess and prints every
message the server sends, one canonical JSON line each, in receipt order.
The transcript is the testable artifact: test_lsp.sh compares it against a
committed golden.

Determinism rules:
  * received messages are re-serialized with json.dumps(sort_keys=True),
    so goldens never depend on the server's key order;
  * the workspace root is scrubbed to ${ROOT} in both directions (fixtures
    say file://${ROOT}/..., transcripts print ${ROOT});
  * fixtures use fixed request ids and explicit wait barriers.

Frame reading is STRICT: any output byte outside a well-formed
Content-Length frame — including trailing garbage at EOF and anything on
stderr — fails the run. This is the permanent tripwire for stdout
pollution from the compiler frontend.

Script format (--script FILE): one JSON step per line ('#' and blank lines
skipped):
  {"send": {...}}         send the message (after ${ROOT} substitution)
  {"open": "rel/path.b"}  didOpen sugar: uri file://ROOT/rel/path.b, text
                          read from the file, languageId blang, version 1
  {"wait_method": "m"}    block until a notification with method m arrives
  {"wait_id": N}          block until the response with id N arrives
  {"expect_exit": N}      drain remaining frames to EOF, require exit code N
"""

import argparse
import json
import os
import subprocess
import sys


class ProtocolError(Exception):
    pass


def read_frame(stream):
    """Read one framed payload. Returns None on clean EOF at a frame
    boundary. Raises ProtocolError on any malformed byte."""
    headers = []
    line = stream.readline()
    if line == b"":
        return None  # clean EOF between frames
    while True:
        if line == b"":
            raise ProtocolError("EOF inside frame headers")
        if not line.endswith(b"\r\n"):
            raise ProtocolError("header line not CRLF-terminated: %r" % line[:80])
        line = line[:-2]
        if line == b"":
            break  # end of headers
        headers.append(line)
        line = stream.readline()

    length = None
    for h in headers:
        if h.startswith(b"Content-Length:"):
            value = h[len(b"Content-Length:"):].strip()
            if not value.isdigit():
                raise ProtocolError("bad Content-Length: %r" % h)
            length = int(value)
    if length is None:
        raise ProtocolError("frame without Content-Length: %r" % headers)

    body = stream.read(length)
    if len(body) != length:
        raise ProtocolError("truncated frame body (%d of %d bytes)" % (len(body), length))
    return body


def substitute_root(value, root):
    if isinstance(value, str):
        return value.replace("${ROOT}", root)
    if isinstance(value, list):
        return [substitute_root(v, root) for v in value]
    if isinstance(value, dict):
        return {k: substitute_root(v, root) for k, v in value.items()}
    return value


class Driver:
    def __init__(self, server, root):
        self.root = root
        self.transcript = []
        self.pending = []  # received but not yet matched by a wait
        self.proc = subprocess.Popen(
            [server], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)

    def fail(self, message):
        self.proc.kill()
        stderr = self.proc.stderr.read()
        sys.stderr.write("lsp_client: FAIL: %s\n" % message)
        if stderr:
            sys.stderr.write("server stderr:\n%s\n" % stderr.decode(errors="replace"))
        sys.exit(1)

    def send(self, msg):
        body = json.dumps(substitute_root(msg, self.root)).encode()
        self.proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        self.proc.stdin.flush()

    def receive_one(self):
        """Read the next frame, record it, and return the parsed message."""
        try:
            body = read_frame(self.proc.stdout)
        except ProtocolError as e:
            self.fail("malformed frame: %s" % e)
        if body is None:
            self.fail("EOF from server while waiting for a message")
        try:
            msg = json.loads(body)
        except ValueError as e:
            self.fail("frame body is not JSON: %s" % e)
        self.transcript.append(msg)
        return msg

    def wait_method(self, method):
        for i, msg in enumerate(self.pending):
            if msg.get("method") == method:
                del self.pending[i]
                return
        while True:
            msg = self.receive_one()
            if msg.get("method") == method:
                return
            self.pending.append(msg)

    def wait_id(self, want_id):
        for i, msg in enumerate(self.pending):
            if "method" not in msg and msg.get("id") == want_id:
                del self.pending[i]
                return
        while True:
            msg = self.receive_one()
            if "method" not in msg and msg.get("id") == want_id:
                return
            self.pending.append(msg)

    def expect_exit(self, code):
        self.proc.stdin.close()
        # Drain every remaining frame; stray bytes at EOF fail in read_frame.
        while True:
            try:
                body = read_frame(self.proc.stdout)
            except ProtocolError as e:
                self.fail("malformed frame while draining: %s" % e)
            if body is None:
                break
            try:
                self.transcript.append(json.loads(body))
            except ValueError as e:
                self.fail("frame body is not JSON: %s" % e)
        actual = self.proc.wait()
        stderr = self.proc.stderr.read()
        if stderr:
            self.fail("server wrote to stderr: %r" % stderr[:400])
        if actual != code:
            self.fail("exit code %d, expected %d" % (actual, code))

    def open_document(self, rel_path):
        path = os.path.join(self.root, rel_path)
        with open(path, "r") as f:
            text = f.read()
        self.send({
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": "file://" + path,
                    "languageId": "blang",
                    "version": 1,
                    "text": text,
                },
            },
        })

    def print_transcript(self):
        for msg in self.transcript:
            line = json.dumps(msg, sort_keys=True)
            print(line.replace(self.root, "${ROOT}"))


def run_sema_fixture(driver, rel_path):
    """Compile one .b file through the live server and render its published
    diagnostics as canonical `file:line:col: severity: message` lines — the
    exact format run_tests.sh's expected-error patterns match against. This
    piggybacks every test_files/fail/sema/ fixture as an LSP regression test."""
    driver.send({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                 "params": {"processId": None, "capabilities": {}}})
    driver.wait_id(1)
    driver.send({"jsonrpc": "2.0", "method": "initialized", "params": {}})
    driver.open_document(rel_path)
    driver.wait_method("textDocument/publishDiagnostics")
    driver.send({"jsonrpc": "2.0", "id": 2, "method": "shutdown"})
    driver.wait_id(2)
    driver.send({"jsonrpc": "2.0", "method": "exit"})
    driver.expect_exit(0)

    published = [m for m in driver.transcript
                 if m.get("method") == "textDocument/publishDiagnostics"]
    if not published:
        driver.fail("no publishDiagnostics received")
    for d in published[-1]["params"]["diagnostics"]:
        severity = "error" if d.get("severity", 1) == 1 else "warning"
        start = d["range"]["start"]
        print("%s:%d:%d: %s: %s" % (rel_path, start["line"] + 1,
                                    start["character"] + 1, severity, d["message"]))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True, help="path to the blangd binary")
    ap.add_argument("--root", default=os.getcwd(),
                    help="workspace root substituted for ${ROOT} (default: cwd)")
    ap.add_argument("--script", help="fixture (.lsp.jsonl) to run")
    ap.add_argument("--sema-fixture", metavar="FILE_B",
                    help="compile one .b file (root-relative) and print its "
                         "diagnostics as file:line:col: error: message lines")
    args = ap.parse_args()
    if bool(args.script) == bool(args.sema_fixture):
        ap.error("exactly one of --script / --sema-fixture is required")

    root = os.path.abspath(args.root)
    driver = Driver(args.server, root)

    if args.sema_fixture:
        return run_sema_fixture(driver, args.sema_fixture)

    with open(args.script, "r") as f:
        for lineno, raw in enumerate(f, 1):
            raw = raw.strip()
            if not raw or raw.startswith("#"):
                continue
            try:
                step = json.loads(raw)
            except ValueError as e:
                driver.fail("bad step at %s:%d: %s" % (args.script, lineno, e))
            if "send" in step:
                driver.send(step["send"])
            elif "open" in step:
                driver.open_document(step["open"])
            elif "wait_method" in step:
                driver.wait_method(step["wait_method"])
            elif "wait_id" in step:
                driver.wait_id(step["wait_id"])
            elif "expect_exit" in step:
                driver.expect_exit(step["expect_exit"])
            else:
                driver.fail("unknown step at %s:%d: %r" % (args.script, lineno, raw))

    if driver.proc.poll() is None:
        driver.fail("script ended without expect_exit and server still running")
    driver.print_transcript()
    return 0


if __name__ == "__main__":
    sys.exit(main())
