import SimSupport
import SwiftUI

struct ContentView: View {
    @EnvironmentObject var model: SimModel
    /// `--tab N` opens on tab N (0 report, 1 descriptor, 2 enumeration, 3 devices).
    @State private var tab: Int = {
        let a = CommandLine.arguments
        if let i = a.firstIndex(of: "--tab"), i + 1 < a.count, let n = Int(a[i + 1]) { return n }
        return 0
    }()

    var body: some View {
        VStack(spacing: 0) {
            StatusBanner(status: model.virtualStatus)
            HSplitView {
                ControllerView()
                    .frame(minWidth: 460, idealWidth: 480)
                TabView(selection: $tab) {
                    ReportView().tabItem { Text("Report") }.tag(0)
                    DescriptorView().tabItem { Text("Descriptor") }.tag(1)
                    EnumerationView().tabItem { Text("USB enumeration") }.tag(2)
                    DevicesView().tabItem { Text("macOS devices") }.tag(3)
                }
                .padding(12)
                .frame(minWidth: 520)
            }
        }
    }
}

struct StatusBanner: View {
    let status: VirtualGamepad.Status

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            Circle().fill(color).frame(width: 10, height: 10).padding(.top, 4)
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.headline)
                Text(detail).font(.callout).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer()
        }
        .padding(.horizontal, 14).padding(.vertical, 8)
        .background(color.opacity(0.12))
    }

    private var color: Color {
        switch status {
        case .active: return .green
        case .unavailable: return .orange
        case .inactive: return .gray
        }
    }

    private var title: String {
        switch status {
        case .active(let name): return "Virtual HID gamepad active: macOS sees \"\(name)\""
        case .unavailable: return "Virtual HID device unavailable — simulating in-app only"
        case .inactive: return "Starting virtual HID device…"
        }
    }

    private var detail: String {
        switch status {
        case .active: return "Games, SDL and the browser Gamepad API receive the same reports the STM32 sends."
        case .unavailable(let reason): return reason + " Report inspection, enumeration replay and the device monitor still work."
        case .inactive: return " "
        }
    }
}

// MARK: - Controller

struct ControllerView: View {
    @EnvironmentObject var model: SimModel

    var body: some View {
        VStack(spacing: 18) {
            HStack(spacing: 40) {
                StickView(title: "Left stick (WASD)", stick: 0)
                DPadView()
                StickView(title: "Right stick (IJKL)", stick: 1)
            }
            ButtonGrid()
            Toggle("Hold EP1 busy (host not polling)", isOn: $model.endpointBusy)
                .toggleStyle(.checkbox)
            Text("Buttons: Z X C V Q E R F T G Y H B N M Space · D-pad: arrow keys. " +
                 "Presses shorter than the 5 ms debounce window are ignored, as on the device.")
                .font(.caption).foregroundStyle(.secondary).multilineTextAlignment(.center)
            Spacer()
        }
        .padding(20)
    }
}

struct StickView: View {
    @EnvironmentObject var model: SimModel
    let title: String
    let stick: Int
    private let radius: CGFloat = 55

    var body: some View {
        let x = model.raw.axes[stick * 2], y = model.raw.axes[stick * 2 + 1]
        VStack {
            ZStack {
                Circle().fill(.quaternary).frame(width: radius * 2, height: radius * 2)
                Circle().stroke(.tertiary).frame(width: radius * 2 * 96 / 2048, height: radius * 2 * 96 / 2048)
                Circle().fill(.tint).frame(width: 34, height: 34)
                    .offset(x: x * radius, y: y * radius)
            }
            .frame(width: radius * 2, height: radius * 2)
            .contentShape(Circle())
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { g in
                    var dx = (g.location.x - radius) / radius, dy = (g.location.y - radius) / radius
                    let len = (dx * dx + dy * dy).squareRoot()
                    if len > 1 { dx /= len; dy /= len }
                    model.setStick(stick, x: dx, y: dy)
                }
                .onEnded { _ in model.setStick(stick, x: 0, y: 0) })
            Text(title).font(.caption)
            Text(String(format: "%+.2f, %+.2f", x, y)).font(.caption.monospacedDigit()).foregroundStyle(.secondary)
        }
    }
}

