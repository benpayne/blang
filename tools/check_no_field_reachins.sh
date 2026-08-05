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

if [ $# -lt 1 ]; then
	echo "usage: $0 <dir> [<dir> ...]" >&2
	exit 2
fi

# Opaque-type fields that must not be reached into from consumer source.
# Format: Type:field  (Type is documentation; the grep keys on the field
# access token. Fields with high false-positive risk get their own tighter
# pattern below.)
FIELDS="
HttpRequest:method
HttpRequest:path
HttpRequest:body
HttpRequestLine:method
HttpRequestLine:path
HttpRequestLine:version
HttpParsedHeaders:keys
HttpParsedHeaders:values
HttpResponse:status
HttpResponse:body
FileInfo:size
FileInfo:name
"
# U5 extends: imported user-struct fields, remaining stdlib DTOs.

fail=0
for entry in $FIELDS; do
	field="${entry#*:}"
	# match `<ident>.<field>` not followed by `(` (a call is a method, fine)
	hits=$(grep -rnE "\\b[a-z_][A-Za-z0-9_]*\\.${field}\\b[^(]" \
		--include='*.b' "$@" 2>/dev/null)
	if [ -n "$hits" ]; then
		echo "REACH-IN (${entry}):"
		echo "$hits"
		fail=1
	fi
done

if [ "$fail" -eq 0 ]; then
	echo "OK: no field reach-ins found in: $*"
fi
exit "$fail"
