# Contract: stdlib discovery and the stdlib-not-found diagnostic (blangd)

These are blangd's user-facing configuration surface and the one new diagnostic it publishes. It follows design D3 and spec FR-006–FR-009.

## Inputs

| Route | How it is set | Display name (in the warning) |
|---|---|---|
| 1 | `initialize` request: `params.initializationOptions.stdlibPath`, a **string**. A missing value, a non-string or `""` counts as unset. | `initializationOptions.stdlibPath` |
| 2 | Environment variable `BLANG_STDLIB_DIR` in blangd's process environment. Unset or `""` counts as unset. | `BLANG_STDLIB_DIR` |
| 3 | `<directory of the blangd executable>/stdlib`. The executable directory is `readlink /proc/self/exe`, else `argv[0]`'s directory, else `.`; this is the same helper `bcc` uses. | `blangd-relative` |

**Selection.** The lowest-numbered route that is set is selected. It is evaluated once, when `initialize` is handled. Its result is final for the session, and **no other route is consulted**, even if the selected one is unusable.

**Usability.** The selected path must be a readable directory, and `<path>/<name>.b` must be readable for every base module `sys, buffer, collections, fs, net` (`BlangStdlib::baseModuleNames()`). The path is used verbatim, as a prefix only.

## Behavior

- **Usable.** Every document is compiled with the stdlib context of `bcc <file>`. The stdlib files are `selectStdlibFiles( path, scanImports( <buffer text> ) )`, and the document's buffer is the last input. Only diagnostics located in the document are published.
- **Not usable.** Every `textDocument/publishDiagnostics` for every document lists this diagnostic **first**, followed by the document's single-file diagnostics, exactly as before U2:

  ```json
  {"range": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 0}},
   "severity": 2, "code": "stdlib-not-found", "source": "blang",
   "message": "stdlib not found: <route> '<path>' is not a usable stdlib directory (missing <what>); set initializationOptions.stdlibPath or BLANG_STDLIB_DIR, or install stdlib/ next to blangd — this file is checked without the stdlib"}
  ```

  `<what>` is either `directory`, or the first absent base file in base order (e.g. `sys.b`).

## Guarantees

- No process is spawned and no network is used. This is checked at the binary level by `blangd_no_subprocess`, which runs on the U1 script.
- Only `stat`/`access`/read-only opens are performed under `<path>`. Each name appended to it is either a fixed base name or an import identifier matching `[A-Za-z0-9_]+`, so no `/`, `..` or absolute component can be introduced.
- Configuration is never written, and `initializationOptions` keys other than `stdlibPath` are ignored.
