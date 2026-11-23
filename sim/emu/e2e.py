"""End-to-end test: the real firmware ELF on the emulated STM32F411, enumerated
by the Linux USB + HID stack over USB/IP, checked through sysfs and evdev.

Run inside the privileged container (tools/docker-sim.sh)."""
import glob
import os
import subprocess
import sys
import threading
import time

import evdev
from evdev import ecodes as E

from .machine import Machine
from .usbip import BUSID, StubHost, USBIPServer

ELF = os.environ.get("FIRMWARE_ELF", "/sim/gamepad.elf")
VID, PID = "1209", "0001"
BTN0 = E.BTN_GAMEPAD                     # Game Pad buttons 1-16 -> 0x130..0x13F
results = []


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))
    print(f"  {'PASS' if ok else 'FAIL'}  {name}" + (f"  ({detail})" if detail else ""), flush=True)
    return ok


def sh(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=60)


def axis_expected(raw, center=2048, dz=96):
    """core/gp_input.c gp_axis_scale(), integer arithmetic."""
    d = raw - center
    mag, span = abs(d), (center if d < 0 else 4095 - center)
    if mag <= dz:
        return 0
    v = min(127, (mag - dz) * 127 // (span - dz))
    return -v if d < 0 else v


def wait_for(fn, timeout=20.0, step=0.1):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        v = fn()
        if v:
            return v
        time.sleep(step)
    return None


def read_file(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return None


def find_usb_device():
    for d in glob.glob("/sys/bus/usb/devices/*"):
        if (read_file(d + "/idVendor") or b"").strip() == VID.encode() and \
           (read_file(d + "/idProduct") or b"").strip() == PID.encode():
            return d
    return None


def find_event_node():
    for ev in glob.glob("/sys/class/input/event*"):
        name = (read_file(ev + "/device/name") or b"").decode().strip()
        phys_dev = os.path.realpath(ev)
        if "Gamepad" in name and "vhci" in phys_dev:
            node = "/dev/input/" + os.path.basename(ev)
            if not os.path.exists(node):
                major, minor = map(int, read_file(ev + "/dev").decode().split(":"))
                os.makedirs("/dev/input", exist_ok=True)
                os.mknod(node, 0o600 | 0o020000, os.makedev(major, minor))
            return node, name
    return None


class Events:
    """Collects evdev events in a background thread."""

    def __init__(self, dev):
        self.dev, self.items, self.lock = dev, [], threading.Lock()
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        try:
            for e in self.dev.read_loop():
                if e.type in (E.EV_KEY, E.EV_ABS):
                    with self.lock:
                        self.items.append((e.type, e.code, e.value))
        except OSError:
            pass

    def take(self, settle=0.4):
        time.sleep(settle)          # USB/IP + kernel delivery latency (real time)
        with self.lock:
            out, self.items = self.items, []
        return out


def attach():
    r = sh("usbip", "attach", "-r", "127.0.0.1", "-b", BUSID)
    return r.returncode == 0, (r.stderr or r.stdout).strip()


def detach_all():
    out = sh("usbip", "port").stdout
    for line in out.splitlines():
        if line.startswith("Port "):
            sh("usbip", "detach", "-p", line.split()[1].rstrip(":"))


def main():
    print(f"== firmware: {ELF}", flush=True)
    m = Machine(ELF, log=lambda s: print(s, flush=True))
    threading.Thread(target=m.run, daemon=True).start()

    print("== boot", flush=True)
    m.wait_until(lambda: m.otg.connected, virtual_ms=2000)
    clocks = m.call(m.rcc.clocks_ok)
    check("firmware programs SYSCLK 96 MHz and USB 48 MHz", clocks == {"sysclk": 96_000_000, "usb": 48_000_000}, clocks)
    check("firmware soft-connects to USB (DCTL.SDIS cleared)", m.call(lambda: m.otg.connected), f"t={m.ms:.1f} ms")
    ticks0 = m.call(lambda: m.adc.conversions)
    m.advance(10)
    convs = m.call(lambda: m.adc.conversions) - ticks0
    check("1 kHz superloop samples 4 ADC channels per tick", 36 <= convs <= 44, f"{convs} conversions in 10 virtual ms")

    print("== stub host enumeration (exporting side)", flush=True)
    stub = StubHost(m)
    dev, cfg = stub.enumerate()
    check("GET_DESCRIPTOR(device) through the OTG FIFOs", dev[:2] == bytes([18, 1]) and dev[8:12] == bytes([0x09, 0x12, 0x01, 0x00]), dev.hex(" "))
    check("SET_ADDRESS programs DCFG.DAD", m.call(lambda: m.otg.address) == 2, f"DAD={m.call(lambda: m.otg.address)}")
    led = lambda: m.call(lambda: m.gpio["C"].output_level(13))
    check("LED off before SET_CONFIGURATION", led() == 1)

    server = USBIPServer(m, stub, log=lambda s: print(s, flush=True))
    server.start()
    listing = sh("usbip", "list", "-r", "127.0.0.1").stdout
    check("usbip list shows the exported gamepad", "1209:0001" in listing, listing.strip().splitlines()[2] if len(listing.splitlines()) > 2 else listing)

    print("== Linux enumeration over USB/IP (vhci_hcd + usbhid)", flush=True)
    detach_all()
    ok, msg = attach()
    if not check("usbip attach", ok, msg):
        return finish(m, server)
    usb = wait_for(find_usb_device)
    if not check("kernel enumerates 1209:0001", usb, usb):
        return finish(m, server)
    product = (read_file(usb + "/product") or b"").decode().strip()
    serial = (read_file(usb + "/serial") or b"").decode().strip()
    check("product string", product == "Nool STM32 Gamepad", product)
    check("manufacturer string", (read_file(usb + "/manufacturer") or b"").decode().strip() == "Nool")
    check("serial = 96-bit UID in hex", serial == "2E0045000F51383031363637", serial)
    check("full speed (12 Mb/s)", (read_file(usb + "/speed") or b"").strip() == b"12")
    check("interface class HID", (read_file(usb + f"/{os.path.basename(usb)}:1.0/bInterfaceClass") or b"").strip() == b"03")
    check("SET_CONFIGURATION reached the firmware (LED on)", wait_for(lambda: led() == 0, 10))
    hid = wait_for(lambda: glob.glob(f"/sys/bus/hid/devices/0003:{VID.upper()}:{PID.upper()}.*"))
    desc = read_file(hid[0] + "/report_descriptor") if hid else None
    fw_desc = m.read_symbol("s_report")
    check("kernel's report descriptor == firmware s_report", desc == fw_desc, f"{len(desc or b'')} bytes")

    node = wait_for(find_event_node)
    if not check("input device created", node, node):
        return finish(m, server)
    dev = evdev.InputDevice(node[0])
    caps = dev.capabilities(absinfo=True)
    keys = set(caps.get(E.EV_KEY, []))
    absinfo = dict(caps.get(E.EV_ABS, []))
    check("16 gamepad buttons (BTN_GAMEPAD..+15)", set(range(BTN0, BTN0 + 16)) <= keys)
    for code, name in ((E.ABS_X, "X"), (E.ABS_Y, "Y"), (E.ABS_Z, "Z"), (E.ABS_RZ, "Rz")):
        a = absinfo.get(code)
        check(f"axis {name} range -127..127", a and a.min == -127 and a.max == 127, a)
    check("hat as ABS_HAT0X/Y", E.ABS_HAT0X in absinfo and E.ABS_HAT0Y in absinfo)
    ev = Events(dev)
    ev.take(1.0)

    print("== input scenarios (virtual time on the emulated GPIO/ADC)", flush=True)
    b = m.board

    def press(i, ms):
        def go():
            b.buttons |= 1 << i
            m.at(ms, lambda: setattr(b, "buttons", b.buttons & ~(1 << i)))
        m.call(go)

    press(1, 3)
    m.advance(30)
    got = [e for e in ev.take() if e[0] == E.EV_KEY]
    check("3 ms contact glitch is debounced away", not got, got)
    press(1, 8)
    m.advance(30)
    got = [e for e in ev.take() if e[0] == E.EV_KEY]
    check("8 ms press -> press and release", got == [(E.EV_KEY, BTN0 + 1, 1), (E.EV_KEY, BTN0 + 1, 0)], got)

    mapping_ok, seen = True, []
    for i in range(16):
        press(i, 10)
        m.advance(25)
        got = [e for e in ev.take(0.25) if e[0] == E.EV_KEY]
        seen.append(got)
        mapping_ok &= got == [(E.EV_KEY, BTN0 + i, 1), (E.EV_KEY, BTN0 + i, 0)]
    check("buttons 1-16 -> BTN_GAMEPAD+0..15", mapping_ok, "" if mapping_ok else seen)

    raw = [4095, 0, 3500, 600]
    m.call(lambda: setattr(b, "axes", list(raw)))
    m.advance(10)
    got = {c: v for t, c, v in ev.take() if t == E.EV_ABS}
    want = {E.ABS_X: 127, E.ABS_Y: -127, E.ABS_Z: axis_expected(3500), E.ABS_RZ: axis_expected(600)}
    check("sticks: ADC counts -> scaled axes", all(got.get(k) == v for k, v in want.items()), f"got {got}, want {want}")
    m.call(lambda: setattr(b, "axes", [2048 + 80, 2048 - 80, 2048, 2048]))
    m.advance(10)
    got = {c: v for t, c, v in ev.take() if t == E.EV_ABS}
    check("centre noise inside the deadzone reads 0", got.get(E.ABS_X) == 0 and got.get(E.ABS_Y) == 0, got)

    m.call(lambda: setattr(b, "dpad", {"up", "right"}))
    m.advance(15)
    got = {c: v for t, c, v in ev.take() if t == E.EV_ABS}
    check("d-pad up+right -> hat (1, -1)", got.get(E.ABS_HAT0X) == 1 and got.get(E.ABS_HAT0Y) == -1, got)
    m.call(lambda: setattr(b, "dpad", {"up", "down"}))
    m.advance(15)
    got = {c: v for t, c, v in ev.take() if t == E.EV_ABS}
    check("opposite directions cancel -> hat centred", got.get(E.ABS_HAT0X) == 0 and got.get(E.ABS_HAT0Y) == 0, got)
    m.call(lambda: setattr(b, "dpad", set()))
    m.advance(15)
    ev.take()

    print("== bus reset: detach, then reattach while a button is held (specs/UsbHid.tla)", flush=True)
    m.call(lambda: setattr(b, "buttons", 1 << 2))
    m.advance(20)
    check("held button 3 reported before reset", (E.EV_KEY, BTN0 + 2, 1) in ev.take())
    dev.close()
    detach_all()
    wait_for(lambda: not find_usb_device(), 10)
    m.advance(20)
    ok, msg = attach()
    check("re-attach", ok, msg)
    node = wait_for(find_event_node)
    if node:
        t_open = m.ms
        dev2 = evdev.InputDevice(node[0])
        t_opened = m.ms
        ev2 = Events(dev2)
        active = wait_for(lambda: BTN0 + 2 in dev2.active_keys(), 10)
        if not active:
            print(f"    open() spanned t={t_open:.3f}..{t_opened:.3f} virtual ms", flush=True)
            print("    events on the new node:", ev2.take(0.5), flush=True)
            print("    input devices:", [(os.path.basename(e), (read_file(e + '/device/name') or b'').decode().strip(), (read_file(e + '/dev') or b'').decode().strip()) for e in glob.glob('/sys/class/input/event*')], flush=True)
        check("after re-enumeration the held state is delivered again", active, dev2.active_keys())
        if not active:
            print("    diagnostics:", m.call(lambda: diagnostics(m)), flush=True)
            for line in list(m.otg.trace)[-40:]:
                print("     ", line)
        dev2.close()
    detach_all()

    linux_setups = sorted(set(server.urb_log))
    print(f"== control requests Linux sent over USB/IP: {len(server.urb_log)} ({len(linux_setups)} distinct)")
    for s in linux_setups:
        print("   ", s)
    print(f"== {m.otg.reports_sent} input reports delivered; {m.ms:.0f} virtual ms emulated")
    return finish(m, server)


def diagnostics(m):
    """Firmware and endpoint state for a failed delivery check."""
    tx = m.read_symbol("s_tx")              # gp_tx_t: last[8], since_ms u16, seq u8, have_last bool
    o = m.otg
    return {
        "node": find_event_node(),
        "s_tx.last": tx[:8].hex(" "), "s_tx.seq": tx[10], "s_tx.have_last": tx[11],
        "DIEPCTL1": hex(o.r(0x920)), "ep1_packet": o.ep1_packet.hex(" ") if o.ep1_packet else None,
        "ep1_urbs": len(o.ep1_urbs), "reports_sent": o.reports_sent,
        "usb_state(s_ctx)": m.read_symbol("s_ctx")[4:9].hex(" "),
        "s_configured": m.read_symbol("s_configured").hex(), "s_link_change": m.read_symbol("s_link_change").hex(),
    }


def finish(m, server):
    m.stopping = True
    failed = [r for r in results if not r[1]]
    print(f"\n{len(results) - len(failed)}/{len(results)} checks passed" + (f"; FAILED: {[f[0] for f in failed]}" if failed else ""))
    if m.fault:
        print(f"emulator fault: {m.fault}")
    return 1 if failed or m.fault else 0


if __name__ == "__main__":
    sys.exit(main())
