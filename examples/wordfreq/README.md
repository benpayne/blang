# wordfreq — generic collections example

Counts word frequencies in a text and prints them alphabetically — the
validation program for the generic-ARC work: every value flowing through the
generic collections here is a refcounted string, which previously crashed,
leaked, or heap-corrupted.

```
$ ./wordfreq
barks: 1
brown: 1
dog: 2
fox: 2
...
9 distinct words (3 stop words ignored)
```

## What it exercises

- **`Map<string, int>`** — counting with `get_or` + `set` overwrite
- **`Set<string>`** — stop-word filtering (`has` / `add`)
- **`sort<T>` with a lambda comparator over strings** — the flagship
  previously-broken case (`sort<string>` was documented "NOT yet supported")
- **Generic call inference** — `sort(names, ...)` binds `T = string` from the
  array argument; no explicit `<string>` needed
- **Iterating a generic struct's field** — `for k in counts.keys` resolves the
  element type through the instance's type arguments (`Array<K>` →
  `Array<string>` for a `Map<string, int>`)
- Colocated `test` blocks for tokenize/count/filter/sort, run with `bcc test`

## Build and run

```sh
cd examples/wordfreq
../../build/bcc build
./wordfreq
../../build/bcc test     # unit tests
./test_wordfreq.sh       # build + demo golden + unit tests
```

## Compiler work this example drove

Beyond the generic-ARC unit it validates, writing it surfaced two more fixes:

- **Sema's "concrete-only" field annotation wasn't recursive** — it recorded
  `Array<K>` as concrete, which blocked codegen's instance substitution and
  produced an IR verification failure on `for k in counts.keys`.
- **`getFieldType` / for-in element typing are now instance-aware** — a
  generic struct's field type is mapped through the object's type arguments
  at the caller (`mapTypeForInstance`), so field iterables of generic
  containers type their loop variables correctly.
