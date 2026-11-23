import GamepadCore

/// Replays a macOS-style enumeration against core/usb_ctrl.c (the same
/// EP0 handler the firmware runs) and records every request and reply.
public struct EnumerationStep: Identifiable, Sendable {
    public enum Outcome: Equatable, Sendable { case data([UInt8]), status, stall }
    public var id: Int
    public var name: String
    public var setup: [UInt8]
    public var outcome: Outcome
    public var expectStall = false
    public var summary: String

    public var passed: Bool { expectStall == (outcome == .stall) }
}

private struct Recorder {
    var data: [UInt8] = []
    var sent = false
    var stalled = false
    var address: Int?
    var config: Int?
    var report = [UInt8](repeating: 0, count: Int(GP_REPORT_SIZE))
}

nonisolated(unsafe) private var recorder = Recorder()

private let opsPointer: UnsafeMutablePointer<usb_hw_ops_t> = {
    let p = UnsafeMutablePointer<usb_hw_ops_t>.allocate(capacity: 1)
    p.initialize(to: usb_hw_ops_t(
        ep0_send: { data, len, _ in
            recorder.sent = true
            recorder.data = data.map { Array(UnsafeBufferPointer(start: $0, count: Int(len))) } ?? []
        },
        ep0_stall: { recorder.stalled = true },
        set_address: { recorder.address = Int($0) },
        configure: { recorder.config = Int($0) },
        get_report: { out in
            guard let out else { return }
            for i in 0..<recorder.report.count { out[i] = recorder.report[i] }
        }))
    return p
}()

public enum EnumerationReplay {
    /// Run the script. `report` is what GET_REPORT should return.
    public static func run(report: [UInt8] = [0, 0, 8, 0, 0, 0, 0, 0]) -> [EnumerationStep] {
        recorder = Recorder()
        recorder.report = report
        usb_ctrl_init(opsPointer)
        var steps: [EnumerationStep] = []
        func req(_ name: String, _ rt: UInt8, _ r: UInt8, _ value: UInt16, _ index: UInt16,
                 _ length: UInt16, stall: Bool = false, describe: ([UInt8]) -> String = { hex($0) }) {
            steps.append(issue(steps.count, name, [rt, r, UInt8(value & 0xFF), UInt8(value >> 8),
                                                   UInt8(index & 0xFF), UInt8(index >> 8),
                                                   UInt8(length & 0xFF), UInt8(length >> 8)],
                               stall: stall, describe: describe))
        }
        req("GET_DESCRIPTOR Device (64)", 0x80, 6, 0x0100, 0, 64, describe: { describeDevice($0) })
        req("SET_ADDRESS 7", 0x00, 5, 7, 0, 0)
        req("GET_DESCRIPTOR Device (18)", 0x80, 6, 0x0100, 0, 18, describe: { describeDevice($0) })
        req("GET_DESCRIPTOR Configuration (9)", 0x80, 6, 0x0200, 0, 9, describe: { hex($0) })
        req("GET_DESCRIPTOR Configuration (full)", 0x80, 6, 0x0200, 0, 255, describe: { describeConfig($0) })
        req("GET_DESCRIPTOR String 0 (languages)", 0x80, 6, 0x0300, 0, 255, describe: { hex($0) })
        req("GET_DESCRIPTOR String 2 (product)", 0x80, 6, 0x0302, 0x0409, 255, describe: { utf16($0) })
        req("GET_DESCRIPTOR String 1 (manufacturer)", 0x80, 6, 0x0301, 0x0409, 255, describe: { utf16($0) })
        req("GET_DESCRIPTOR String 3 (serial)", 0x80, 6, 0x0303, 0x0409, 255, describe: { utf16($0) })
        req("GET_DESCRIPTOR Device Qualifier", 0x80, 6, 0x0600, 0, 10, stall: true)
        req("SET_CONFIGURATION 1", 0x00, 9, 1, 0, 0)
        req("HID SET_IDLE 0", 0x21, 0x0A, 0, 0, 0)
        req("GET_DESCRIPTOR HID Report", 0x81, 6, 0x2200, 0, 0x1000, describe: { describeReport($0) })
        req("HID GET_REPORT (input)", 0xA1, 0x01, 0x0100, 0, 8, describe: { hex($0) })
        req("HID SET_REPORT (output: none)", 0x21, 0x09, 0x0200, 0, 1, stall: true)
        return steps
    }

    private static func issue(_ id: Int, _ name: String, _ setup: [UInt8], stall: Bool,
                              describe: ([UInt8]) -> String) -> EnumerationStep {
        recorder.sent = false; recorder.stalled = false; recorder.data = []
        _ = setup.withUnsafeBufferPointer { usb_ctrl_setup($0.baseAddress) }
        let outcome: EnumerationStep.Outcome = recorder.stalled ? .stall
            : (recorder.data.isEmpty ? .status : .data(recorder.data))
        let summary: String
        switch outcome {
        case .stall: summary = "STALL"
        case .status: summary = "status OK"
        case .data(let d): summary = describe(d)
        }
        return EnumerationStep(id: id, name: name, setup: setup, outcome: outcome,
                               expectStall: stall, summary: summary)
    }

    public static var finalState: (address: Int?, configuration: Int?) { (recorder.address, recorder.config) }

    static func hex(_ d: [UInt8]) -> String { d.map { String(format: "%02X", $0) }.joined(separator: " ") }

    static func describeDevice(_ d: [UInt8]) -> String {
        guard d.count >= 18 else { return "\(d.count) bytes: " + hex(d) }
        let vid = UInt16(d[8]) | UInt16(d[9]) << 8, pid = UInt16(d[10]) | UInt16(d[11]) << 8
        return String(format: "18 bytes, USB %X.%02X, EP0 %d, VID %04X PID %04X", d[3], d[2], d[7], vid, pid)
    }

    static func describeConfig(_ d: [UInt8]) -> String {
        var parts: [String] = [], i = 0
        while i + 1 < d.count, d[i] > 0 {
            switch d[i + 1] {
            case 2: parts.append("config")
            case 4: parts.append("interface class \(d[i + 5])")
            case 0x21: parts.append("HID \(d[i + 3]).\(String(format: "%02X", d[i + 2]))")
            case 5: parts.append(String(format: "EP %02X interrupt %d B / %d ms", d[i + 2], d[i + 4], d[i + 6]))
            default: parts.append("type \(d[i + 1])")
            }
            i += Int(d[i])
        }
        return "\(d.count) bytes: " + parts.joined(separator: ", ")
    }

    static func utf16(_ d: [UInt8]) -> String {
        guard d.count > 2 else { return hex(d) }
        let units = stride(from: 2, to: d.count - 1, by: 2).map { UInt16(d[$0]) | UInt16(d[$0 + 1]) << 8 }
        return "\"" + String(decoding: units, as: UTF16.self) + "\""
    }

    static func describeReport(_ d: [UInt8]) -> String {
        let l = HIDLayout(descriptor: d)
        let kind = l.isGamepad ? "Game Pad" : String(format: "usage %02X:%02X", l.applicationUsagePage, l.applicationUsage)
        return "\(d.count) bytes: \(kind), \(l.fields.filter { !$0.isConstant }.count) fields, \(l.reportBytes)-byte input report"
    }
}
