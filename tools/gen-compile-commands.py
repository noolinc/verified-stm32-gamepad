#!/usr/bin/env python3
"""Write compile_commands.json for Nool's per-file C syntax check.

Paths are relative so the file also works in Nool's isolated reify worktree.
Flags are portable (clang/gcc): C11 to match policy/hardware-profile.toml,
freestanding for firmware, plus each tree's include directories.
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
GROUPS = [
    ("core", ["-Icore"]),
    ("tests", ["-Icore", "-Itests"]),
    ("firmware/stm32f411", ["--target=arm-none-eabi", "-mcpu=cortex-m4", "-mthumb",
                            "-ffreestanding", "-Icore", "-Ifirmware/stm32f411"]),
]

entries = []
for folder, flags in GROUPS:
    for src in sorted((ROOT / folder).glob("*.c")):
        rel = src.relative_to(ROOT).as_posix()
        entries.append({
            "directory": ".",
            "file": rel,
            "arguments": ["cc", "-std=c11", *flags, "-c", rel],
        })
(ROOT / "compile_commands.json").write_text(json.dumps(entries, indent=1) + "\n")
print(f"wrote {len(entries)} entries")
