import SwiftUI

@main
struct CemuIosApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView(backend: IOSBackends.gameList)
        }
    }
}
