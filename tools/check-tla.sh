#!/usr/bin/env bash
# Model-check the TLA+ specs in specs/ with TLC.
#   - the real configurations must pass;
#   - the seeded-bug configurations must FAIL (proves the properties have teeth);
#   - the debounce window in the model must match GP_DEBOUNCE_MS in the C code.
set -euo pipefail
cd "$(dirname "$0")/.."
command -v tlc >/dev/null || { echo "tlc not on PATH (TLA+ tools)"; exit 1; }
meta="$(mktemp -d)"; trap 'rm -rf "$meta"' EXIT

c_window=$(sed -n 's/^#define GP_DEBOUNCE_MS \([0-9]*\)U.*/\1/p' core/gp_input.h)
m_window=$(sed -n 's/^ *Window = \([0-9]*\).*/\1/p' specs/Debounce.cfg)
if [ "$c_window" != "$m_window" ]; then
  echo "FAIL Debounce.cfg Window=$m_window but GP_DEBOUNCE_MS=$c_window"; exit 1
fi

run() { tlc -workers auto -metadir "$meta/$1" -config "specs/$1.cfg" "specs/$2.tla" 2>&1; }
status=0
for pair in Debounce:Debounce UsbHid:UsbHid; do
  cfg=${pair%%:*}; spec=${pair##*:}
  if out=$(run "$cfg" "$spec") && grep -q "No error has been found" <<<"$out"; then
    echo "PASS $cfg ($(grep -o '[0-9,]* distinct states' <<<"$out" | tail -1))"
  else
    echo "FAIL $cfg"; grep -E "Error|violated|^State|^/\\\\" <<<"$out" | head -40; status=1
  fi
done
for pair in DebounceBug:Debounce UsbHidNoKeepalive:UsbHid UsbHidNoReinit:UsbHid; do
  cfg=${pair%%:*}; spec=${pair##*:}
  out=$(run "$cfg" "$spec" || true)
  if grep -qE "is violated|were violated" <<<"$out"; then
    echo "PASS $cfg (seeded bug caught: $(grep -oE '(Invariant [A-Za-z]+ is|Temporal properties were) violated' <<<"$out" | head -1))"
  else
    echo "FAIL $cfg: seeded bug was NOT caught"; status=1
  fi
done
exit $status
