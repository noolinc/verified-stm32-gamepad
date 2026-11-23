"""Cortex-M4 machine: runs the firmware ELF on Unicorn with MMIO peripherals,
SysTick exceptions and a virtual clock (1 instruction = 1 cycle at 96 MHz)."""
import queue
import struct
import threading

from elftools.elf.elffile import ELFFile
from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc, UcError
from unicorn import arm_const as A

from .periph import ADC, GPIO, RCC, Board, RegisterFile, SysTickSCB
from .otg import OTG

CPU_HZ = 96_000_000
CYCLES_PER_MS = CPU_HZ // 1000
SLICE = 4_000                       # instructions between peripheral/event servicing
TRAMPOLINE = 0x1FFE0000             # exception-return landing pad (mapped, outside the image)
FLASH, FLASH_SIZE = 0x08000000, 512 * 1024
SRAM, SRAM_SIZE = 0x20000000, 128 * 1024
UID_PAGE = 0x1FFF7000
UID = bytes.fromhex("2E0045000F51383031363637")     # 96-bit device ID the firmware reads
SYSTICK_EXC = 15


class FirmwareFault(Exception):
    pass


class Machine:
    def __init__(self, elf_path, log=print):
        self.log = log
        self.elf_path = elf_path
        self.cycles = 0
        self.inbox = queue.Queue()
        self.cond = threading.Condition()
        self.stopping = False
        self.fault = None
        self.in_handler = False
        self._exc_return = False
        self.timers = []                 # (cycle, fn) run on the emulator thread
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        try:
            self.uc.ctl_set_cpu_model(A.UC_CPU_ARM_CORTEX_M4)
        except (AttributeError, UcError):
            pass
        for base, size in ((FLASH, FLASH_SIZE), (SRAM, SRAM_SIZE), (UID_PAGE, 0x1000), (TRAMPOLINE, 0x1000)):
            self.uc.mem_map(base, size)
        self.uc.mem_write(UID_PAGE + 0xA10, UID)
        self.uc.mem_write(TRAMPOLINE, b"\xfe\xe7")      # b .  (never executed: hooked)
        self.uc.hook_add(UC_HOOK_CODE, self._on_trampoline, begin=TRAMPOLINE, end=TRAMPOLINE + 1)

        self.board = Board()
        self.rcc = RCC()
        self.gpio = {p: GPIO(p, self) for p in "ABC"}
        self.adc = ADC(self)
        self.systick = SysTickSCB(self)
        self.otg = OTG(self)
        self._mmio(0x40023800, 0x400, self.rcc)
        self._mmio(0x40007000, 0x400, RegisterFile("PWR"))
        self._mmio(0x40023C00, 0x400, RegisterFile("FLASH"))
        self._mmio(0x40020000, 0x400, self.gpio["A"])
        self._mmio(0x40020400, 0x400, self.gpio["B"])
        self._mmio(0x40020800, 0x400, self.gpio["C"])
        self._mmio(0x40012000, 0x400, self.adc)
        self._mmio(0xE000E000, 0x1000, self.systick)
        self._mmio(0x50000000, 0x20000, self.otg)
        self.symbols = {}
        self._load_elf(elf_path)

    # ---- setup ----
    def _mmio(self, base, size, dev):
        def rd(uc, off, sz, _):
            return dev.read(off, sz) & ((1 << (8 * sz)) - 1)

        def wr(uc, off, sz, val, _):
            dev.write(off, sz, val)

        self.uc.mmio_map(base, size, rd, None, wr, None)

    def _load_elf(self, path):
        with open(path, "rb") as f:
            elf = ELFFile(f)
            for seg in elf.iter_segments():
                if seg["p_type"] == "PT_LOAD" and seg["p_filesz"]:
                    self.uc.mem_write(seg["p_paddr"], seg.data())
            symtab = elf.get_section_by_name(".symtab")
            for s in symtab.iter_symbols():
                if s.name:
                    addr = s["st_value"]
                    if s["st_info"]["type"] == "STT_FUNC":
                        addr &= ~1                            # Thumb bit
                    self.symbols[s.name] = (addr, s["st_size"])
        sp, reset = struct.unpack("<II", self.uc.mem_read(FLASH, 8))
        self.uc.reg_write(A.UC_ARM_REG_SP, sp)
        self.pc = reset & ~1

    def read_symbol(self, name):
        addr, size = self.symbols[name]
        return bytes(self.uc.mem_read(addr, size))

    # ---- time ----
    @property
    def ms(self):
        return self.cycles / CYCLES_PER_MS

    # ---- exceptions (Unicorn has no NVIC: stack the frame by hand) ----
    def _take_exception(self, num):
        uc = self.uc
        regs = [uc.reg_read(r) for r in (A.UC_ARM_REG_R0, A.UC_ARM_REG_R1, A.UC_ARM_REG_R2, A.UC_ARM_REG_R3,
                                         A.UC_ARM_REG_R12, A.UC_ARM_REG_LR)]
        xpsr = uc.reg_read(A.UC_ARM_REG_XPSR) | (1 << 24)
        sp = uc.reg_read(A.UC_ARM_REG_SP)
        new_sp = sp - 32
        if new_sp & 4:
            new_sp -= 4
            xpsr |= 1 << 9
        uc.mem_write(new_sp, struct.pack("<8I", *regs, self.pc | 1, xpsr))
        handler = struct.unpack("<I", uc.mem_read(FLASH + 4 * num, 4))[0]
        uc.reg_write(A.UC_ARM_REG_SP, new_sp)
        uc.reg_write(A.UC_ARM_REG_LR, TRAMPOLINE | 1)
        self.pc = handler & ~1
        self.in_handler = True

    def _on_trampoline(self, uc, address, size, _):
        self._exc_return = True
        uc.emu_stop()

    def _return_from_exception(self):
        uc = self.uc
        sp = uc.reg_read(A.UC_ARM_REG_SP)
        r0, r1, r2, r3, r12, lr, pc, xpsr = struct.unpack("<8I", uc.mem_read(sp, 32))
        for reg, v in ((A.UC_ARM_REG_R0, r0), (A.UC_ARM_REG_R1, r1), (A.UC_ARM_REG_R2, r2),
                       (A.UC_ARM_REG_R3, r3), (A.UC_ARM_REG_R12, r12), (A.UC_ARM_REG_LR, lr)):
            uc.reg_write(reg, v)
        uc.reg_write(A.UC_ARM_REG_SP, sp + 32 + (4 if xpsr & (1 << 9) else 0))
        uc.reg_write(A.UC_ARM_REG_XPSR, xpsr & ~(1 << 9))
        self.pc = pc & ~1
        self.in_handler = False

    def _in_it_block(self):
        x = self.uc.reg_read(A.UC_ARM_REG_XPSR)
        return ((x >> 10) & 0x3F) != 0 or ((x >> 25) & 3) != 0

    # ---- execution ----
    def _exec(self, count):
        self._exc_return = False
        try:
            self.uc.emu_start(self.pc | 1, 0xFFFFFFFF, count=count)
        except UcError as e:
            pc = self.uc.reg_read(A.UC_ARM_REG_PC)
            raise FirmwareFault(f"{e} at PC=0x{pc:08X} (t={self.ms:.3f} ms)") from None
        if self._exc_return:
            self._return_from_exception()
        else:
            self.pc = self.uc.reg_read(A.UC_ARM_REG_PC)
        self.cycles += count

    def step_slice(self):
        self._exec(SLICE)
        while self._in_it_block():       # never stop or interrupt inside an IT block
            self._exec(1)
        if not self.in_handler and self.systick.pending(self.cycles):
            self._take_exception(SYSTICK_EXC)
        self.otg.tick()
        if self.timers:
            due = [t for t in self.timers if t[0] <= self.cycles]
            self.timers = [t for t in self.timers if t[0] > self.cycles]
            for _, fn in sorted(due, key=lambda t: t[0]):
                fn()

    def run(self):
        """Emulator thread: service requests, run slices, wake waiters."""
        try:
            while not self.stopping:
                self._drain()
                self.step_slice()
                with self.cond:
                    self.cond.notify_all()
        except Exception as e:      # noqa: BLE001 - surfaced to the test
            self.fault = e
            self.log(f"[machine] stopped: {e}")
            with self.cond:
                self.cond.notify_all()

    def _drain(self):
        while True:
            try:
                fn, done = self.inbox.get_nowait()
            except queue.Empty:
                return
            try:
                result = fn()
            except Exception as e:  # noqa: BLE001
                result = e
            if done is not None:
                done.put(result)

    def at(self, ms_from_now, fn):
        """Emulator thread: run fn after ms_from_now of virtual time."""
        self.timers.append((self.cycles + int(ms_from_now * CYCLES_PER_MS), fn))

    # ---- cross-thread API ----
    def post(self, fn):
        """Run fn on the emulator thread (fire and forget)."""
        self.inbox.put((fn, None))

    def call(self, fn, timeout=30):
        """Run fn on the emulator thread and return its result."""
        done = queue.Queue()
        self.inbox.put((fn, done))
        r = done.get(timeout=timeout)
        if isinstance(r, Exception):
            raise r
        return r

    def wait_until(self, predicate, virtual_ms=None, real_timeout=60.0):
        """Block until predicate() holds (evaluated on the emulator thread) or
        virtual_ms of emulated time pass. Returns the predicate's last value."""
        import time
        start_ms = self.call(lambda: self.ms)
        deadline = time.monotonic() + real_timeout
        while True:
            if self.fault:
                raise FirmwareFault(str(self.fault))
            ok = self.call(predicate)
            if ok:
                return ok
            if virtual_ms is not None and self.call(lambda: self.ms) - start_ms >= virtual_ms:
                return ok
            if time.monotonic() > deadline:
                raise TimeoutError("real-time timeout waiting for the emulator")
            with self.cond:
                self.cond.wait(0.05)

    def advance(self, virtual_ms):
        """Let virtual_ms of emulated time pass."""
        self.wait_until(lambda: False, virtual_ms=virtual_ms)
