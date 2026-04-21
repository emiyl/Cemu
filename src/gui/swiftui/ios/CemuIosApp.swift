import SwiftUI

@main
struct CemuIosApp: App {
    init() {
        CemuAppInit()
    }

    var body: some Scene {
        WindowGroup {
            ContentView(backend: IOSBackends.gameList)
        }
    }
}

@_silgen_name("CemuAppInit")
private func CemuAppInit() -> Bool
