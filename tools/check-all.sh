#!/usr/bin/env bash
# Run every automated check locally, without Nool. These are the same commands
# the repository's landing gates run (see [[reification.checks]] in nool.toml).
#   tools/check-all.sh            all checks
#   tools/check-all.sh --no-docker  skip the Docker USB/IP end-to-end test
set -uo pipefail
cd "$(dirname "$0")/.."
docker=1; [ "${1:-}" = "--no-docker" ] && docker=0
fails=()

step() {
  local name="$1"; shift
  printf '\n== %s\n' "$name"
  if "$@"; then echo "-- $name: PASS"; else echo "-- $name: FAIL"; fails+=("$name"); fi
}

step "host unit + property-based tests" sh -c \
  "cmake -S . -B build/gate -G 'Unix Makefiles' >/dev/null && cmake --build build/gate --clean-first -j >/dev/null && ctest --test-dir build/gate --output-on-failure --no-tests=error"
step "TLA+ model checking" tools/check-tla.sh
step "firmware cross build + size budget" sh -c \
  "cmake --preset firmware >/dev/null && cmake --build --preset firmware --clean-first >/dev/null && python3 tools/size_budget.py --profile policy/hardware-profile.toml build/firmware/firmware/stm32f411/gamepad.elf"
if [ "$(uname)" = "Darwin" ]; then step "macOS simulator tests" swift test --quiet; fi
if [ "$docker" = 1 ]; then step "Docker USB/IP end-to-end" tools/docker-sim.sh; fi

echo
if [ "${#fails[@]}" -eq 0 ]; then echo "ALL CHECKS PASS"; else printf 'FAILED: %s\n' "${fails[@]}"; exit 1; fi
