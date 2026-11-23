#!/usr/bin/env bash
# Full-system simulation in Docker: build the firmware from source, run the
# real gamepad.elf on an emulated STM32F411, attach it to the Linux USB/HID
# stack over USB/IP and check enumeration and input events end to end.
#   tools/docker-sim.sh            build + run the end-to-end test
#   tools/docker-sim.sh --shell    build, then open a shell in the container
# Needs Docker with a kernel that has USB/IP vhci_hcd (Docker Desktop does).
set -euo pipefail
cd "$(dirname "$0")/.."
image=gamepad-sim
if ! docker build -q -f docker/Dockerfile -t "$image" . >/dev/null; then
  echo "BuildKit build failed; retrying with the classic builder" >&2
  DOCKER_BUILDKIT=0 docker build -q -f docker/Dockerfile -t "$image" . >/dev/null
fi
if [ "${1:-}" = "--shell" ]; then
  exec docker run --rm -it --privileged --entrypoint bash "$image"
fi
# --privileged: usbip attach writes vhci_hcd's sysfs attach file and the test
# creates /dev/input nodes for the new gamepad.
exec docker run --rm --privileged --name gamepad-sim-$$ "$image"
