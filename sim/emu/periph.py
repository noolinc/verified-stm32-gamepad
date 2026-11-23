"""Register-level models of the STM32F411 peripherals the firmware touches
(RM0383). Clock gating and pull-ups are modelled, so firmware that forgets
to enable a clock or a pull-up behaves wrongly here too."""
from collections import defaultdict

# RCC enable bits the models check
AHB1_GPIOA, AHB1_GPIOB, AHB1_GPIOC = 1 << 0, 1 << 1, 1 << 2
AHB2_OTGFS = 1 << 7
APB2_ADC1 = 1 << 8


class RegisterFile:
    """Plain read/write registers (accesses of any width)."""

    def __init__(self, name, resets=None):
        self.name = name
        self.regs = defaultdict(int, resets or {})

    def read(self, off, size):
        word = self.regs[off & ~3]
        return word >> (8 * (off & 3))

    def write(self, off, size, val):
        base, shift = off & ~3, 8 * (off & 3)
        mask = ((1 << (8 * size)) - 1) << shift
        self.regs[base] = (self.regs[base] & ~mask) | ((val << shift) & mask)


class RCC(RegisterFile):
    CR, PLLCFGR, CFGR, AHB1ENR, AHB2ENR, APB1ENR, APB2ENR = 0x00, 0x04, 0x08, 0x30, 0x34, 0x40, 0x44

    def __init__(self):
        super().__init__("RCC", {0x00: 0x83, 0x04: 0x24003010})

    def read(self, off, size):
        if off == self.CR:
            cr = self.regs[self.CR] | 0x2                     # HSIRDY
            if cr & (1 << 16):
                cr |= 1 << 17                                 # HSEON -> HSERDY (25 MHz crystal fitted)
            if cr & (1 << 24):
                cr |= 1 << 25                                 # PLLON -> PLLRDY
            return cr
        if off == self.CFGR:
            c = self.regs[self.CFGR]
            return (c & ~0xC) | ((c & 3) << 2)                # SWS follows SW
        return super().read(off, size)

    def enabled(self, reg, bit):
        return bool(self.regs[reg] & bit)

    def clocks_ok(self):
        """The clock tree the firmware should have programmed: HSE/25*192/2 = 96 MHz, /4 -> 48 MHz USB."""
        p = self.regs[self.PLLCFGR]
        m, n, pp, q, src = p & 0x3F, (p >> 6) & 0x1FF, 2 * (((p >> 16) & 3) + 1), (p >> 24) & 0xF, (p >> 22) & 1
        if not (src and m):
            return None
        vco = 25_000_000 // m * n
        return {"sysclk": vco // pp if (self.regs[self.CFGR] & 3) == 2 else 16_000_000, "usb": vco // q if q else 0}


class Board:
    """Black Pill wiring: buttons/d-pad to GND (active low), sticks on PA1-PA4, LED on PC13."""
    BUTTONS = [("B", 0), ("B", 1), ("B", 3), ("B", 4), ("B", 5), ("B", 6), ("B", 7), ("B", 8),
               ("B", 9), ("B", 10), ("B", 12), ("B", 13), ("B", 14), ("B", 15), ("A", 8), ("A", 15)]
    DPAD = {"up": ("A", 9), "down": ("A", 10), "left": ("C", 14), "right": ("C", 15)}
    AXIS_CHANNEL = {1: 0, 2: 1, 3: 2, 4: 3}                  # ADC1_IN1..4 = X, Y, Z, Rz

    def __init__(self):
        self.buttons = 0
        self.dpad = set()
        self.axes = [2048, 2048, 2048, 2048]

    def pressed(self, port, pin):
        for i, (p, n) in enumerate(self.BUTTONS):
            if (p, n) == (port, pin) and self.buttons & (1 << i):
                return True
        return any((p, n) == (port, pin) for d, (p, n) in self.DPAD.items() if d in self.dpad)

    def analog(self, channel):
        idx = self.AXIS_CHANNEL.get(channel)
        return None if idx is None else self.axes[idx]


class GPIO(RegisterFile):
    MODER, PUPDR, IDR, ODR, BSRR = 0x00, 0x0C, 0x10, 0x14, 0x18
    RESETS = {"A": {0x00: 0xA8000000, 0x0C: 0x64000000}, "B": {0x00: 0x00000280, 0x0C: 0x00000100}, "C": {}}
    CLOCK = {"A": AHB1_GPIOA, "B": AHB1_GPIOB, "C": AHB1_GPIOC}

    def __init__(self, port, machine):
        super().__init__("GPIO" + port, dict(self.RESETS[port]))
        self.port, self.m = port, machine

    def _clocked(self):
        return self.m.rcc.enabled(RCC.AHB1ENR, self.CLOCK[self.port])

    def read(self, off, size):
        if not self._clocked():
            return 0
        if (off & ~3) == self.IDR:
            idr = 0
            for pin in range(16):
                mode = (self.regs[self.MODER] >> (2 * pin)) & 3
                pull = (self.regs[self.PUPDR] >> (2 * pin)) & 3
                if mode == 1:                                 # output: reads back ODR
                    level = (self.regs[self.ODR] >> pin) & 1
                elif self.m.board.pressed(self.port, pin):
                    level = 0                                 # switch to GND
                else:
                    level = 1 if pull == 1 else 0             # pull-up, else floating reads 0
                idr |= level << pin
            return idr >> (8 * (off & 3))
        return super().read(off, size)

    def write(self, off, size, val):
        if not self._clocked():
            return
        if off == self.BSRR:
            self.regs[self.ODR] = (self.regs[self.ODR] | (val & 0xFFFF)) & ~(val >> 16)
            return
        super().write(off, size, val)

    def mode(self, pin):
        return (self.regs[self.MODER] >> (2 * pin)) & 3

    def output_level(self, pin):
        return (self.regs[self.ODR] >> pin) & 1


class ADC(RegisterFile):
    SR, CR2, SQR3, DR, CCR = 0x00, 0x08, 0x34, 0x4C, 0x304

    def __init__(self, machine):
        super().__init__("ADC1")
        self.m = machine
        self.conversions = 0

    def write(self, off, size, val):
        if not self.m.rcc.enabled(RCC.APB2ENR, APB2_ADC1):
            return
        super().write(off, size, val)
        if off == self.CR2 and val & (1 << 30) and val & 1:   # SWSTART with ADON
            self.regs[self.CR2] &= ~(1 << 30)
            ch = self.regs[self.SQR3] & 0x1F
            pin_mode = self.m.gpio["A"].mode(ch) if ch <= 7 else 0
            v = self.m.board.analog(ch)
            self.regs[self.DR] = (v if (v is not None and pin_mode == 3) else 0) & 0xFFF
            self.regs[self.SR] |= 1 << 1                      # EOC
            self.conversions += 1

    def read(self, off, size):
        if not self.m.rcc.enabled(RCC.APB2ENR, APB2_ADC1):
            return 0
        v = super().read(off, size)
        if (off & ~3) == self.DR:
            self.regs[self.SR] &= ~(1 << 1)                   # reading DR clears EOC
        return v


class SysTickSCB(RegisterFile):
    CSR, RVR, CVR = 0x10, 0x14, 0x18

    def __init__(self, machine):
        super().__init__("SCS")
        self.m = machine
        self.next_fire = None

    def write(self, off, size, val):
        super().write(off, size, val)
        if off in (self.CSR, self.RVR, self.CVR):
            self._schedule(reset=True)

    def _schedule(self, reset=False):
        csr = self.regs[self.CSR]
        if csr & 1 and csr & 2 and self.regs[self.RVR]:
            if reset or self.next_fire is None:
                self.next_fire = self.m.cycles + self.regs[self.RVR] + 1
        else:
            self.next_fire = None

    def pending(self, cycles):
        if self.next_fire is not None and cycles >= self.next_fire:
            self.next_fire += self.regs[self.RVR] + 1
            return True
        return False

    def read(self, off, size):
        if off == self.CVR and self.next_fire is not None:
            return max(0, self.next_fire - self.m.cycles) % (self.regs[self.RVR] + 1)
        return super().read(off, size)
