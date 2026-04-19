import AppKit
import SwiftUI

@objc(CemuSwiftUIRootViewController)
final class CemuSwiftUIRootViewController: NSViewController {
    override func loadView() {
        let backend: GameListBackend = CemuBackend()
        self.view = NSHostingView(rootView: ContentView(backend: backend))
    }
}

@_cdecl("CemuCreateSwiftUIRootViewController")
public func CemuCreateSwiftUIRootViewController() -> UnsafeMutableRawPointer {
    let controller = CemuSwiftUIRootViewController()
    return Unmanaged.passRetained(controller).autorelease().toOpaque()
}

struct ContentView: View {
    let backend: GameListBackend

    var body: some View {
        GameList(backend: backend)
            .frame(minWidth: 960, minHeight: 540)
    }
}

#Preview {
    ContentView(backend: MockBackend())
}
