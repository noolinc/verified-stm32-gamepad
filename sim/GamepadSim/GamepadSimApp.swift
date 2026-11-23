import AppKit
import SwiftUI

@main
struct GamepadSimApp: App {
    @StateObject private var model = SimModel()

    init() {
        // Running from `swift run` (no .app bundle): become a regular app.
        NSApplication.shared.setActivationPolicy(.regular)
        DispatchQueue.main.async { NSApplication.shared.activate(ignoringOtherApps: true) }
    }

    var body: some Scene {
        WindowGroup("Nool Gamepad Simulator") {
            ContentView()
                .environmentObject(model)
                .onAppear { model.start() }
                .frame(minWidth: 1040, minHeight: 640)
        }
        .windowResizability(.contentMinSize)
    }
}
