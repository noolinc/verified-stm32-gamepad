# STM32F411 USB HID gamepad + macOS simulator

A bare-metal USB gamepad for the **STM32F411 "Black Pill"** that macOS recognises without
drivers, plus a **macOS simulator** that runs the same firmware logic. Every change is gated
by Nool: the [EmbeddedPolicyPack](../EmbeddedPolicyPack) policies, unit and property-based
tests, TLA+ model checking, a cross build with a flash/RAM budget, and a commit template.

```
core/                portable C (firmware and simulator): debounce, HID report, descriptors, EP0 handling
firmware/stm32f411/  register-level OTG FS driver, clock, GPIO/ADC, superloop, linker script
tests/               host unit tests (test_*) and property-based tests (pbt_*)
specs/               TLA+ models (Debounce, UsbHid) and seeded-bug configs
sim/                 SwiftUI simulator (Package.swift at the root)
sim/emu/             full-system emulator: real firmware ELF on an emulated STM32F411, exported over USB/IP
docker/              image that builds the firmware and runs the USB/IP end-to-end test
policy/              hardware profile and commit template
policy-src/          gp_commit_msg policy (Rust → WASM)
tools/               propose wrapper, policy build, TLC check, flash, bundle, size budget
```

## The device

| | |
|---|---|
| USB | Full speed, VID:PID `1209:0001` (pid.codes test ID: change before distributing), serial from the 96-bit UID |
| HID | Generic Desktop / **Game Pad**, one 8-byte input report on EP1 IN: within 1 ms of a change, and repeated every 50 ms (keepalive, see below) |
| Report | 16 buttons · hat (0–7, 8 = centred) · X Y Z Rz (−127…127) · sequence counter |
| Pins | Buttons PB0 PB1 PB3–PB10 PB12–PB15 PA8 PA15 · D-pad PA9 PA10 PC14 PC15 · sticks PA1–PA4 · LED PC13 (all inputs active-low) |
| Clock | 25 MHz crystal → 96 MHz SYSCLK, 48 MHz USB |

macOS binds Game Pad collections to its built-in HID driver, so the pad works with SDL, the
browser Gamepad API and anything using IOHIDManager or CoreHID. `GameController.framework`
only lists controllers it has a profile for, so games that use it alone will not see a generic HID pad.

## Build, test, flash

```sh
cmake --preset host && cmake --build --preset host && ctest --preset host   # unit + PBT
tools/check-tla.sh                                                           # TLC
cmake --preset firmware && cmake --build --preset firmware                   # gamepad.elf / .bin
tools/flash.sh dfu        # hold BOOT0, tap NRST, release BOOT0; or: tools/flash.sh stlink
```

The firmware is freestanding (no C library), built with `arm-none-eabi-gcc -Werror`;
it uses about 6 KB of flash and 4 KB of RAM.

Property-based tests print a shrunk counterexample and a replay line on failure:
`PBT_SEED=<seed> PBT_RUNS=1 build/host/tests/pbt_input`. `PBT_RUNS` raises the case count.

## Full-system simulation in Docker

```sh
tools/docker-sim.sh          # build firmware from source, emulate it, let Linux enumerate it (about 20 s warm)
tools/docker-sim.sh --shell  # same image, interactive
```

The real `gamepad.elf` runs on an emulated Cortex-M4 (Unicorn) with register-level models
of the RCC, PWR, FLASH, GPIO, ADC1, SysTick and the **USB OTG FS core**, written from RM0383.
Clock gating, pull-ups and the OTG RX/TX FIFO protocol are modelled, so firmware mistakes there
show up too. The emulated device is exported over **USB/IP**, and in the same privileged
container `usbip attach` hands it to the **Linux kernel's own USB and HID stack**
(`vhci_hcd`, `usbhid`, evdev).

The test checks:
- **Clocks and connection:** the clock tree is 96/48 MHz and the firmware soft-connects.
- **Kernel enumeration:** the strings, serial, speed and HID class Linux reads.
- **Report descriptor:** the kernel's copy is byte-identical to the firmware's `s_report`.
- **Input path:** evdev codes and ranges for 16 buttons, the sticks and the hat.
- **Debounce:** a 3 ms glitch is rejected and an 8 ms press registers, in virtual time.
- **Reset:** after a detach and reattach with a button held, Linux still sees the held button.

Time is virtual (1 instruction = 1 cycle at 96 MHz), so the emulator runs slower than real time
but timing-dependent firmware behaviour is exact.

