import GamepadCore

/// Raw controller input as the firmware's board_read() would see it.
public struct RawInput: Equatable, Sendable {
    public var buttons: UInt16 = 0           // bit n = button n+1
    public var up = false, down = false, left = false, right = false
    /// Stick positions in -1...1: X, Y (down positive), Z, Rz.
    public var axes: [Double] = [0, 0, 0, 0]

    public init() {}

    /// The 12-bit ADC reading for an axis position: -1 -> 0, 0 -> 2048, +1 -> 4095
    /// (the converter's full range around the firmware's default centre).
    public static func adc(_ v: Double) -> UInt16 {
        let c = max(-1, min(1, v))
        return UInt16((2048 + c * (c < 0 ? 2048 : 2047)).rounded())
    }
}

/// Decoded controller state (what the report carries).
public struct PadState: Equatable, Sendable {
    public var buttons: UInt16
    public var hat: UInt8
    public var axes: [Int8]

    public init(buttons: UInt16, hat: UInt8, axes: [Int8]) {
        self.buttons = buttons
        self.hat = hat
        self.axes = axes
    }
}

/// Runs the firmware core (core/gp_input.c, core/gp_report.c) one
/// millisecond at a time, exactly as firmware/stm32f411/main.c does.
public final class GamepadEngine {
    private var input = gp_input_t()
    private var tx = gp_tx_t()
    private var state = gp_state_t()
    public private(set) var reportsSent = 0
    public private(set) var lastReport = [UInt8](repeating: 0, count: Int(GP_REPORT_SIZE))

    public init() { reset() }

    /// Equivalent of a bus reset / reconfigure (gp_tx_init, specs/UsbHid.tla).
    public func reset() {
        gp_input_init(&input)
        gp_tx_init(&tx)
        state = gp_state_t()
        state.hat = UInt8(GP_HAT_NULL)
        reportsSent = 0
    }

    /// One 1 ms tick. Returns the report when the scheduler sends one.
    public func tick(_ raw: RawInput, idleRate: UInt8 = 0, endpointReady: Bool = true) -> [UInt8]? {
        var r = gp_raw_t()
        var bits = UInt32(raw.buttons)
        if raw.up { bits |= UInt32(GP_IN_UP) }
        if raw.down { bits |= UInt32(GP_IN_DOWN) }
        if raw.left { bits |= UInt32(GP_IN_LEFT) }
        if raw.right { bits |= UInt32(GP_IN_RIGHT) }
        r.inputs = bits
        let a = raw.axes + [0, 0, 0, 0]
        r.axis_raw = (RawInput.adc(a[0]), RawInput.adc(a[1]), RawInput.adc(a[2]), RawInput.adc(a[3]))
        gp_input_step(&input, &r, &state)
        var out = [UInt8](repeating: 0, count: Int(GP_REPORT_SIZE))
        let sent = out.withUnsafeMutableBufferPointer { buf in
            gp_tx_step(&tx, &state, idleRate, endpointReady, buf.baseAddress)
        }
        guard sent else { return nil }
        reportsSent += 1
        lastReport = out
        return out
    }

    public var padState: PadState {
        PadState(buttons: state.buttons, hat: state.hat,
                 axes: [state.axis.0, state.axis.1, state.axis.2, state.axis.3])
    }

    /// The current report as GET_REPORT would return it.
    public func currentReport() -> [UInt8] {
        var out = [UInt8](repeating: 0, count: Int(GP_REPORT_SIZE))
        out.withUnsafeMutableBufferPointer { gp_report_build(&state, tx.seq, $0.baseAddress) }
        return out
    }

    /// The HID report descriptor the firmware sends (core/usb_desc.c).
    public static var reportDescriptor: [UInt8] { bytes(usb_desc_report()) }
    public static var deviceDescriptor: [UInt8] { bytes(usb_desc_device()) }
    public static var configDescriptor: [UInt8] { bytes(usb_desc_config()) }
    public static let vendorID = UInt32(USB_VID)
    public static let productID = UInt32(USB_PID)

    static func bytes(_ b: usb_buf_t) -> [UInt8] {
        guard let p = b.data else { return [] }
        return Array(UnsafeBufferPointer(start: p, count: Int(b.len)))
    }
}
