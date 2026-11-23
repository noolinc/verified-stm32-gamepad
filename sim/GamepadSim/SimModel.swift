import AppKit
import Foundation
import SimSupport

/// Drives the firmware core at 1 kHz on a background queue, feeds reports
/// to the virtual HID device, and publishes snapshots to the UI at 30 Hz.
@MainActor
final class SimModel: ObservableObject {
    // UI state
    @Published var raw = RawInput()
    @Published private(set) var report = [UInt8](repeating: 0, count: 8)
    @Published private(set) var pad = PadState(buttons: 0, hat: 8, axes: [0, 0, 0, 0])
    @Published private(set) var reportsSent = 0
    @Published private(set) var reportsPerSecond = 0
    @Published private(set) var virtualStatus: VirtualGamepad.Status = .inactive
    @Published private(set) var enumeration: [EnumerationStep] = []
    @Published var endpointBusy = false

    let layout = HIDLayout(descriptor: GamepadEngine.reportDescriptor)
    let monitor = DeviceMonitor()

    private let engine = GamepadEngine()
    private let virtualPad = VirtualGamepad()
    private let queue = DispatchQueue(label: "gamepad.engine", qos: .userInteractive)
    private let lock = NSLock()
    private var shared = Shared()
    private var engineTimer: DispatchSourceTimer?
    private var uiTimer: Timer?
    private var keyMonitor: Any?
    private var pressedKeys = Set<String>()
    private var mouseButtons: UInt16 = 0
    private var keyButtons: UInt16 = 0
    private var lastCount = 0

    private struct Shared {
        var raw = RawInput()
        var busy = false
        var report = [UInt8](repeating: 0, count: 8)
        var pad = PadState(buttons: 0, hat: 8, axes: [0, 0, 0, 0])
        var sent = 0
    }

    func start() {
        enumeration = EnumerationReplay.run()
        monitor.start()
        Task {
            let serial = "SIM-" + String(UInt32(truncatingIfNeeded: ProcessInfo.processInfo.processIdentifier), radix: 16)
            virtualStatus = await virtualPad.start(descriptor: GamepadEngine.reportDescriptor, serial: serial)
        }
        startEngine()
        uiTimer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.refresh() }
        }
        Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self else { return }
                self.reportsPerSecond = self.reportsSent - self.lastCount
                self.lastCount = self.reportsSent
            }
        }
        installKeyboard()
        if CommandLine.arguments.contains("--demo") { startDemo() }
    }

    /// `--demo`: circle both sticks, sweep the d-pad and walk the buttons.
    private func startDemo() {
        var t = 0.0
        Timer.scheduledTimer(withTimeInterval: 1.0 / 60.0, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self else { return }
                t += 1.0 / 60.0
                var r = RawInput()
                r.axes = [cos(t * 2), sin(t * 2), cos(-t * 3) * 0.6, sin(-t * 3) * 0.6]
                let step = Int(t * 4)
                r.buttons = 1 << UInt16(step % 16)
                let dir = (step / 2) % 8
                r.up = [7, 0, 1].contains(dir); r.right = [1, 2, 3].contains(dir)
                r.down = [3, 4, 5].contains(dir); r.left = [5, 6, 7].contains(dir)
                self.raw = r
            }
        }
    }

    private func startEngine() {
        let timer = DispatchSource.makeTimerSource(flags: .strict, queue: queue)
        timer.schedule(deadline: .now(), repeating: .milliseconds(1), leeway: .microseconds(100))
        let engine = engine, lock = lock, pad = virtualPad
        timer.setEventHandler { [unowned self] in
            lock.lock(); let raw = self.shared.raw; let busy = self.shared.busy; lock.unlock()
            let sent = engine.tick(raw, endpointReady: !busy)
            lock.lock()
            self.shared.pad = engine.padState
            self.shared.sent = engine.reportsSent
            if let sent { self.shared.report = sent }
            lock.unlock()
            if let sent { Task { await pad.send(sent) } }
        }
        timer.resume()
        engineTimer = timer
    }

    /// Re-run enumeration and reset the scheduler, as a USB bus reset would.
    func replug() {
        queue.async { [engine] in engine.reset() }
        enumeration = EnumerationReplay.run(report: report)
    }

    private func refresh() {
        lock.lock()
        shared.raw = raw
        shared.busy = endpointBusy
        let snap = shared
        lock.unlock()
        if snap.report != report { report = snap.report }
        if snap.pad != pad { pad = snap.pad }
        if snap.sent != reportsSent { reportsSent = snap.sent }
    }

    // MARK: - input helpers used by the views

    func setButton(_ index: Int, _ down: Bool) {
        if down { mouseButtons |= 1 << UInt16(index) } else { mouseButtons &= ~(1 << UInt16(index)) }
        raw.buttons = mouseButtons | keyButtons
    }

    func setStick(_ stick: Int, x: Double, y: Double) {
        raw.axes[stick * 2] = x
        raw.axes[stick * 2 + 1] = y
    }

    // MARK: - keyboard

    static let buttonKeys = ["z", "x", "c", "v", "q", "e", "r", "f", "t", "g", "y", "h", "b", "n", "m", " "]

    private func installKeyboard() {
        keyMonitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .keyUp]) { [weak self] event in
            guard let self else { return event }
            let handled = MainActor.assumeIsolated { self.handle(event) }
            return handled ? nil : event
        }
    }

    private func key(for event: NSEvent) -> String? {
        switch event.keyCode {
        case 123: return "←"
        case 124: return "→"
        case 125: return "↓"
        case 126: return "↑"
        case 49: return " "
        default: return event.charactersIgnoringModifiers?.lowercased()
        }
    }

    private func handle(_ event: NSEvent) -> Bool {
        guard !event.modifierFlags.contains(.command), let k = key(for: event) else { return false }
        if event.type == .keyDown { pressedKeys.insert(k) } else { pressedKeys.remove(k) }
        var r = raw
        r.up = pressedKeys.contains("↑"); r.down = pressedKeys.contains("↓")
        r.left = pressedKeys.contains("←"); r.right = pressedKeys.contains("→")
        func axis(_ neg: String, _ pos: String) -> Double {
            (pressedKeys.contains(pos) ? 1 : 0) - (pressedKeys.contains(neg) ? 1 : 0)
        }
        let known = Set(Self.buttonKeys + ["w", "a", "s", "d", "i", "j", "k", "l", "↑", "↓", "←", "→"])
        guard known.contains(k) else { return false }
        if ["w", "a", "s", "d"].contains(k) { r.axes[0] = axis("a", "d"); r.axes[1] = axis("w", "s") }
        if ["i", "j", "k", "l"].contains(k) { r.axes[2] = axis("j", "l"); r.axes[3] = axis("i", "k") }
        var buttons: UInt16 = 0
        for (i, bk) in Self.buttonKeys.enumerated() where pressedKeys.contains(bk) { buttons |= 1 << UInt16(i) }
        keyButtons = buttons
        r.buttons = mouseButtons | keyButtons
        raw = r
        return true
    }
}