struct DPadView: View {
    @EnvironmentObject var model: SimModel

    var body: some View {
        VStack(spacing: 2) {
            arrow("arrowtriangle.up.fill", model.raw.up) { model.raw.up = $0 }
            HStack(spacing: 2) {
                arrow("arrowtriangle.left.fill", model.raw.left) { model.raw.left = $0 }
                Text(hatName).font(.caption2.monospaced()).frame(width: 34, height: 34)
                arrow("arrowtriangle.right.fill", model.raw.right) { model.raw.right = $0 }
            }
            arrow("arrowtriangle.down.fill", model.raw.down) { model.raw.down = $0 }
        }
    }

    private var hatName: String {
        let names = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
        return model.pad.hat < 8 ? names[Int(model.pad.hat)] : "·"
    }

    private func arrow(_ symbol: String, _ on: Bool, _ set: @escaping (Bool) -> Void) -> some View {
        Image(systemName: symbol)
            .frame(width: 34, height: 34)
            .background(on ? Color.accentColor.opacity(0.5) : Color.secondary.opacity(0.15),
                        in: RoundedRectangle(cornerRadius: 6))
            .gesture(DragGesture(minimumDistance: 0).onChanged { _ in set(true) }.onEnded { _ in set(false) })
    }
}

struct ButtonGrid: View {
    @EnvironmentObject var model: SimModel

    var body: some View {
        let columns = Array(repeating: GridItem(.fixed(48), spacing: 6), count: 8)
        LazyVGrid(columns: columns, spacing: 8) {
            ForEach(0..<16, id: \.self) { i in
                let raw = model.raw.buttons & (1 << UInt16(i)) != 0
                let reported = model.pad.buttons & (1 << UInt16(i)) != 0
                VStack(spacing: 1) {
                    Text("\(i + 1)").font(.headline)
                    Text(SimModel.buttonKeys[i] == " " ? "Space" : SimModel.buttonKeys[i].uppercased())
                        .font(.caption2).foregroundStyle(.secondary)
                }
                .frame(width: 48, height: 44)
                .background(reported ? Color.accentColor.opacity(0.55)
                            : (raw ? Color.accentColor.opacity(0.2) : Color.secondary.opacity(0.12)),
                            in: RoundedRectangle(cornerRadius: 8))
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { _ in model.setButton(i, true) }
                    .onEnded { _ in model.setButton(i, false) })
            }
        }
    }
}

// MARK: - Inspector tabs

struct ReportView: View {
    @EnvironmentObject var model: SimModel

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Input report on EP1 IN (8 bytes, no report ID)").font(.headline)
            HStack(spacing: 4) {
                ForEach(Array(model.report.enumerated()), id: \.offset) { i, b in
                    VStack(spacing: 2) {
                        Text(String(format: "%02X", b)).font(.title3.monospaced())
                        Text(String(b, radix: 2).leftPad(8)).font(.system(size: 9, design: .monospaced))
                            .foregroundStyle(.secondary).fixedSize()
                        Text(["btn 1-8", "btn 9-16", "hat", "X", "Y", "Z", "Rz", "seq"][i]).font(.caption2)
                    }
                    .frame(minWidth: 50).padding(4).background(.quaternary, in: RoundedRectangle(cornerRadius: 6))
                }
            }
            Text("\(model.reportsSent) reports sent · \(model.reportsPerSecond)/s (sent on change, idle rate 0)")
                .font(.callout).foregroundStyle(.secondary)
            Text("Decoded with the report descriptor").font(.headline)
            Table(model.layout.decode(model.report).map(DecodedRow.init)) {
                TableColumn("Field", value: \.name)
                TableColumn("Usage", value: \.usage)
                TableColumn("Bits", value: \.bits)
                TableColumn("Value", value: \.value)
            }
        }
    }
}

struct DecodedRow: Identifiable {
    let id: Int, name: String, usage: String, bits: String, value: String
    init(_ d: (field: HIDField, value: Int32)) {
        id = d.field.id
        name = d.field.name
        usage = String(format: "%02X:%02X", d.field.usagePage, d.field.usage)
        bits = "\(d.field.bitOffset)+\(d.field.bitSize)"
        value = d.field.hasNull && (d.value < d.field.logicalMin || d.value > d.field.logicalMax)
            ? "null (\(d.value))" : "\(d.value)"
    }
}

