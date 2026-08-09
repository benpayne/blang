#ifndef BLANG_BMOD_FORMAT_H_
#define BLANG_BMOD_FORMAT_H_

// Version of the .bmod interface FORMAT (not of any library's contents).
//
// Deliberately in a standalone, dependency-free header: it is read by both the
// emitter (qcc, which has the whole QLang frontend) and by BuildCache (linked
// into bcc, which has none of it). Putting it in BmodEmitter.h would drag the
// AST headers into the build driver.
//
// Bump this whenever the shape of an emitted .bmod changes. It is salted into
// BuildCache::computeKey, so a bump invalidates every warm cache entry — without
// which a cache written by one compiler is read as an interface by the next, and
// a stale-format .bmod surfaces as a syntax error inside a generated file at a
// consumer's build (the exact P9 experience this epic exists to eliminate).
//
//   1 — pre-versioned: field layout, generic bodies, and (from modules-v2-exports
//       U1) non-generic init/method signatures. No version marker in the file.
//   2 — U2: version marker line, protocol conformance records, `pub table struct`
//       emission order corrected. An unmarked `init`/method is EXPORTED here,
//       because `pub` could not yet be written on impl members.
//   3 — U3: impl members are filtered by `pub` (private by default, D9), and
//       members are emitted with their `pub` marker. From this version onward
//       `pub init(...)` means externally constructible and a bare `init(...)`
//       means declared-but-private. Reading a format-2 file under format-3 rules
//       would invert the meaning of every unmarked member, which is exactly what
//       the version marker exists to prevent.
//   4 — U5: field layout is DROPPED for a non-generic, non-data-contract
//       `pub struct` (emitted as an empty body). A `table`/`@json` struct keeps
//       its real field declarations — its shape IS its data contract (DB columns,
//       JSON keys, D15), so the fields must cross and editing one must move the
//       interface hash. Those retained fields are compiler-facing metadata:
//       source outside the defining module can never NAME a field (a resolution
//       rule, enforced in Sema), even though the declarations are present.
//       Generic structs are unchanged (full layout + all method bodies ship, so
//       consumers can monomorphize). Reading a format-3 file (which shipped
//       layout for every struct) under format-4 rules is harmless for parsing,
//       but the salt still forces a rebuild so a consumer never mixes shapes.
//
// THE MARKER IS VALIDATED ON READ (U3, qcc.cpp). A .bmod whose version differs
// from this constant is REJECTED with a located diagnostic telling the user to
// rebuild the dependency. Until U3 the marker was written but never parsed, so
// the protection claimed above did not exist: a format-2 file read by a
// format-3 compiler would have silently inverted the meaning of every unmarked
// `init`. A file with NO marker is treated as format 1.
//
// NOT every emission fix is a format change. Within U2, protocol declarations
// were reordered ahead of structs so a conformance record naming a user-defined
// protocol is no longer a forward reference. That reorder does not change the
// emitted shape for any case that previously WORKED — files that parsed before
// still parse, byte-for-byte, and the only files whose bytes moved are ones no
// consumer could read at all. The version therefore stays at 2 rather than
// moving to 3 mid-PR (manager ruling, 2026-08-05). Bump when the shape a working
// consumer sees changes; not when a broken emission is repaired.
namespace BlangBmod
{
	static const int kFormatVersion = 4;
}

#endif // BLANG_BMOD_FORMAT_H_
