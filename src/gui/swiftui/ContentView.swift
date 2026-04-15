import AppKit
import SwiftUI

@objc(CemuSwiftUIRootViewController)
final class CemuSwiftUIRootViewController: NSViewController {
    override func loadView() {
        self.view = NSHostingView(rootView: ContentView())
    }
}

@_cdecl("CemuCreateSwiftUIRootViewController")
public func CemuCreateSwiftUIRootViewController() -> UnsafeMutableRawPointer {
    let controller = CemuSwiftUIRootViewController()
    return Unmanaged.passRetained(controller).autorelease().toOpaque()
}

struct ContentView: View {
    var body: some View {
        SwiftUIGameList()
            .frame(minWidth: 900, minHeight: 480)
    }
}