struct DescriptorView: View {
    @EnvironmentObject var model: SimModel

    var body: some View {
        let d = GamepadEngine.reportDescriptor
        VStack(alignment: .leading, spacing: 10) {
            Text("HID report descriptor (\(d.count) bytes, core/usb_desc.c)").font(.headline)
            Text(model.layout.isGamepad ? "Application collection: Generic Desktop / Game Pad — bound by macOS's built-in HID driver."
                 : "Not a Game Pad collection").foregroundStyle(model.layout.isGamepad ? Color.secondary : Color.red)
            ScrollView {
                Text(stride(from: 0, to: d.count, by: 16).map { i in
                    String(format: "%04X  ", i) + d[i..<min(i + 16, d.count)].map { String(format: "%02X", $0) }.joined(separator: " ")
                }.joined(separator: "\n"))
                .font(.body.monospaced()).textSelection(.enabled)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .frame(maxHeight: 160)
            Table(model.layout.fields.map { f in
                DecodedRow((field: f, value: 0))
            }) {
                TableColumn("Field", value: \.name)
                TableColumn("Usage", value: \.usage)
                TableColumn("Bits", value: \.bits)
            }
        }
    }
}

struct EnumerationView: View {
    @EnvironmentObject var model: SimModel

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("macOS-style enumeration against core/usb_ctrl.c").font(.headline)
                Spacer()
                Button("Replug") { model.replug() }
            }
            let ok = model.enumeration.allSatisfy(\.passed)
            Text(ok ? "All \(model.enumeration.count) requests answered as expected."
                 : "Some requests were not answered as expected.")
                .foregroundStyle(ok ? Color.secondary : Color.red)
            Table(model.enumeration) {
                TableColumn("") { s in Image(systemName: s.passed ? "checkmark.circle.fill" : "xmark.octagon.fill")
                    .foregroundStyle(s.passed ? .green : .red) }.width(20)
                TableColumn("Request") { s in Text(s.name).help(s.name) }.width(min: 170, ideal: 230)
                TableColumn("SETUP") { s in Text(s.setup.map { String(format: "%02X", $0) }.joined(separator: " "))
                    .font(.system(size: 10, design: .monospaced)) }.width(min: 150, ideal: 165)
                TableColumn("Reply") { s in Text(s.summary).help(s.summary) }.width(min: 200, ideal: 320)
            }
        }
    }
}

struct DevicesView: View {
    @EnvironmentObject var model: SimModel

    var body: some View {
        DevicesList(monitor: model.monitor)
    }
}

struct DevicesList: View {
    @ObservedObject var monitor: DeviceMonitor

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Gamepads and joysticks macOS currently sees").font(.headline)
            Text("Plug in the flashed Black Pill to see its reports decoded here with its own descriptor.")
                .font(.callout).foregroundStyle(.secondary)
            if let e = monitor.error { Text(e).foregroundStyle(.red) }
            if monitor.devices.isEmpty {
                Text("No HID gamepads connected.").foregroundStyle(.secondary).padding(.top, 20)
            }
            List(monitor.devices) { d in
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Text(d.name).font(.headline)
                        if d.isThisProject { Text("this project").font(.caption).padding(.horizontal, 6)
                            .background(.green.opacity(0.2), in: Capsule()) }
                        Spacer()
                        Text(String(format: "VID %04X  PID %04X", d.vendorID, d.productID)).font(.caption.monospaced())
                    }
                    Text("\(d.layout.isGamepad ? "Game Pad" : "Joystick") · \(d.layout.reportBytes)-byte reports · \(d.reports) received" +
                         (d.serial.map { " · serial \($0)" } ?? ""))
                        .font(.caption).foregroundStyle(.secondary)
                    Text(d.layout.decode(d.lastReport).map { "\($0.field.name)=\($0.value)" }.joined(separator: "  "))
                        .font(.caption.monospaced()).lineLimit(3)
                }
                .padding(.vertical, 4)
            }
        }
    }
}

extension String {
    func leftPad(_ n: Int) -> String { String(repeating: "0", count: max(0, n - count)) + self }
}
