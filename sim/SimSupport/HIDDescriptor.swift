/// A HID report-descriptor parser: lists every Input field the way a host
/// lays out the report, then decodes reports with it. Used for the
/// simulator's own descriptor and for real devices seen by the monitor.
public struct HIDField: Equatable, Sendable, Identifiable {
    public var id: Int
    public var usagePage: UInt16
    public var usage: UInt16          // 0 for padding
    public var bitOffset: Int
    public var bitSize: Int
    public var logicalMin: Int32
    public var logicalMax: Int32
    public var isConstant: Bool
    public var hasNull: Bool

    public var name: String {
        if isConstant { return "padding" }
        switch (usagePage, usage) {
        case (0x01, 0x30): return "X"
        case (0x01, 0x31): return "Y"
        case (0x01, 0x32): return "Z"
        case (0x01, 0x33): return "Rx"
        case (0x01, 0x34): return "Ry"
        case (0x01, 0x35): return "Rz"
        case (0x01, 0x39): return "Hat"
        case (0x09, let n): return "Button \(n)"
        case (0xFF00...0xFFFF, let n): return String(format: "Vendor 0x%02X", n)
        default: return String(format: "Usage %02X:%02X", usagePage, usage)
        }
    }
}

public struct HIDLayout: Equatable, Sendable {
    public var fields: [HIDField] = []
    public var totalBits = 0
    public var applicationUsagePage: UInt16 = 0
    public var applicationUsage: UInt16 = 0
    public var usesReportIDs = false
    public var error: String?

    public var isGamepad: Bool { applicationUsagePage == 0x01 && applicationUsage == 0x05 }
    public var reportBytes: Int { (totalBits + 7) / 8 }

    public init(descriptor d: [UInt8]) {
        var page: UInt16 = 0, lmin: Int32 = 0, lmax: Int32 = 0, size = 0, count = 0
        var usages: [UInt16] = [], umin: UInt32 = 0, umax: UInt32 = 0, range = false
        var depth = 0, i = 0
        func uval(_ b: ArraySlice<UInt8>) -> UInt32 {
            b.enumerated().reduce(0) { $0 | UInt32($1.element) << (8 * UInt32($1.offset)) }
        }
        func sval(_ b: ArraySlice<UInt8>) -> Int32 {
            switch b.count {
            case 1: return Int32(Int8(bitPattern: b.first!))
            case 2: return Int32(Int16(bitPattern: UInt16(uval(b))))
            case 4: return Int32(bitPattern: uval(b))
            default: return 0
            }
        }
        while i < d.count {
            let pre = d[i]
            let sz = [0, 1, 2, 4][Int(pre & 3)]
            let type = (pre >> 2) & 3, tag = pre >> 4
            guard i + 1 + sz <= d.count else { error = "truncated item at byte \(i)"; return }
            let v = d[(i + 1)..<(i + 1 + sz)]
            switch type {
            case 1: // global
                switch tag {
                case 0: page = UInt16(truncatingIfNeeded: uval(v))
                case 1: lmin = sval(v)
                case 2: lmax = lmin < 0 ? sval(v) : Int32(bitPattern: uval(v))
                case 7: size = Int(uval(v))
                case 8: usesReportIDs = true
                case 9: count = Int(uval(v))
                default: break
                }
            case 2: // local
                switch tag {
                case 0: usages.append(UInt16(truncatingIfNeeded: uval(v)))
                case 1: umin = uval(v); range = true
                case 2: umax = uval(v); range = true
                default: break
                }
            case 0: // main
                switch tag {
                case 0xA:
                    if depth == 0, let u = usages.first { applicationUsagePage = page; applicationUsage = u }
                    depth += 1
                case 0xC:
                    depth -= 1
                    if depth < 0 { error = "unbalanced End Collection"; return }
                case 0x8:
                    let flags = uval(v)
                    for k in 0..<count {
                        var usage: UInt16 = 0
                        if flags & 1 == 0 {
                            if range { usage = UInt16(min(umin + UInt32(k), umax)) }
                            else if !usages.isEmpty { usage = usages[min(k, usages.count - 1)] }
                        }
                        fields.append(HIDField(id: fields.count, usagePage: page, usage: usage,
                                               bitOffset: totalBits, bitSize: size,
                                               logicalMin: lmin, logicalMax: lmax,
                                               isConstant: flags & 1 != 0, hasNull: flags & 0x40 != 0))
                        totalBits += size
                    }
                default: break
                }
                usages = []; range = false; umin = 0; umax = 0
            default: break
            }
            i += 1 + sz
        }
        if depth != 0 { error = "unclosed collection" }
    }

    /// Value of one field in a report (sign-extended when logicalMin < 0).
    public func value(of f: HIDField, in report: [UInt8]) -> Int32 {
        var v: UInt32 = 0
        for b in 0..<f.bitSize {
            let bit = f.bitOffset + b
            if bit / 8 < report.count, report[bit / 8] & (1 << (bit % 8)) != 0 { v |= 1 << UInt32(b) }
        }
        if f.logicalMin < 0, f.bitSize < 32, f.bitSize > 0, v & (1 << UInt32(f.bitSize - 1)) != 0 {
            v |= ~((1 << UInt32(f.bitSize)) - 1)
        }
        return Int32(bitPattern: v)
    }

    /// Non-padding fields with their decoded values.
    public func decode(_ report: [UInt8]) -> [(field: HIDField, value: Int32)] {
        fields.filter { !$0.isConstant }.map { ($0, value(of: $0, in: report)) }
    }
}
