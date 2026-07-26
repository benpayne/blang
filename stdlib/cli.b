// stdlib/cli.b — CLI flag/argument parsing (pure BLang on sys.args-style input).
//
// Usage: import cli;   (typically also `import sys;` for sys.args())
//   cli.has_flag(args, "verbose")           -> bool   (--verbose present)
//   cli.flag_value(args, "name", "default") -> string (--name=value, or default)
//   cli.bool_flag(args, "verbose")          -> bool   (present, unless =false)
//   cli.positionals(args)                   -> Array<string>  (non-flag args)
//
// Recognizes: --name, --name=value, -x (short bool), and positionals. args[0]
// (the program name) is skipped.
//
// A STATELESS functional API over the argv array (each query scans args), not a
// parsed Flags struct — returning a multi-field struct whose Array fields are
// populated in a function currently double-frees under codegen's aggregate ARC
// (see known-issues). Iteration uses `for x in args` (for-in, which handles
// string-element refcounts correctly) with an index counter to skip args[0];
// index-based `args[i]` element access has broken string ARC (see known-issues
// "array-element string ARC"). Element helpers take the arg by value-parameter.

// The flag NAME an argument declares, or "" if it is not a flag. `--name`,
// `--name=value`, and `-x` all yield their name; positionals yield "".
fn flag_name_of(string a) -> string {
    if a.starts_with("--") {
        string body = a.substring(2, a.length);
        int eq = body.index_of("=");
        if eq >= 0 {
            return body.substring(0, eq);
        }
        return body;
    }
    if a.starts_with("-") {
        if a.length > 1 {
            return a.substring(1, a.length);
        }
    }
    return "";
}

// The VALUE an argument carries: the text after `=` for `--name=value`, else
// "true" (a bare `--name`/`-x`). Only meaningful when flag_name_of(a) != "".
fn flag_value_of(string a) -> string {
    if a.starts_with("--") {
        string body = a.substring(2, a.length);
        int eq = body.index_of("=");
        if eq >= 0 {
            return body.substring(eq + 1, body.length);
        }
    }
    return "true";
}

// True iff `--name` (or `--name=...`, or `-name`) appears in args (args[0] skip).
pub fn has_flag(Array<string> args, string name) -> bool {
    int idx = 0;
    for x in args {
        if idx > 0 {
            if flag_name_of(x) == name {
                return true;
            }
        }
        idx = idx + 1;
    }
    return false;
}

// Value of `--name=value` (or "true" for a bare `--name`/`-x`), else `fallback`.
pub fn flag_value(Array<string> args, string name, string fallback) -> string {
    int idx = 0;
    for x in args {
        if idx > 0 {
            if flag_name_of(x) == name {
                return flag_value_of(x);
            }
        }
        idx = idx + 1;
    }
    return fallback;
}

// A boolean flag is true when present and not explicitly `--name=false`.
pub fn bool_flag(Array<string> args, string name) -> bool {
    int idx = 0;
    for x in args {
        if idx > 0 {
            if flag_name_of(x) == name {
                return flag_value_of(x) != "false";
            }
        }
        idx = idx + 1;
    }
    return false;
}

// The positional (non-flag) arguments, in order (args[0] skipped).
pub fn positionals(Array<string> args) -> Array<string> {
    Array<string> pos = [];
    int idx = 0;
    for x in args {
        if idx > 0 {
            if flag_name_of(x) == "" {
                pos.push(x);
            }
        }
        idx = idx + 1;
    }
    return pos;
}
