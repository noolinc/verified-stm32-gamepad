import CoreHID
import Foundation

/// Publishes the simulated gamepad to macOS as a real HID device (CoreHID
/// HIDVirtualDevice), using the firmware's report descriptor and IDs.
///
/// Creating a virtual HID device needs the restricted entitlement
/// `com.apple.developer.hid.virtual.device`. Without it creation fails and
/// the simulator keeps working without the system-visible device.
public actor VirtualGamepad {
    public enum Status: Equatable, Sendable {
        case inactive
        case active(name: String)
        case unavailable(reason: String)
    }

    public static let productName = "Nool Gamepad (Sim)"
    public private(set) var status: Status = .inactive
    private var device: HIDVirtualDevice?
    private var current = [UInt8](repeating: 0, count: 8)

    public init() {}

    @discardableResult
    public func start(descriptor: [UInt8], serial: String) async -> Status {
        let props = HIDVirtualDevice.Properties(
            descriptor: Data(descriptor),
            vendorID: GamepadEngine.vendorID,
            productID: GamepadEngine.productID,
            transport: .usb,
            product: Self.productName,
            manufacturer: "Nool",
            serialNumber: serial)
        guard let dev = HIDVirtualDevice(properties: props) else {
            status = .unavailable(reason: """
                macOS refused to create the virtual HID device. It needs the \
                com.apple.developer.hid.virtual.device entitlement (sign the app with \
                tools/bundle-sim.sh and a provisioning profile that grants it).
                """)
            return status
        }
        await dev.activate(delegate: Delegate(owner: self))
        device = dev
        status = .active(name: Self.productName)
        return status
    }

    public func send(_ report: [UInt8]) async {
        current = report
        guard let device else { return }
        try? await device.dispatchInputReport(data: Data(report), timestamp: .now)
    }

    func currentReport() -> Data { Data(current) }

    public func stop() {
        device = nil
        status = .inactive
    }

    private struct Delegate: HIDVirtualDeviceDelegate {
        let owner: VirtualGamepad

        func hidVirtualDevice(_ device: HIDVirtualDevice, receivedSetReportRequestOfType type: HIDReportType,
                              id: HIDReportID?, data: Data) async throws {
            throw CocoaError(.featureUnsupported) // the gamepad has no output reports (stall)
        }

        func hidVirtualDevice(_ device: HIDVirtualDevice, receivedGetReportRequestOfType type: HIDReportType,
                              id: HIDReportID?, maxSize: Int) async throws -> Data {
            await owner.currentReport().prefix(maxSize)
        }
    }
}
