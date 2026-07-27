# kv — file-backed key-value store CLI

A small command-line tool written in BLang: a persistent key-value store kept
in a plain `key=value` text file.

```sh
kv set name ada          # store/overwrite a key
kv get name              # print the value (exit 1 if missing)
kv has name              # exit 0 if present, 1 if not
kv del name              # remove a key (exit 1 if missing)
kv list                  # print all key=value lines
kv keys                  # print just the keys
```

The store path is `--file=<path>` if given, else the `KV_FILE` environment
variable, else `./kv.store`.

## What it exercises

This is the first real program written against the stdlib breadth added by the
toolchain epic — the modules had unit tests but had never been composed into a
working tool:

- **`sys.args()`** — real argv, not a simulated array
- **`cli`** — `flag_value` / `positionals` for `--file=...` + subcommands
- **`env`** — `env.get_or("KV_FILE", ...)` fallback configuration
- **`fs`** — `exists` / `read_all` / `write_all` persistence
- **String processing** — the store format is parsed with `index_of` /
  `substring` / `starts_with` and rebuilt with concatenation
- **`Option<string>`** — `lookup` returns `some(value)`/`none`, consumed with
  exhaustive `match` (misses become exit codes, not sentinel strings)
- **Colocated `test` blocks** — the store logic is pure functions over the
  file content, unit-tested in-file and run with `bcc test`

## Build and run

```sh
cd examples/kv
../../build/bcc build
./kv set greeting hello
./kv get greeting
```

`./test_kv.sh` automates everything: build, every command's output and exit
code, `--file`/`KV_FILE` handling, and the `bcc test` unit suite.

## Compiler work this example drove

Writing it surfaced (and fixed) real issues, in the example-programs tradition:

- **String locals bound from borrowed sources double-freed** — `string b = a;`
  and `string cmd = pos[0];` (a variable copy / array element) created a second
  tracked owner without retaining. This was known-issue #1 ("array-element
  string ARC") and even affected the plain copy case; both now retain at the
  binding site. The natural `pos[0]` CLI style works because of this fix.
- **Sema didn't type `for x in array` loop variables**, so string operations
  over the loop variable (`out = out + line`) were rejected. The loop variable
  now gets the iterable's element type (`Array<T>` → `T`, ranges → `int`).
- **`bcc test` used its own partial stdlib list** (base + collections/timer
  only), so a test file importing `env`/`cli` compiled under `bcc build` but
  failed under `bcc test`. It now uses the same stdlib resolution as the
  normal compile path.
