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
//       emission order corrected.
//
// Later units in this epic each bump it again: U3 when `pub` filters impl
// members, U5 when field layout is dropped in favour of D15 metadata.
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
	static const int kFormatVersion = 2;
}

#endif // BLANG_BMOD_FORMAT_H_
