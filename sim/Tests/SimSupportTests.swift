import Testing
@testable import SimSupport

@Suite struct DescriptorTests {
    let layout = HIDLayout(descriptor: GamepadEngine.reportDescriptor)

    @Test func parsesAsGamePad() {
        #expect(layout.error == nil)
        #expect(layout.isGamepad)
        #expect(layout.reportBytes == 8)
        #expect(!layout.usesReportIDs)
    }

    @Test func namesEveryField() {
        let names = layout.fields.filter { !$0.isConstant }.map(\.name)
        #expect(names == (1...16).map { "Button \($0)" } + ["Hat", "X", "Y", "Z", "Rz", "Vendor 0x01"])
    }
}

@Suite struct EngineTests {
    @Test func buttonNeedsDebounceWindow() {
        let e = GamepadEngine()
        var raw = RawInput()
        _ = e.tick(raw) // first report goes out immediately
        raw.buttons = 1 << 4
        for _ in 0..<4 { #expect(e.tick(raw) == nil) }
        let r = e.tick(raw)
        #expect(r?[0] == 0x10)
        #expect(e.padState.buttons == 0x10)
    }

    @Test func decodedReportMatchesInput() {
        let e = GamepadEngine()
        var raw = RawInput()
        raw.up = true; raw.right = true
        raw.axes = [1, -1, 0.5, 0]
        var last: [UInt8] = []
        for _ in 0..<10 { if let r = e.tick(raw) { last = r } }
        let values = Dictionary(uniqueKeysWithValues:
            HIDLayout(descriptor: GamepadEngine.reportDescriptor).decode(last).map { ($0.field.name, $0.value) })
        #expect(values["Hat"] == 1)            // up-right
        #expect(values["X"] == 127)
        #expect(values["Y"] == -127)
        #expect((60...64).contains(values["Z"] ?? 0))
        #expect(values["Rz"] == 0)
    }

    @Test func noReportWhileEndpointBusy() {
        let e = GamepadEngine()
        #expect(e.tick(RawInput(), endpointReady: false) == nil)
        #expect(e.tick(RawInput(), endpointReady: true) != nil)
    }
}

@Suite struct EnumerationTests {
    @Test func macOSStyleEnumerationSucceeds() {
        let steps = EnumerationReplay.run()
        for s in steps { #expect(s.passed, "\(s.name): \(s.summary)") }
        #expect(EnumerationReplay.finalState.address == 7)
        #expect(EnumerationReplay.finalState.configuration == 1)
        let product = steps.first { $0.name.contains("product") }
        #expect(product?.summary == "\"Nool STM32 Gamepad\"")
        let config = steps.first { $0.name == "GET_DESCRIPTOR Configuration (full)" }
        #expect(config?.summary.contains("EP 81 interrupt 8 B / 1 ms") == true)
    }
}

@Suite struct VirtualDeviceTests {
    /// Without the HID virtual-device entitlement (unsigned test runner),
    /// start() must report `unavailable` rather than crash; with it, `active`.
    @Test func startReportsAStatus() async {
        let pad = VirtualGamepad()
        let status = await pad.start(descriptor: GamepadEngine.reportDescriptor, serial: "TEST")
        switch status {
        case .unavailable(let reason): #expect(reason.contains("entitlement"))
        case .active: #expect(Bool(true))
        case .inactive: Issue.record("start() left the device inactive")
        }
        await pad.send([0, 0, 8, 0, 0, 0, 0, 0]) // harmless either way
        await pad.stop()
    }
}
