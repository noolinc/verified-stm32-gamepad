# Contributing

Thanks for helping improve the Verified STM32 Gamepad. Bug reports, hardware test results,
fixes and new features are all welcome.

## Two ways to contribute

| | Pull request | Working with Nool (Enterprise) |
|---|---|---|
| **Who it suits** | Occasional contributors and one-off fixes | Regular contributors, teams, AI-agent workflows |
| **What you need** | The tools in the [README](README.md#what-you-need) | A Nool Enterprise licence: **[request one at nool.dev](https://nool.dev)** |
| **How checks run** | You run `tools/check-all.sh`; a maintainer lands your change through Nool | Nool runs every gate on your own proposals, before they reach the history |
| **What you get** | Your change, landed by a maintainer | Policy gates, TLA+ gates, evidence records, recalled findings, task tracking and fleet planning in your own clone |

### Option 1: pull request

1. Fork the repository and create a branch.
2. Make your change and follow the [coding rules](#coding-rules).
3. Run every check:

   ```sh
   tools/check-all.sh            # prints ALL CHECKS PASS
   tools/check-all.sh --no-docker  # if Docker is not available; say so in the PR
   ```

4. Open a pull request. Describe it the way a commit message is written (see
   [below](#change-descriptions)).

A maintainer lands the change through Nool. Nool re-runs every check, plus the coding-rule
policies that `tools/check-all.sh` cannot run without Nool. If a gate refuses the change,
the maintainer posts the refusal on the PR so you can fix it.

### Option 2: work with Nool (Enterprise)

This repository is built on [Nool](https://nool.dev), a software assurance substrate that
turns the project's specifications into enforcement gates; see
[Built on Nool](README.md#built-on-nool-the-specification-is-the-enforcement-layer). To use
it yourself, **request a Nool Enterprise licence at [https://nool.dev](https://nool.dev)**.
It lets you:

- **Get refusals as you work.** Your proposals are checked by the same gates the
  maintainers use: the MISRA-style policies, the TLA+ model check, unit and property tests,
  the firmware size budget and the Docker emulation. You see the exact reason for a refusal
  immediately, not after review.
- **Keep the history.** Clone with the full history: tasks, findings from refused
  attempts, and the evidence attached to every landed change.
- **Run AI coding agents safely.** Agents are held to the same specifications. They
  coordinate through leases, and you can plan parallel work with `nool fleet plan`
  (see the [roadmap](README.md#roadmap-125-µs-latency-planned-as-a-parallel-fleet)).
- **Produce audit evidence.** Export evidence as DSSE / in-toto / SLSA bundles for
  safety and supply-chain reviews.

Once you have a licence, land changes with the project's wrapper, which fills in the
commit template:

```sh
nool code trust                     # once per clone: allow the gate commands in nool.toml
tools/propose.sh --type feature|bug [--scope core] --title "what changed" \
  --description "why, at least 20 characters" --tested "what you ran" -- <files>
```

## Change descriptions

Every change, whether a pull request or a Nool proposal, uses the same format. Nool enforces it.

```
feature(core): short title, at most 72 characters

Nool-Why: what changed and why (at least 20 characters)
Nool-Test-Note: what you ran to test it
```

- **Type:** `feature` or `bug`.
- **Scope:** optional; usually the top-level directory (`core`, `firmware`, `sim`, `emu`, `tla`, `docs`).
- **Test note:** be specific. "Ran tools/check-all.sh: ALL CHECKS PASS; flashed a Black Pill
  and checked buttons 1–16 in the simulator's macOS devices tab" is much more useful than "tested".

## Coding rules

The C code in `core/` and `firmware/` follows a MISRA C:2012 / BARR-C:2018 / Power of 10
subset. The full list is in the [README](README.md#coding-rules). The rules contributors
break most often:

- braces on every `if`/`else`/loop body;
- every `switch` has a `default` and a `break` per case;
- every loop is bounded, and every hardware wait has a timeout;
- no `goto`, no recursion, no heap, no `printf`, no floating point;
- functions are at most 60 lines.

If a rule truly has to be broken, add a deviation comment on or above the line, with the
reason. Reviewers and auditors can then see it:

```c
/* EMB-DEVIATION(EMB-C-001): <why this is necessary> */
```

## Changing the behaviour, models or pins

- **Debounce or report scheduling.** Update the TLA+ model in `specs/` together with the C
  code. `tools/check-tla.sh` fails if, for example, `GP_DEBOUNCE_MS` and the model's `Window`
  disagree. If you add a property, add a seeded-bug configuration that **must fail** too.
- **Pins.** Update `firmware/stm32f411/board.c`, the board model in `sim/emu/periph.py`, and
  the wiring diagram (`python3 tools/gen-wiring-svg.py docs/images/wiring.svg`).
- **New `.c` files.** Regenerate `compile_commands.json` with `tools/gen-compile-commands.py`.

## Hardware test reports

The firmware has not yet been verified on a physical board, so real-hardware results are
especially valuable. Please open an issue with:

- your board (vendor, revision, crystal frequency) and how you flashed it;
- your host OS and version, and what System Information or `lsusb` shows;
- which inputs worked, using the simulator's **macOS devices** tab or a browser gamepad tester.

## Licence

By contributing, you agree that your contributions are licensed under the
[MIT License](LICENSE), copyright Nool, Inc.
