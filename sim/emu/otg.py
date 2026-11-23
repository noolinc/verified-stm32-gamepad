"""USB OTG_FS device-mode model (RM0383 ch. 22, Synopsys DWC2 core) plus the
host side of the cable: control transfers on EP0 and interrupt IN on EP1.

Modelled behaviour the firmware depends on:
  * GRSTCTL soft reset / FIFO flushes self-clear; AHBIDL reads 1.
  * Bus reset latches GINTSTS.USBRST; clearing it raises ENUMDNE (full speed).
  * SETUP packets arrive through the RX FIFO as GRXSTSP entries (PKTSTS 6 with
    8 data bytes, then PKTSTS 4); STUP is raised when the "setup done" entry
    is popped. Status/OUT stages arrive as PKTSTS 2 + 3 (XFRC on pop).
  * An IN endpoint transmits once EPENA is set and DIEPTSIZ.XFRSIZ bytes are
    in its FIFO; EP0 packets complete at once, EP1 waits (NAKs) until the
    host has an interrupt URB outstanding. Completion sets DIEPINT.XFRC and
    clears EPENA.
  * DIEPCTL0/DOEPCTL0 STALL ends the current control transfer with a STALL.
"""
from collections import deque

from .periph import AHB2_OTGFS, RCC

GINTSTS, GINTMSK, GRSTCTL, GRXSTSR, GRXSTSP = 0x014, 0x018, 0x010, 0x01C, 0x020
DCFG, DCTL, DSTS, DIEPMSK, DOEPMSK, DAINT, DAINTMSK = 0x800, 0x804, 0x808, 0x810, 0x814, 0x818, 0x81C
USBRST, ENUMDNE, RXFLVL, IEPINT, OEPINT = 1 << 12, 1 << 13, 1 << 4, 1 << 18, 1 << 19
EPENA, EPDIS, SNAK, CNAK, STALL, SD0PID = 1 << 31, 1 << 30, 1 << 27, 1 << 26, 1 << 21, 1 << 28
XFRC, EPDISD, STUP = 1 << 0, 1 << 1, 1 << 3
PKT_OUT_DATA, PKT_OUT_DONE, PKT_SETUP_DONE, PKT_SETUP_DATA = 2, 3, 4, 6
EPIPE, ETIMEDOUT, ESHUTDOWN, EPROTO = -32, -110, -108, -71
EP0_MPS = 64
CONTROL_TIMEOUT_MS = 100


def diepctl(n): return 0x900 + 0x20 * n
def diepint(n): return 0x908 + 0x20 * n
def dieptsiz(n): return 0x910 + 0x20 * n
def dtxfsts(n): return 0x918 + 0x20 * n
def doepctl(n): return 0xB00 + 0x20 * n
def doepint(n): return 0xB08 + 0x20 * n


class ControlTransfer:
    def __init__(self, setup, out_data, done):
        self.setup = bytes(setup)
        self.out_data = bytes(out_data or b"")
        self.done = done
        self.dir_in = bool(setup[0] & 0x80)
        self.wlength = setup[6] | (setup[7] << 8)
        self.data = bytearray()
        self.deadline_ms = None

    def describe(self):
        return self.setup.hex(" ")


