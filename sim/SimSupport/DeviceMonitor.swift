import CoreHID
import Foundation

/// Watches every gamepad or joystick macOS sees (the virtual one or a real
/// Black Pill) and decodes its input reports with the device's own report
/// descriptor, using the same parser as the report inspector.
@MainActor
public final class DeviceMonitor: ObservableObject {
    public struct Device: Identifiable {
        public let id: UInt64
        public var name: String
        public var vendorID: UInt32
        public var productID: UInt32
        public var serial: String?
        public var layout: HIDLayout
        public var lastReport: [UInt8] = []
        public var reports = 0

        public var isThisProject: Bool {
            vendorID == GamepadEngine.vendorID && productID == GamepadEngine.productID
        }
    }

    @Published public private(set) var devices: [Device] = []
    @Published public private(set) var error: String?
    private var started = false

    public init() {}

    public func start() {
        guard !started else { return }
        started = true
        Task { await watch() }
    }

    private func watch() async {
        let manager = HIDDeviceManager()
        let criteria = [
            HIDDeviceManager.DeviceMatchingCriteria(primaryUsage: .genericDesktop(.gamepad)),
            HIDDeviceManager.DeviceMatchingCriteria(primaryUsage: .genericDesktop(.joystick)),
        ]
        do {
            for try await note in await manager.monitorNotifications(matchingCriteria: criteria) {
                switch note {
                case .deviceMatched(let ref): await attach(ref)
                case .deviceRemoved(let ref): devices.removeAll { $0.id == ref.deviceID }
                @unknown default: break
                }
            }
        } catch {
            self.error = "Device monitor stopped: \(error.localizedDescription)"
        }
    }

    private func attach(_ ref: HIDDeviceClient.DeviceReference) async {
        guard let client = HIDDeviceClient(deviceReference: ref) else { return }
        let name = await client.product ?? "HID device"
        let serial = await client.serialNumber
        let vid = await client.vendorID, pid = await client.productID
        let descriptor = await client.descriptor
        let dev = Device(id: ref.deviceID, name: name, vendorID: vid, productID: pid, serial: serial,
                         layout: HIDLayout(descriptor: [UInt8](descriptor)))
        devices.removeAll { $0.id == dev.id }
        devices.append(dev)
        Task { await follow(client, id: ref.deviceID) }
    }

    private func follow(_ client: HIDDeviceClient, id: UInt64) async {
        do {
            let stream = await client.monitorNotifications(reportIDsToMonitor: [HIDReportID.allReports],
                                                           elementsToMonitor: [])
            for try await note in stream {
                if case .inputReport(_, let data, _) = note, let i = devices.firstIndex(where: { $0.id == id }) {
                    devices[i].lastReport = [UInt8](data)
                    devices[i].reports += 1
                }
            }
        } catch {
            // device removed or access refused; the manager stream reports removal
        }
    }
}
