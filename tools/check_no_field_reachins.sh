#!/usr/bin/env bash
# check_no_field_reachins.sh — modules-v2-exports done-condition 8 gate.
#
# Greps the given directories for source-level field reach-ins on struct
# types that are (or become) opaque under the modules-v2-exports epic.
# Exits 0 iff no reach-in is found.
#
# NOTE: this check is EXPECTED TO FAIL until workplan units U4/U5 migrate
# the corpus onto the accessor/method surface. It is the target gate, not a
# CI gate for master today. Wire it into CI when U5 lands.
#
# Maintained by: U4 seeds the field list below (stdlib DTOs); U5 extends it
# to the full opaque surface. Each entry is TYPE:FIELD — the pattern matches
# `.FIELD` accesses in .b sources, with a curated per-field variable-name
# heuristic kept deliberately simple; refine per-entry rather than making
# the script clever.
#
# Usage: tools/check_no_field_reachins.sh <dir> [<dir> ...]

set -u

# Opaque-type fields that must not be reached into from consumer source.
# Format: Type:field  (Type is documentation; the grep keys on the field
# access token. Fields with high false-positive risk get their own tighter
# pattern below.)
#
# This is a REAL enumeration, not a wildcard — every entry names a concrete
# field of a struct that is (or becomes) opaque. U4 seeds the stdlib DTO
# surface it migrated `examples/` off of; U5 extends it to imported
# user-struct fields and the remaining stdlib DTOs.
FIELDS="
HttpRequest:method
HttpRequest:path
HttpRequest:body
HttpResponse:status
HttpResponse:content_type
HttpResponse:body
HttpRequestLine:method
HttpRequestLine:path
HttpRequestLine:version
HttpParsedHeaders:keys
HttpParsedHeaders:values
FileInfo:size
Socket:fd
Map:keys
Map:values
Set:items
"

# Scan the given directories; set `fail=1` and print each reach-in found.
# Reads $FIELDS and, for a self-check, an override directory list in $1..$n.
scan_dirs() {
	local rc=0
	local entry field hits
	for entry in $FIELDS; do
		field="${entry#*:}"
		# match `<ident>.<field>` not followed by `(` (a call is a method, fine)
		hits=$(grep -rnE "\\b[a-z_][A-Za-z0-9_]*\\.${field}\\b[^(]" \
			--include='*.b' "$@" 2>/dev/null)
		if [ -n "$hits" ]; then
			echo "REACH-IN (${entry}):"
			echo "$hits"
			rc=1
		fi
	done
	return "$rc"
}

# --selfcheck: prove the gate has teeth. Synthesize a source file that
# reaches into a known opaque field and assert the scan flags it (exit
# non-zero). A gate that cannot fail is not a gate.
if [ "${1:-}" = "--selfcheck" ]; then
	tmp=$(mktemp -d)
	trap 'rm -rf "$tmp"' EXIT
	mkdir -p "$tmp/bad" "$tmp/good"
	# A deliberate reach-in: `req.method` NOT followed by `(` — must be flagged.
	printf 'fn h(net.HttpRequest req) -> string {\n\treturn req.method;\n}\n' \
		> "$tmp/bad/reachin.b"
	if scan_dirs "$tmp/bad" >/dev/null 2>&1; then
		echo "SELFCHECK: FAIL — a planted reach-in was NOT detected" >&2
		exit 1
	fi
	# The accessor form (`req.method()`) must NOT be flagged.
	printf 'fn h(net.HttpRequest req) -> string {\n\treturn req.method();\n}\n' \
		> "$tmp/good/clean.b"
	if ! scan_dirs "$tmp/good" >/dev/null 2>&1; then
		echo "SELFCHECK: FAIL — the accessor form was wrongly flagged" >&2
		exit 1
	fi
	echo "SELFCHECK: OK"
	exit 0
fi

if [ $# -lt 1 ]; then
	echo "usage: $0 <dir> [<dir> ...]" >&2
	echo "       $0 --selfcheck" >&2
	exit 2
fi

if scan_dirs "$@"; then
	echo "OK: no field reach-ins found in: $*"
	exit 0
fi
exit 1