**What it found.** For about 50 ms after an application opens the device, Linux `usbhid`
discards input reports (`HID_RESUME_RUNNING`). The original firmware sent reports only when
they changed (SET_IDLE 0), so a button held before the open was never reported. The firmware now
re-sends its state every 50 ms (`GP_TX_KEEPALIVE_MS`). `specs/UsbHid.tla` models the drop
window: `UsbHidNoKeepalive.cfg` reproduces the bug in TLC, and a PBT property guards it too.

## Simulator

```sh
swift run GamepadSim            # or: swift run GamepadSim --demo   (animated input)
swift test                      # simulator tests
tools/bundle-sim.sh             # build/GamepadSim.app
```

- **Virtual pad**: drag the sticks, hold the buttons, or use the keyboard (WASD and IJKL
  sticks, arrow-key D-pad, buttons `Z X C V Q E R F T G Y H B N M Space`). Input goes
  through the firmware's own C core at 1 kHz, so debounce behaves exactly as on the board.
- **Report**: live bytes and bits, decoded through the report descriptor.
- **USB enumeration**: replays a macOS-style enumeration against `core/usb_ctrl.c`.
- **macOS devices**: lists every gamepad macOS sees (the virtual one, or the flashed Black
  Pill) and decodes its reports with that device's own descriptor.
- **Virtual HID device**: the app publishes itself to macOS as "Nool Gamepad (Sim)" through
  CoreHID. This needs the restricted `com.apple.developer.hid.virtual.device` entitlement:
  `SIGN_ID=… PROFILE=….provisionprofile BUNDLE_ID=… tools/bundle-sim.sh`. Without it the
  app shows an orange banner and everything else keeps working.

## Formal model and its link to the code

`specs/Debounce.tla` models the integrator in `gp_input.c` (a stable input is reported
within the window, shorter glitches never register, the output never moves against the
input). `specs/UsbHid.tla` models enumeration, EP1, the report scheduler and a host that opens and
closes the device and drops reports right after each open. Without the keepalive
(`UsbHidNoKeepalive.cfg`), or without the keepalive and the scheduler re-init after a bus reset
(`UsbHidNoReinit.cfg`), the host can be left with a stale state forever. Those configurations
and `DebounceBug.cfg` seed bugs and must fail. `tools/check-tla.sh`
also checks that the model's window equals `GP_DEBOUNCE_MS`.

## Landing changes (Nool)

Use the wrapper; it fills the commit template:

```sh
tools/propose.sh --type feature|bug [--scope core] --title "what changed" \
  --description "why, at least 20 characters" --tested "what you ran" \
  [--task <id>] -- <files>
```

| Template part | Flag | Enforced by |
|---|---|---|
| Title (≤ 72 chars) | `--title` → `--intent "type(scope): title"` | `[commit]` template, at the seal |
| Feature / bug | `--type` | `gp_commit_msg` policy (GP-COMMIT-001), at push |
| Description | `--description` → `--why` (`Nool-Why:`) | template trailer at the seal; ≥ 20 chars GP-COMMIT-002 at push |
| What was tested | `--tested` → `--test-note` (`Nool-Test-Note:`) | template trailer, at the seal |

Nool 8.0.2 gives policies the intent only after the seal, so the type rule runs at
`nool push`. Knots from before the policy are grandfathered by id in
`policy-src/gp_commit_msg/src/lib.rs`.

Every proposal also runs:

| Gate | What it checks |
|---|---|
| `emb_c_subset`, `emb_resource_bounds`, `emb_hardware_fit` | MISRA/BARR-C subset, bounded loops, no heap or recursion, fit to the F411 profile (`policy/hardware-profile.toml`) |
| `host-tests` | clean CMake build, unit tests and property-based tests |
| `tla-model-check` | TLC on both models, seeded bugs must fail |
| `firmware-build` | `-Werror` cross build and flash/RAM budget |
| `sim-tests` | `swift test` |
| `docker-usbip-e2e` | `tools/docker-sim.sh`: real firmware, emulated MCU, Linux USB/HID stack |

After editing `policy/hardware-profile.toml`, rebuild and re-pin the policies:
`tools/build-policies.sh`, then `nool admin plugin install … --policy --no-land`,
`nool admin plugin pin --no-land`, and land `nool.toml` + `policies/` with
`--binary raw --justification …`. Each clone runs `nool code trust` once to allow the
gate commands.

Work is tracked as Nool tasks: `nool task board`.