class OTG:
    def __init__(self, machine):
        self.m = machine
        self.log = machine.log
        self.regs = {}
        self.rxq = deque()          # [status word, [data words], on_pop]
        self.rx_words = deque()
        self.txfifo = {n: bytearray() for n in range(4)}
        self.connected = False      # DCTL.SDIS cleared by firmware
        self.reset_pending = False
        self.enumerated = False
        self.controls = deque()     # queued ControlTransfer
        self.active = None
        self.ep1_urbs = deque()     # (seq, length, reply)
        self.ep1_packet = None
        self.reports_sent = 0
        self.on_connect = None      # callback when the firmware soft-connects
        self.trace = deque(maxlen=200)
        self._core_reset()

    def ev(self, what):
        self.trace.append(f"{self.m.ms:9.3f} ms  {what}")

    # ---- register file ----
    def _core_reset(self):
        self.regs = {GINTSTS: 0x04000020, DCTL: 0x2, 0x03C: 0x00002000}   # SDIS=1 at reset; CID
        self.rxq.clear()
        self.rx_words.clear()
        for f in self.txfifo.values():
            f.clear()

    def r(self, off):
        return self.regs.get(off, 0)

    def _clocked(self):
        return self.m.rcc.enabled(RCC.AHB2ENR, AHB2_OTGFS)

    def _daint(self):
        v = 0
        for n in range(4):
            if self.r(diepint(n)) & self.r(DIEPMSK):
                v |= 1 << n
            if self.r(doepint(n)) & self.r(DOEPMSK):
                v |= 1 << (16 + n)
        return v

    def _gintsts(self):
        v = self.r(GINTSTS) & (USBRST | ENUMDNE)
        if self.rxq:
            v |= RXFLVL
        daint = self._daint() & self.r(DAINTMSK)
        if daint & 0xFFFF:
            v |= IEPINT
        if daint >> 16:
            v |= OEPINT
        return v

    def read(self, off, size):
        if not self._clocked():
            return 0
        if off >= 0x1000:                                   # any FIFO window: RX data
            return self.rx_words.popleft() if self.rx_words else 0
        if off == GRSTCTL:
            return (self.r(GRSTCTL) & ~0x31) | (1 << 31)    # resets/flushes done, AHB idle
        if off == GINTSTS:
            return self._gintsts()
        if off == DAINT:
            return self._daint()
        if off == DSTS:
            return (3 << 1) if self.enumerated else 0       # ENUMSPD = full speed 48 MHz
        if off in (GRXSTSR, GRXSTSP):
            if not self.rxq:
                return 0
            status, words, on_pop = self.rxq[0]
            if off == GRXSTSP:
                self.rxq.popleft()
                self.rx_words = deque(words)
                if on_pop:
                    on_pop()
            return status
        if any(off == dtxfsts(n) for n in range(4)):
            return 32 - len(self.txfifo[(off - 0x918) // 0x20]) // 4
        return self.r(off)

    def write(self, off, size, val):
        if not self._clocked():
            return
        if off >= 0x1000:
            self._fifo_write((off - 0x1000) // 0x1000, val)
            return
        if off == GRSTCTL:
            if val & 1:
                self._core_reset()
            if val & (1 << 5):                               # TXFFLSH
                num = (val >> 6) & 0x1F
                for n in (range(4) if num == 0x10 else [num]):
                    self.txfifo[n].clear()
            if val & (1 << 4):                               # RXFFLSH
                self.rxq.clear()
                self.rx_words.clear()
            self.regs[GRSTCTL] = val & ~0x31
            return
        if off == GINTSTS:                                   # W1C latched bits
            cleared = val & self.r(GINTSTS) & (USBRST | ENUMDNE)
            self.regs[GINTSTS] = self.r(GINTSTS) & ~cleared
            if cleared & USBRST and self.reset_pending:
                self.reset_pending = False
                self.enumerated = True
                self.regs[GINTSTS] |= ENUMDNE                # reset ends: enumeration done
            return
        if any(off in (diepint(n), doepint(n)) for n in range(4)):
            self.regs[off] = self.r(off) & ~val              # W1C
            return
        if off == DCTL:
            was = self.r(DCTL)
            self.regs[DCTL] = val & ~((1 << 8) | (1 << 10))  # CGINAK/CGONAK are strobes
            if was & 2 and not val & 2 and not self.connected:
                self.connected = True
                if self.on_connect:
                    self.on_connect()
            return
        for n in range(4):
            if off == diepctl(n):
                self._diepctl_write(n, val)
                return
            if off == doepctl(n):
                if val & STALL and n == 0:
                    self._stall()
                self.regs[off] = val & ~(SNAK | CNAK | SD0PID | STALL | (1 << 29))
                return
        self.regs[off] = val

    def _diepctl_write(self, n, val):
        old = self.r(diepctl(n))
        if n == 1:
            self.ev(f"DIEPCTL1 <- {val:#010x} (was {old:#010x})")
        new = val & ~(SNAK | CNAK | SD0PID | (1 << 29) | EPDIS)
        if val & STALL:
            new &= ~STALL
            if n == 0:
                self._stall()
        if val & EPDIS and old & EPENA:
            new &= ~EPENA
            self.regs[diepint(n)] = self.r(diepint(n)) | EPDISD
            self.txfifo[n].clear()
            if n == 1:
                self.ep1_packet = None
        self.regs[diepctl(n)] = new
        if val & EPENA and not old & EPENA:
            self._maybe_packet(n)

    def _xfrsize(self, n):
        return self.r(dieptsiz(n)) & 0x7FFFF

    def _fifo_write(self, n, val):
        self.txfifo[n] += (val & 0xFFFFFFFF).to_bytes(4, "little")
        self._maybe_packet(n)

    def _maybe_packet(self, n):
        ctl = self.r(diepctl(n))
        size = self._xfrsize(n)
        if not ctl & EPENA or len(self.txfifo[n]) < size:
            return
        data = bytes(self.txfifo[n][:size])
        del self.txfifo[n][: (size + 3) // 4 * 4]
        if n == 0:
            self._ep0_in_complete()
            self._control_in_packet(data)
        elif n == 1:
            if not ctl & (1 << 15):                           # USBAEP: endpoint not active
                self.log("[otg] EP1 enabled while not active: dropped")
                return
            self.ep1_packet = data
            self.ev(f"EP1 packet ready {data.hex(' ')}")
            self._ep1_deliver()

    def _ep0_in_complete(self):
        self.regs[diepctl(0)] = self.r(diepctl(0)) & ~EPENA
        self.regs[diepint(0)] = self.r(diepint(0)) | XFRC

    # ---- bus events ----
    def bus_reset(self):
        """Host drives SE0: firmware sees USBRST then ENUMDNE."""
        self.ev("bus reset")
        self.reset_pending = True
        self.enumerated = False
        self.regs[GINTSTS] = self.r(GINTSTS) | USBRST
        self.rxq.clear()
        self.rx_words.clear()
        if self.active:
            self._finish(ESHUTDOWN, b"")
        for t in list(self.controls):
            t.done(ESHUTDOWN, b"")
        self.controls.clear()
        self.ep1_packet = None
        self.ep1_urbs.clear()            # URBs of a previous host connection

    # ---- control transfers (host side) ----
    def submit_control(self, setup, out_data, done):
        self.controls.append(ControlTransfer(setup, out_data, done))
        self._start_next()

    def _start_next(self):
        if self.active or not self.controls:
            return
        t = self.active = self.controls.popleft()
        self.ev(f"SETUP {t.describe()}")
        t.deadline_ms = self.m.ms + CONTROL_TIMEOUT_MS
        self.regs[diepctl(0)] = self.r(diepctl(0)) & ~STALL
        self.regs[doepctl(0)] = self.r(doepctl(0)) & ~STALL
        words = [int.from_bytes(t.setup[0:4], "little"), int.from_bytes(t.setup[4:8], "little")]
        self.rxq.append([(PKT_SETUP_DATA << 17) | (8 << 4), words, None])
        self.rxq.append([PKT_SETUP_DONE << 17, [], self._setup_done])
        if not t.dir_in and t.wlength and t.out_data:
            d = t.out_data[: t.wlength]
            w = [int.from_bytes(d[i:i + 4].ljust(4, b"\0"), "little") for i in range(0, len(d), 4)]
            self.rxq.append([(PKT_OUT_DATA << 17) | (len(d) << 4), w, None])
            self.rxq.append([PKT_OUT_DONE << 17, [], self._out_done])

    def _setup_done(self):
        self.regs[doepint(0)] = self.r(doepint(0)) | STUP

    def _out_done(self):
        self.regs[doepint(0)] = self.r(doepint(0)) | XFRC

    def _control_in_packet(self, data):
        t = self.active
        if t is None:
            self.log(f"[otg] EP0 IN packet with no transfer in progress: {data.hex()}")
            return
        if t.dir_in and t.wlength:
            t.data += data
            if len(data) < EP0_MPS or len(t.data) >= t.wlength:
                # data stage over: host sends the zero-length OUT status packet
                self.rxq.append([(PKT_OUT_DATA << 17), [], None])
                self.rxq.append([PKT_OUT_DONE << 17, [], self._out_done])
                self._finish(0, bytes(t.data[: t.wlength]))
        elif not data:
            self._finish(0, b"")                              # IN status stage (ZLP)
        else:
            self._finish(EPROTO, b"")

    def _stall(self):
        if self.active:
            self._finish(EPIPE, b"")

    def _finish(self, status, data):
        t, self.active = self.active, None
        t.done(status, data)
        self._start_next()

    def tick(self):
        """Called every slice: control-transfer timeouts."""
        t = self.active
        if t and self.m.ms > t.deadline_ms:
            self.log(f"[otg] control transfer {t.describe()} timed out")
            self._finish(ETIMEDOUT, b"")

    # ---- interrupt IN on EP1 ----
    def submit_ep1(self, seq, length, reply):
        self.ev(f"EP1 URB seq={seq}")
        self.ep1_urbs.append((seq, length, reply))
        self._ep1_deliver()

    def unlink_ep1(self, seq):
        for u in list(self.ep1_urbs):
            if u[0] == seq:
                self.ev(f"EP1 URB seq={seq} unlinked")
                self.ep1_urbs.remove(u)
                return True
        return False

    def _ep1_deliver(self):
        if self.ep1_packet is None or not self.ep1_urbs:
            return                                            # device NAKs / host not polling
        seq, length, reply = self.ep1_urbs.popleft()
        data, self.ep1_packet = self.ep1_packet, None
        self.regs[diepctl(1)] = self.r(diepctl(1)) & ~EPENA
        self.regs[diepint(1)] = self.r(diepint(1)) | XFRC
        self.reports_sent += 1
        self.ev(f"EP1 -> URB seq={seq}: {data.hex(' ')}")
        reply(0, data[:length])

    @property
    def address(self):
        return (self.r(DCFG) >> 4) & 0x7F
