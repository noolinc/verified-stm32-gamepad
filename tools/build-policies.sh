#!/usr/bin/env bash
# Build the EmbeddedPolicyPack policies against this repo's hardware profile.
# The pack clone is copied, never modified.
#   PACK_DIR=/path/to/EmbeddedPolicyPack tools/build-policies.sh [policy...]
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
pack_src="${PACK_DIR:-$HOME/Research/EmbeddedPolicyPack}"
work="$repo/build/policy-pack"
if [ "$#" -eq 0 ]; then
  set -- emb_hardware_fit emb_resource_bounds emb_c_subset
fi
mkdir -p "$work"
rsync -a --delete --exclude '.git/' --exclude '.nool/' "$pack_src/" "$work/"
cp "$repo/policy/hardware-profile.toml" "$work/hardware-profile.toml"
cd "$work"
# The pack's hardware_fit e2e test asserts the sample profile's wording
# (generic-cortex-m4, 64 KiB RAM, RTOS). Retarget those expectations to ours.
sed -i.bak -e 's/"allows 1024" "generic-cortex-m4"/"allows 2048" "stm32f411ceu6"/' \
  -e 's/"busy-wait delay under an RTOS" src\/task.c "EMB-HW-005" "busy-waits"/"blocking delay in a superloop" src\/task.c "EMB-HW-005" "blocks the main loop"/' \
  tests/e2e/emb_hardware_fit.sh
rm -f tests/e2e/emb_hardware_fit.sh.bak
tools/check.sh "$@"
echo "Install with:"
for p in "$@"; do
  echo "  nool admin plugin install $work/policy-src/$p/$p.toml --policy"
done
echo "  nool admin plugin pin"
