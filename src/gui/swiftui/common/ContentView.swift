import SwiftUI

#if os(macOS)
    import AppKit
#endif

#if os(macOS)
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
#endif

struct ContentView: View {
    let backend: GameListBackend
    #if os(iOS)
        @State private var showSettings = false
    #endif

    var body: some View {
        Group {
            #if os(iOS)
                NavigationStack {
                    GameList(backend: backend)
                        .navigationTitle("Games")
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .toolbar {
                            ToolbarItem(placement: .topBarTrailing) {
                                Button {
                                    showSettings = true
                                } label: {
                                    Image(systemName: "gearshape")
                                }
                                .accessibilityLabel("Settings")
                            }
                        }
                        .sheet(isPresented: $showSettings) {
                            SettingsView(backend: IOSBackends.settings)
                        }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(Color(uiColor: .systemBackground).ignoresSafeArea())
            #else
                GameList(backend: backend)
                    .frame(minWidth: 960, minHeight: 540)
            #endif
        }
    }
}

#if !os(iOS)
    #Preview {
        ContentView(backend: MockBackend())
    }
#endif
