#!/usr/bin/env bash
# Build and flash the Black Pill.
#   tools/flash.sh dfu     USB DFU: hold BOOT0, tap NRST, release BOOT0 (needs dfu-util)
#   tools/flash.sh stlink  ST-Link on SWD (needs st-flash from stlink)
set -euo pipefail
cd "$(dirname "$0")/.."
cmake --preset firmware >/dev/null
cmake --build --preset firmware
bin=build/firmware/firmware/stm32f411/gamepad.bin
python3 "${PACK_DIR:-$HOME/Research/EmbeddedPolicyPack}/tools/size_budget.py" \
  --profile policy/hardware-profile.toml build/firmware/firmware/stm32f411/gamepad.elf
case "${1:-dfu}" in
  dfu) dfu-util -a 0 -d 0483:df11 -s 0x08000000:leave -D "$bin" ;;
  stlink) st-flash --reset write "$bin" 0x08000000 ;;
  *) echo "usage: $0 [dfu|stlink]" >&2; exit 2 ;;
esac
