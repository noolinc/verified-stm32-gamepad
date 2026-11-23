#!/usr/bin/env bash
# Propose a change using the project commit template:
#   title, type (feature|bug), description, what was tested.
#
#   tools/propose.sh --type feature|bug [--scope <scope>] --title "<title>" \
#     --description "<why, 20+ chars>" --tested "<what was tested>" \
#     [--task <id>] [--kind <kind>] [--justification "<text>"] [--detach] -- <path>...
#
# Nool enforces the same rules: the [commit] template blocks a missing
# description or test note at the seal, and the gp_commit_msg policy blocks a
# push whose title is not "feature: ..." or "bug: ...". This wrapper catches
# mistakes before a knot lands.
set -euo pipefail
type="" scope="" title="" desc="" tested="" extra=()
usage() { sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
while [ $# -gt 0 ]; do
  case "$1" in
    --type) type="$2"; shift 2 ;;
    --scope) scope="$2"; shift 2 ;;
    --title) title="$2"; shift 2 ;;
    --description) desc="$2"; shift 2 ;;
    --tested) tested="$2"; shift 2 ;;
    --task|--kind|--justification) extra+=("$1" "$2"); shift 2 ;;
    --detach) extra+=("$1"); shift ;;
    --) shift; break ;;
    -h|--help) usage ;;
    *) echo "unknown option: $1" >&2; usage ;;
  esac
done
fail() { echo "commit template: $*" >&2; exit 2; }
case "$type" in feature|bug) ;; *) fail "--type must be feature or bug" ;; esac
[ -n "${title// /}" ] || fail "--title is required"
[ "${#desc}" -ge 20 ] || fail "--description must be at least 20 characters"
[ -n "${tested// /}" ] || fail "--tested is required (what was tested)"
[ $# -gt 0 ] || fail "name the changed files after --"
case "$scope" in *[\(\)]*) fail "--scope must not contain parentheses" ;; esac
intent="$type${scope:+($scope)}: $title"
[ "${#intent}" -le 72 ] || fail "title line '$intent' is ${#intent} chars; max 72"
paths=()
for p in "$@"; do paths+=(--path "$p"); done
exec nool propose "${paths[@]}" --intent "$intent" --why "$desc" --test-note "$tested" \
  ${extra[@]+"${extra[@]}"} --solidify
