#!/usr/bin/env python3
# Vendored from EmbeddedPolicyPack (MIT licence), tools/size_budget.py.
"""Check a firmware image against the flash and RAM budget in hardware-profile.toml.

    python3 tools/size_budget.py build/fw.elf [--profile hardware-profile.toml] [--size arm-none-eabi-size]

Flash is text + data (code, constants and the load image of initialised data);
RAM is data + bss, as `size` reports them in Berkeley format. Sections that
`size` does not count, such as a stack or heap reserved by the linker script
outside .bss, are not included.

Exit 0 under the warn percentage, 0 with a warning between warn and max, and
1 above max or on any error. The thresholds are `[budget]` in the profile
(defaults: warn at 85 %, fail above 100 %). Run it as a blocking
`[[reification.checks]]` command or in CI before recording an attestation.
"""
import argparse
import os
import subprocess
import sys
import tomllib

UNITS = {"k": 1024, "kb": 1024, "kib": 1024, "m": 1024 * 1024, "mb": 1024 * 1024, "mib": 1024 * 1024}


def size_value(v):
    """Bytes from 65536, "64k" or "1_048_576" (the profile's own forms)."""
    if isinstance(v, int):
        return v
    s = str(v).replace("_", "").strip().lower()
    for unit in sorted(UNITS, key=len, reverse=True):
        if s.endswith(unit):
            return int(s[: -len(unit)].strip()) * UNITS[unit]
    return int(s)


def percent(budget, key, default):
    v = int(budget.get(key, default))
    if not 1 <= v <= 100:
        raise ValueError(f"[budget] {key} = {v}: must be 1..100")
    return v


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("elf")
    ap.add_argument("--profile", default="hardware-profile.toml")
    ap.add_argument("--size", default=os.environ.get("SIZE", "arm-none-eabi-size"), help="size tool (default $SIZE or arm-none-eabi-size)")
    a = ap.parse_args(argv)
    try:
        with open(a.profile, "rb") as f:
            prof = tomllib.load(f)
        hw, budget = prof.get("hardware", {}), prof.get("budget", {})
        flash_total, ram_total = size_value(hw["flash_bytes"]), size_value(hw["ram_bytes"])
        limits = {
            "flash": (flash_total, percent(budget, "flash_warn_percent", 85), percent(budget, "flash_max_percent", 100)),
            "RAM": (ram_total, percent(budget, "ram_warn_percent", 85), percent(budget, "ram_max_percent", 100)),
        }
        for name, (_, warn, mx) in limits.items():
            if warn > mx:
                raise ValueError(f"[budget] {name} warn percentage {warn} is above max {mx}")
        out = subprocess.run([a.size, "-B", a.elf], capture_output=True, text=True, check=True).stdout
        rows = [l.split() for l in out.splitlines() if l.strip()]
        text, data, bss = (int(x) for x in rows[1][:3])
    except (OSError, KeyError, ValueError, IndexError, subprocess.CalledProcessError, tomllib.TOMLDecodeError) as e:
        print(f"size-budget: error: {e}", file=sys.stderr)
        return 1

    used = {"flash": text + data, "RAM": data + bss}
    status = 0
    for name, (total, warn, mx) in limits.items():
        u = used[name]
        pct = u * 100 / total
        line = f"{name}: {u} of {total} bytes ({pct:.1f}%), warn at {warn}%, fail above {mx}%"
        if u * 100 > total * mx:
            print(f"FAIL {line}")
            status = 1
        elif u * 100 >= total * warn:
            print(f"WARN {line}")
        else:
            print(f"ok   {line}")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
