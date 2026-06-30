# Worker Pool — BLang concurrency example

A concurrent prime counter that fans work out across green threads connected by
channels. It exercises the concurrency subsystem end to end and serves as an
integration test for it.

## Topology

```
producer (spawn) ──► jobs ──► N workers (spawn) ──► results ──► main (collector)
```

- The **producer** splits `[0, limit)` into fixed-size chunks and sends each
  chunk's start index into the `jobs` channel, then closes it.
- Each **worker** drains `jobs`, counts primes in its chunk, and sends the count
  into `results`. When `jobs` is closed and drained, `recv()` returns `none` and
  the worker exits.
- **main** is the collector: it sums one result per chunk, then `wait_all` joins
  the spawned threads.

## Features exercised

- `spawn { }` — each spawn runs on its own OS thread.
- `chan<T>` — `.send()`, `.recv()` (returns `Option<T>`, matched with
  `some`/`none`), `.close()`; bounded buffer with blocking handoff.
- `sync int` — a counter written from many worker threads under locking.
- `wait_all;` — join all spawned tasks.

## Run

```sh
cd examples/worker_pool
bcc build
./worker_pool
# Found 9592 primes below 100000 using 4 workers over 40 chunks
```

## Test

```sh
./test_worker_pool.sh
```

Builds the example, runs it several times (concurrency bugs are often
intermittent), and checks the prime count is correct and stable.
