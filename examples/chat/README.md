# Chat — TCP broadcast server example

A small TCP chat server: every line a client sends is relayed verbatim to
every other connected client. It is the first example to combine **all** of
BLang's concurrency pieces in one program:

- the **Selector** event loop (poll-based, runs on a spawned thread) with
  lambda `on_accept`/`on_data` callbacks,
- **spawn** (the selector's own thread),
- a **`chan<int>`** crossing threads — the `/quit` handler runs on the
  selector thread and signals the blocked main thread to shut down,
- the **net stdlib** (`ServerSocket`/`Socket`, accept/read/write/close).

## Usage

```
chat server <port>                 # start the broadcast server
chat client <port> <msg> <reads>   # connect, send <msg> ("-" = nothing),
                                   # then read <reads> broadcast lines
chat quit <port>                   # connect and send "/quit"
```

## Structure

- `Room` — the connected-client registry plus a relay counter. It is mutated
  only from selector callbacks, which all run on the selector thread, so
  access is single-threaded by design.
- `run_server` — binds, wires the callbacks, then blocks on `done.recv()`
  until the `/quit` signal arrives from the selector thread.

## What it shook out

Written (like every example) to stress the compiler with real code. It
immediately exposed a use-after-free in Array-typed **struct field
reassignment** from a local variable (`self.fds = keep` in `Room.leave` —
the field kept an uncounted reference that died with the local), fixed in
`CGStruct.cpp` alongside a release of the previously-held array (which had
silently leaked on every such reassignment).

## Test

`./test_chat.sh` builds the binary and runs a scripted lifecycle: server up,
listener client, sender client, broadcast verified end-to-end, `/quit`,
clean shutdown with correct join/leave/relay accounting.
