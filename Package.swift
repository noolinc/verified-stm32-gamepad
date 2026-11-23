// swift-tools-version:6.0
// macOS simulator for the STM32 gamepad. GamepadCore compiles the firmware's
// portable C core (core/*.c) unchanged, so the simulator runs the same
// debounce, report packing, descriptors and USB request handling.
import PackageDescription

let package = Package(
    name: "GamepadSim",
    platforms: [.macOS(.v15)],
    products: [.executable(name: "GamepadSim", targets: ["GamepadSim"])],
    targets: [
        .target(
            name: "GamepadCore",
            path: "core",
            sources: ["gp_input.c", "gp_report.c", "usb_ctrl.c", "usb_desc.c"],
            publicHeadersPath: ".",
            cSettings: [.unsafeFlags(["-std=c11"])]
        ),
        .target(name: "SimSupport", dependencies: ["GamepadCore"], path: "sim/SimSupport"),
        .executableTarget(name: "GamepadSim", dependencies: ["SimSupport"], path: "sim/GamepadSim"),
        .testTarget(name: "SimSupportTests", dependencies: ["SimSupport"], path: "sim/Tests"),
    ],
    swiftLanguageModes: [.v5]
)
