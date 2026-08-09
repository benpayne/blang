// nsarc.b — U2 (modules-v2-graph) regression-lock module for the module-prefix
// string-ARC path.
//
// This module is combined by test_codegen.sh as a NAMESPACED module (its own
// namespace scope) and is deliberately kept OUT of every promotion list
// (buffer/collections/cli), so qcc gives it a module PREFIX and emits its
// internal calls as `@nsarc__<fn>` — the exact prefixed codegen configuration
// that the retired `qcc.cpp:303-307` rationale claimed double-freed on an
// internal string-returning call (`has_flag -> flag_name_of`).
//
// The harness ASSERTS the `@nsarc__`-mangled callee is present in the IR (so a
// regression that accidentally promotes this module, taking the prefix-free
// path, fails loudly rather than passing on the wrong config) and runs the
// linked binary under --leak-check against the ASan-instrumented runtime.
//
// U2 is characterization + regression-lock: the double-free no longer
// reproduces (fixed by a post-2026-07-20 return-retain/borrow ARC fix); this
// fixture locks that in so it cannot silently regress, and so U3 can safely
// demote `cli` off the promotion list. See docs/epics/modules-v2-graph/
// design-audit-U2.md §8 and specs/035-module-prefix-codegen-fix/.

// The KEY an argument declares, or "" if it is not a flag. Mirrors cli.b's
// flag_name_of: returns an owned substring temp in one branch and a borrowed
// local string in another — both flow back across the prefix boundary.
fn key_of(string a) -> string {
    if a.starts_with("--") {
        string body = a.substring(2, a.length);
        int eq = body.index_of("=");
        if eq >= 0 {
            return body.substring(0, eq);
        }
        return body;
    }
    return "";
}

// has_flag shape: an internal string-returning call (key_of) whose owned temp
// is consumed by a comparison and dropped — the shape the rationale named.
pub fn lookup(Array<string> args, string want) -> bool {
    for x in args {
        if key_of(x) == want {
            return true;
        }
    }
    return false;
}

// flag_value shape: a namespaced pub fn RETURNING an owned string across the
// prefix boundary, built from an internal string-returning call.
pub fn value_of(Array<string> args, string want, string fallback) -> string {
    for x in args {
        if key_of(x) == want {
            string body = x.substring(2, x.length);
            int eq = body.index_of("=");
            if eq >= 0 {
                return body.substring(eq + 1, body.length);
            }
            return "true";
        }
    }
    return fallback;
}
