// stdlib/io.b — Shared I/O protocol (reference)
//
// FileOps is the common interface for readable/writable handles
// (files, sockets, etc.). Implementations: File (fs.b), Socket (net.b).
//
// NOTE: Due to a codegen limitation with cross-namespace protocol visibility,
// FileOps is currently duplicated in net.b and fs.b rather than shared from
// this file. This file serves as documentation of the intended shared protocol.
//
// pub protocol FileOps {
//     fn read(self, int max_len) -> string;
//     fn write(self, string data) -> int;
//     fn close(self);
// }
