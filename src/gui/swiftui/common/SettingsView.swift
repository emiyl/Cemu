import SwiftUI

#if os(iOS)
    import UniformTypeIdentifiers
#endif

#if os(macOS)
    import AppKit
#elseif os(iOS)
    import UIKit
#endif

@_silgen_name("CemuSettingsLoad")
private func CemuSettingsLoad(_ outState: UnsafeMutablePointer<CemuSettingsState>) -> Bool

@_silgen_name("CemuSettingsSave")
private func CemuSettingsSave(_ inState: UnsafePointer<CemuSettingsState>) -> Bool

@_silgen_name("CemuSettingsFreeBuffer")
private func CemuSettingsFreeBuffer(_ ptr: UnsafeMutableRawPointer?)

@_silgen_name("CemuSettingsGetMlcPath")
private func CemuSettingsGetMlcPath() -> UnsafePointer<CChar>?

@_silgen_name("CemuSettingsGetDefaultMlcPath")
private func CemuSettingsGetDefaultMlcPath() -> UnsafePointer<CChar>?

@_silgen_name("CemuSettingsSetMlcPath")
private func CemuSettingsSetMlcPath(_ path: UnsafePointer<CChar>?) -> Bool

@_silgen_name("CemuSettingsGetGpuCaptureDir")
private func CemuSettingsGetGpuCaptureDir() -> UnsafePointer<CChar>?

@_silgen_name("CemuSettingsGetDefaultGpuCaptureDir")
private func CemuSettingsGetDefaultGpuCaptureDir() -> UnsafePointer<CChar>?

@_silgen_name("CemuSettingsSetGpuCaptureDir")
private func CemuSettingsSetGpuCaptureDir(_ path: UnsafePointer<CChar>?) -> Bool

@_silgen_name("CemuSettingsGetGamePathCount")
private func CemuSettingsGetGamePathCount() -> UInt64

@_silgen_name("CemuSettingsGetGamePath")
private func CemuSettingsGetGamePath(_ index: UInt64) -> UnsafePointer<CChar>?

@_silgen_name("CemuSettingsAddGamePath")
private func CemuSettingsAddGamePath(_ path: UnsafePointer<CChar>?) -> Bool

@_silgen_name("CemuSettingsRemoveGamePath")
private func CemuSettingsRemoveGamePath(_ index: UInt64) -> Bool

@_silgen_name("CemuSettingsGetAccountCount")
private func CemuSettingsGetAccountCount() -> UInt64

@_silgen_name("CemuSettingsGetAccountPersistentId")
private func CemuSettingsGetAccountPersistentId(_ index: UInt64) -> UInt32

@_silgen_name("CemuSettingsGetAccountDisplayName")
private func CemuSettingsGetAccountDisplayName(_ index: UInt64) -> UnsafePointer<CChar>?

@_silgen_name("CemuSettingsCreateAccount")
private func CemuSettingsCreateAccount(
    _ miiName: UnsafePointer<CChar>?, _ outPersistentId: UnsafeMutablePointer<UInt32>?
) -> Bool

@_silgen_name("CemuSettingsDeleteAccount")
private func CemuSettingsDeleteAccount(_ persistentId: UInt32) -> Bool

struct CemuSettingsState: Codable, Equatable {
    var language: Int32 = 0
    var useDiscordPresence: Int32 = 0
    var saveScreenshots: Int32 = 0
    var checkForUpdates: Int32 = 0
    var receiveUntestedUpdates: Int32 = 0
    var playBootSound: Int32 = 0
    var isTitleRunning: Int32 = 0
    var supportsCustomNetworkService: Int32 = 0

    var graphicApi: Int32 = 0
    var vsync: Int32 = 0
    var asyncCompile: Int32 = 0
    var gx2DrawDoneSync: Int32 = 0
    var forceMeshShaders: Int32 = 0
    var supportsVulkan: Int32 = 0
    var supportsMetal: Int32 = 0
    var overrideGamma: Int32 = 0
    var overrideGammaValue: Float = 2.2
    var displayGammaValue: Float = 2.2
    var displayGammaIsSRGB: Int32 = 0
    var upscaleFilter: Int32 = 0
    var downscaleFilter: Int32 = 0
    var fullscreenScaling: Int32 = 0

    var audioApi: Int32 = 0
    var audioDelay: Int32 = 0
    var tvChannels: Int32 = 1
    var padChannels: Int32 = 1
    var inputChannels: Int32 = 0
    var tvVolume: Int32 = 100
    var padVolume: Int32 = 100
    var inputVolume: Int32 = 100
    var portalVolume: Int32 = 100

    var overlayPosition: Int32 = 0
    var overlayTextScale: Int32 = 100
    var overlayTextColor: UInt32 = 0xFFFF_FFFF
    var overlayFps: Int32 = 1
    var overlayDrawcalls: Int32 = 0
    var overlayCpuUsage: Int32 = 0
    var overlayCpuPerCoreUsage: Int32 = 0
    var overlayRamUsage: Int32 = 0
    var overlayVramUsage: Int32 = 0
    var overlayDebug: Int32 = 0

    var notificationPosition: Int32 = 1
    var notificationTextScale: Int32 = 100
    var notificationTextColor: UInt32 = 0xFFFF_FFFF
    var notificationControllerProfiles: Int32 = 1
    var notificationControllerBattery: Int32 = 0
    var notificationShaderCompiling: Int32 = 1
    var notificationFriends: Int32 = 1

    var activeAccountPersistentId: UInt32 = 0
    var activeAccountNetworkService: Int32 = 0

    var crashDump: Int32 = 0
    var gdbPort: Int32 = 1337
    var framebufferFetch: Int32 = 0
}

protocol SettingsBackend: AnyObject {
    func loadState() -> CemuSettingsState?
    func saveState(_ state: CemuSettingsState, mlcPath: String, gpuCaptureDir: String)

    func getMlcPath() -> String
    func getDefaultMlcPath() -> String
    func setMlcPath(_ path: String) -> Bool

    func getGpuCaptureDir() -> String
    func getDefaultGpuCaptureDir() -> String
    func setGpuCaptureDir(_ path: String) -> Bool

    func getGamePaths() -> [String]
    func addGamePath(_ path: String) -> Bool
    func removeGamePath(at index: UInt64) -> Bool

    func getAccounts() -> [AccountEntry]
    func createAccount(name: String) -> UInt32?
    func deleteAccount(_ persistentId: UInt32) -> Bool
}

final class CemuSettingsBackend: SettingsBackend {
    func loadState() -> CemuSettingsState? {
        var loaded = CemuSettingsState()
        if CemuSettingsLoad(&loaded) {
            return loaded
        }
        return nil
    }

    func saveState(_ state: CemuSettingsState, mlcPath: String, gpuCaptureDir: String) {
        var snapshot = state
        _ = CemuSettingsSave(&snapshot)
        mlcPath.withCString { _ = CemuSettingsSetMlcPath($0) }
        gpuCaptureDir.withCString { _ = CemuSettingsSetGpuCaptureDir($0) }
    }

    func getMlcPath() -> String {
        Self.consumeCString(CemuSettingsGetMlcPath())
    }

    func getDefaultMlcPath() -> String {
        Self.consumeCString(CemuSettingsGetDefaultMlcPath())
    }

    func setMlcPath(_ path: String) -> Bool {
        path.withCString { CemuSettingsSetMlcPath($0) }
    }

    func getGpuCaptureDir() -> String {
        Self.consumeCString(CemuSettingsGetGpuCaptureDir())
    }

    func getDefaultGpuCaptureDir() -> String {
        Self.consumeCString(CemuSettingsGetDefaultGpuCaptureDir())
    }

    func setGpuCaptureDir(_ path: String) -> Bool {
        path.withCString { CemuSettingsSetGpuCaptureDir($0) }
    }

    func getGamePaths() -> [String] {
        var paths: [String] = []
        let count = CemuSettingsGetGamePathCount()
        for i in 0..<count {
            paths.append(Self.consumeCString(CemuSettingsGetGamePath(i)))
        }
        return paths
    }

    func addGamePath(_ path: String) -> Bool {
        path.withCString { CemuSettingsAddGamePath($0) }
    }

    func removeGamePath(at index: UInt64) -> Bool {
        CemuSettingsRemoveGamePath(index)
    }

    func getAccounts() -> [AccountEntry] {
        var values: [AccountEntry] = []
        let count = CemuSettingsGetAccountCount()
        for i in 0..<count {
            let id = CemuSettingsGetAccountPersistentId(i)
            let name = Self.consumeCString(CemuSettingsGetAccountDisplayName(i))
            values.append(AccountEntry(persistentId: id, displayName: name))
        }
        return values
    }

    func createAccount(name: String) -> UInt32? {
        var createdId: UInt32 = 0
        name.withCString {
            _ = CemuSettingsCreateAccount($0, &createdId)
        }
        return createdId == 0 ? nil : createdId
    }

    func deleteAccount(_ persistentId: UInt32) -> Bool {
        CemuSettingsDeleteAccount(persistentId)
    }

    private static func consumeCString(_ ptr: UnsafePointer<CChar>?) -> String {
        guard let ptr else {
            return ""
        }
        let string = String(cString: ptr)
        CemuSettingsFreeBuffer(UnsafeMutableRawPointer(mutating: ptr))
        return string
    }
}

final class MockSettingsBackend: SettingsBackend {
    private var state = CemuSettingsState()
    private var mlcPath = "/mock/mlc"
    private var defaultMlcPath = "/mock/default-mlc"
    private var gpuCaptureDir = "/mock/gpu-captures"
    private var defaultGpuCaptureDir = "/mock/default-gpu-captures"
    private var gamePaths = ["/mock/games"]
    private var accounts = [
        AccountEntry(persistentId: 1, displayName: "Mock User")
    ]
    private var nextAccountId: UInt32 = 2

    func loadState() -> CemuSettingsState? {
        state
    }

    func saveState(_ state: CemuSettingsState, mlcPath: String, gpuCaptureDir: String) {
        self.state = state
        self.mlcPath = mlcPath
        self.gpuCaptureDir = gpuCaptureDir
    }

    func getMlcPath() -> String { mlcPath }
    func getDefaultMlcPath() -> String { defaultMlcPath }

    func setMlcPath(_ path: String) -> Bool {
        mlcPath = path
        return true
    }

    func getGpuCaptureDir() -> String { gpuCaptureDir }
    func getDefaultGpuCaptureDir() -> String { defaultGpuCaptureDir }

    func setGpuCaptureDir(_ path: String) -> Bool {
        gpuCaptureDir = path
        return true
    }

    func getGamePaths() -> [String] { gamePaths }

    func addGamePath(_ path: String) -> Bool {
        guard !path.isEmpty, !gamePaths.contains(path) else {
            return false
        }
        gamePaths.append(path)
        return true
    }

    func removeGamePath(at index: UInt64) -> Bool {
        guard index < UInt64(gamePaths.count) else {
            return false
        }
        gamePaths.remove(at: Int(index))
        return true
    }

    func getAccounts() -> [AccountEntry] { accounts }

    func createAccount(name: String) -> UInt32? {
        guard !name.isEmpty else {
            return nil
        }
        let id = nextAccountId
        nextAccountId += 1
        accounts.append(AccountEntry(persistentId: id, displayName: name))
        return id
    }

    func deleteAccount(_ persistentId: UInt32) -> Bool {
        let originalCount = accounts.count
        accounts.removeAll { $0.persistentId == persistentId }
        return accounts.count != originalCount
    }
}

struct AccountEntry: Identifiable {
    let persistentId: UInt32
    let displayName: String
    var id: UInt32 { persistentId }
}

enum SettingsTab: String, CaseIterable {
    case general = "General"
    case graphics = "Graphics"
    case audio = "Audio"
    case overlay = "Overlay"
    case account = "Account"
    case debug = "Debug"
}

enum GraphicsAPI: Int32, CaseIterable, Identifiable {
    case openGL = 0
    case vulkan = 1
    case metal = 2

    var id: Int32 { rawValue }

    var title: String {
        switch self {
        case .openGL: return "OpenGL"
        case .vulkan: return "Vulkan"
        case .metal: return "Metal"
        }
    }
}

struct LanguageOption: Identifiable {
    let id: Int32
    let title: String
}

enum AudioAPI: Int32, CaseIterable, Identifiable {
    case directSound = 0
    case xAudio27 = 1
    case xAudio2 = 2
    case cubeb = 3

    var id: Int32 { rawValue }

    var title: String {
        switch self {
        case .directSound: return "DirectSound"
        case .xAudio27: return "XAudio2.7"
        case .xAudio2: return "XAudio2"
        case .cubeb: return "Cubeb"
        }
    }
}

enum SettingsPosition: Int32, CaseIterable, Identifiable {
    case disabled = 0
    case topLeft = 1
    case topCenter = 2
    case topRight = 3
    case bottomLeft = 4
    case bottomCenter = 5
    case bottomRight = 6

    var id: Int32 { rawValue }

    var title: String {
        switch self {
        case .disabled: return "Disabled"
        case .topLeft: return "Top left"
        case .topCenter: return "Top center"
        case .topRight: return "Top right"
        case .bottomLeft: return "Bottom left"
        case .bottomCenter: return "Bottom center"
        case .bottomRight: return "Bottom right"
        }
    }
}

enum NetworkServiceOption: Int32, CaseIterable, Identifiable {
    case offline = 0
    case nintendo = 1
    case pretendo = 2
    case custom = 3

    var id: Int32 { rawValue }

    var title: String {
        switch self {
        case .offline: return "Offline"
        case .nintendo: return "Nintendo"
        case .pretendo: return "Pretendo"
        case .custom: return "Custom"
        }
    }
}

enum ScalePreset: Int32, CaseIterable, Identifiable {
    case x50 = 50
    case x75 = 75
    case x100 = 100
    case x125 = 125
    case x150 = 150
    case x175 = 175
    case x200 = 200
    case x225 = 225
    case x250 = 250
    case x275 = 275
    case x300 = 300

    var id: Int32 { rawValue }
    var title: String { "\(rawValue)%" }
}

final class SettingsStore: ObservableObject {
    private let backend: SettingsBackend

    @Published var state = CemuSettingsState()
    @Published var selectedTab: SettingsTab = .general
    @Published var gamePaths: [String] = []
    @Published var accounts: [AccountEntry] = []
    @Published var newAccountName = ""
    @Published var mlcPath = ""
    @Published var defaultMlcPath = ""
    @Published var gpuCaptureDir = ""
    @Published var defaultGpuCaptureDir = ""
    @Published var selectedGamePath: String?

    private var autosaveWorkItem: DispatchWorkItem?
    private var isLoading = false

    #if os(iOS)
        private var securityScopedURLs: [String: URL] = [:]
        private static let bookmarkDefaultsKey = "cemuGamePathBookmarks"
    #endif

    let availableLanguages: [LanguageOption] = [
        LanguageOption(id: 0, title: "Default"),
        LanguageOption(id: 1, title: "Japanese"),
        LanguageOption(id: 2, title: "English"),
        LanguageOption(id: 3, title: "French"),
        LanguageOption(id: 4, title: "German"),
        LanguageOption(id: 5, title: "Italian"),
        LanguageOption(id: 6, title: "Spanish"),
        LanguageOption(id: 7, title: "Chinese"),
        LanguageOption(id: 8, title: "Korean"),
        LanguageOption(id: 9, title: "Dutch"),
        LanguageOption(id: 10, title: "Portuguese"),
        LanguageOption(id: 11, title: "Russian"),
        LanguageOption(id: 12, title: "Taiwanese"),
    ]

    init(backend: SettingsBackend) {
        self.backend = backend
    }

    func load() {
        isLoading = true
        defer { isLoading = false }

        if let loaded = backend.loadState() {
            state = loaded
        }
        if !availableLanguages.contains(where: { $0.id == state.language }) {
            state.language = 0
        }
        mlcPath = backend.getMlcPath()
        defaultMlcPath = backend.getDefaultMlcPath()
        gpuCaptureDir = backend.getGpuCaptureDir()
        defaultGpuCaptureDir = backend.getDefaultGpuCaptureDir()
        #if os(iOS)
            restoreSecurityScopedAccess()
        #endif
        reloadGamePaths()
        reloadAccounts()
        if state.activeAccountPersistentId == 0, let first = accounts.first {
            state.activeAccountPersistentId = first.persistentId
        }
    }

    private func persist() {
        backend.saveState(state, mlcPath: mlcPath, gpuCaptureDir: gpuCaptureDir)
    }

    func scheduleAutosave() {
        guard !isLoading else {
            return
        }

        autosaveWorkItem?.cancel()
        let item = DispatchWorkItem { [weak self] in
            self?.persist()
        }
        autosaveWorkItem = item
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25, execute: item)
    }

    func closeWindow() {
        #if os(macOS)
            NSApp.keyWindow?.close()
        #endif
    }

    func reloadGamePaths() {
        let paths = backend.getGamePaths()
        gamePaths = paths
        if !paths.contains(selectedGamePath ?? "") {
            selectedGamePath = nil
        }
    }

    func addGamePath() {
        #if os(macOS)
            let panel = NSOpenPanel()
            panel.canChooseFiles = false
            panel.canChooseDirectories = true
            panel.allowsMultipleSelection = false
            panel.canCreateDirectories = true
            if panel.runModal() != .OK {
                return
            }
            guard let url = panel.url else {
                return
            }
            let path = url.path
            addGamePath(path)
        #endif
    }

    func addGamePath(_ path: String) {
        let trimmed = path.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else {
            return
        }
        _ = backend.addGamePath(trimmed)
        reloadGamePaths()
    }

    func removeGamePath(at offsets: IndexSet) {
        for idx in offsets.sorted(by: >) {
            #if os(iOS)
                if idx < gamePaths.count {
                    stopSecurityScopedAccess(for: gamePaths[idx])
                }
            #endif
            _ = backend.removeGamePath(at: UInt64(idx))
        }
        reloadGamePaths()
    }

    func removeSelectedGamePath() {
        guard let selectedGamePath,
            let idx = gamePaths.firstIndex(of: selectedGamePath)
        else {
            return
        }
        #if os(iOS)
            stopSecurityScopedAccess(for: selectedGamePath)
        #endif
        _ = backend.removeGamePath(at: UInt64(idx))
        reloadGamePaths()
    }

    func reloadAccounts() {
        accounts = backend.getAccounts()
    }

    func createAccount() {
        let trimmed = newAccountName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else {
            return
        }
        let createdId = backend.createAccount(name: trimmed)
        newAccountName = ""
        reloadAccounts()
        if let createdId {
            state.activeAccountPersistentId = createdId
        }
    }

    func deleteSelectedAccount() {
        guard state.activeAccountPersistentId != 0 else {
            return
        }
        _ = backend.deleteAccount(state.activeAccountPersistentId)
        reloadAccounts()
        if !accounts.contains(where: { $0.persistentId == state.activeAccountPersistentId }) {
            state.activeAccountPersistentId = accounts.first?.persistentId ?? 0
        }
    }

    var availableGraphicsAPIs: [GraphicsAPI] {
        var values: [GraphicsAPI] = []
        if state.supportsVulkan != 0 {
            values.append(.vulkan)
        }
        if state.supportsMetal != 0 {
            values.append(.metal)
        }
        if values.isEmpty {
            values = [.vulkan]
        }
        return values
    }

    var showCustomNetwork: Bool {
        state.supportsCustomNetworkService != 0
    }

    #if os(iOS)
        func addGamePath(url: URL) {
            let path = url.path
            let trimmed = path.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !trimmed.isEmpty else { return }

            if url.startAccessingSecurityScopedResource() {
                securityScopedURLs[trimmed] = url
            }

            if let bookmarkData = try? url.bookmarkData() {
                var bookmarks =
                    UserDefaults.standard.dictionary(forKey: Self.bookmarkDefaultsKey)
                    as? [String: Data] ?? [:]
                bookmarks[trimmed] = bookmarkData
                UserDefaults.standard.set(bookmarks, forKey: Self.bookmarkDefaultsKey)
            }

            _ = backend.addGamePath(trimmed)
            reloadGamePaths()
        }

        private func restoreSecurityScopedAccess() {
            let storedPaths = Set(backend.getGamePaths())
            guard
                var bookmarks = UserDefaults.standard.dictionary(forKey: Self.bookmarkDefaultsKey)
                    as? [String: Data]
            else { return }

            var changed = false
            for (path, bookmarkData) in bookmarks {
                guard storedPaths.contains(path) else {
                    bookmarks.removeValue(forKey: path)
                    changed = true
                    continue
                }
                var isStale = false
                if let resolvedURL = try? URL(
                    resolvingBookmarkData: bookmarkData,
                    bookmarkDataIsStale: &isStale)
                {
                    if resolvedURL!.startAccessingSecurityScopedResource() {
                        securityScopedURLs[path] = resolvedURL
                    }
                    if isStale, let newData = try? resolvedURL!.bookmarkData() {
                        bookmarks[path] = newData
                        changed = true
                    }
                }
            }

            if changed {
                UserDefaults.standard.set(bookmarks, forKey: Self.bookmarkDefaultsKey)
            }
        }

        private func stopSecurityScopedAccess(for path: String) {
            securityScopedURLs[path]?.stopAccessingSecurityScopedResource()
            securityScopedURLs.removeValue(forKey: path)
            if var bookmarks = UserDefaults.standard.dictionary(forKey: Self.bookmarkDefaultsKey)
                as? [String: Data]
            {
                bookmarks.removeValue(forKey: path)
                UserDefaults.standard.set(bookmarks, forKey: Self.bookmarkDefaultsKey)
            }
        }
    #endif
}

struct SettingsView: View {
    @StateObject var store: SettingsStore
    @Environment(\.dismiss) private var dismiss
    #if os(iOS)
        @State private var showGamePathImporter = false
    #endif

    init(
        backend: SettingsBackend = {
            #if os(iOS)
                IOSBackends.settings
            #elseif os(macOS)
                CemuSettingsBackend()
            #endif
        }()
    ) {
        _store = StateObject(wrappedValue: SettingsStore(backend: backend))
    }

    private var selectedTabBinding: Binding<SettingsTab?> {
        Binding<SettingsTab?>(
            get: { store.selectedTab },
            set: { newValue in
                guard let newValue else {
                    return
                }
                store.selectedTab = newValue
            }
        )
    }

    @ViewBuilder
    private func tabView(for tab: SettingsTab) -> some View {
        switch tab {
        case .general: generalTab
        case .graphics: graphicsTab
        case .audio: audioTab
        case .overlay: overlayTab
        case .account: accountTab
        case .debug: debugTab
        }
    }

    var selectedTabView: some View {
        tabView(for: store.selectedTab)
    }

    var body: some View {
        Group {
            #if os(iOS)
                NavigationStack {
                    List {
                        Section {
                            ForEach(SettingsTab.allCases) { tab in
                                NavigationLink {
                                    tabView(for: tab)
                                        .navigationTitle(tab.title)
                                } label: {
                                    Label(tab.title, systemImage: tab.symbolName)
                                }
                            }
                        }
                    }
                    .listStyle(.insetGrouped)
                    .navigationTitle("Settings")
                    .toolbar {
                        ToolbarItem(placement: .topBarTrailing) {
                            Button("Done") { dismiss() }
                        }
                    }
                }
            #else
                VStack(spacing: 0) {
                    NavigationSplitView {
                        List(SettingsTab.allCases, selection: selectedTabBinding) { tab in
                            Label(tab.title, systemImage: tab.symbolName)
                                .tag(tab)
                        }
                        .listStyle(.sidebar)
                        .navigationSplitViewColumnWidth(min: 220, ideal: 260, max: 320)
                    } detail: {
                        selectedTabView
                            .padding(16)
                            .frame(
                                maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
                    }
                    .navigationSplitViewStyle(.balanced)
                }
                .frame(minWidth: 860, minHeight: 620)
            #endif
        }
        .onAppear {
            store.load()
        }
        .onChange(of: store.state) {
            store.scheduleAutosave()
        }
        .onChange(of: store.mlcPath) {
            store.scheduleAutosave()
        }
        .onChange(of: store.gpuCaptureDir) {
            store.scheduleAutosave()
        }
        #if os(iOS)
            .fileImporter(
                isPresented: $showGamePathImporter,
                allowedContentTypes: [.folder],
                allowsMultipleSelection: false
            ) { result in
                guard case .success(let urls) = result, let url = urls.first else {
                    return
                }
                store.addGamePath(url: url)
            }
        #endif
    }

    #if os(iOS)
        func presentGamePathImporter() {
            showGamePathImporter = true
        }
    #endif

    func sliderRow(_ title: String, value: Binding<Int32>, step: Double) -> some View {
        HStack {
            Text(title)
                .frame(width: 80, alignment: .leading)
            Slider(
                value: Binding(
                    get: { Double(value.wrappedValue) },
                    set: { value.wrappedValue = Int32($0.rounded()) }
                ), in: 0...100, step: step)
            Text("\(value.wrappedValue)%")
                .frame(width: 44, alignment: .trailing)
        }
    }

    func colorRow(_ title: String, value: Binding<UInt32>) -> some View {
        let colorBinding = Binding<Color>(
            get: {
                let rgba = value.wrappedValue
                let r = Double((rgba >> 24) & 0xFF) / 255.0
                let g = Double((rgba >> 16) & 0xFF) / 255.0
                let b = Double((rgba >> 8) & 0xFF) / 255.0
                let a = Double(rgba & 0xFF) / 255.0
                return Color(.sRGB, red: r, green: g, blue: b, opacity: a)
            },
            set: { color in
                #if os(macOS)
                    let nsColor = NSColor(color)
                    guard let converted = nsColor.usingColorSpace(.sRGB) else {
                        return
                    }
                    let r = UInt32((converted.redComponent * 255).rounded())
                    let g = UInt32((converted.greenComponent * 255).rounded())
                    let b = UInt32((converted.blueComponent * 255).rounded())
                    let a = UInt32((converted.alphaComponent * 255).rounded())
                    value.wrappedValue = (r << 24) | (g << 16) | (b << 8) | a
                #else
                    let uiColor = UIColor(color)
                    var r: CGFloat = 0
                    var g: CGFloat = 0
                    var b: CGFloat = 0
                    var a: CGFloat = 0
                    guard uiColor.getRed(&r, green: &g, blue: &b, alpha: &a) else { return }
                    let red = UInt32((r * 255).rounded())
                    let green = UInt32((g * 255).rounded())
                    let blue = UInt32((b * 255).rounded())
                    let alpha = UInt32((a * 255).rounded())
                    value.wrappedValue = (red << 24) | (green << 16) | (blue << 8) | alpha
                #endif
            }
        )

        return HStack {
            Text(title)
            Spacer()
            ColorPicker(title, selection: colorBinding, supportsOpacity: true)
                .labelsHidden()
        }
    }

    func boolBinding(_ keyPath: WritableKeyPath<CemuSettingsState, Int32>) -> Binding<Bool> {
        Binding<Bool>(
            get: { store.state[keyPath: keyPath] != 0 },
            set: { store.state[keyPath: keyPath] = $0 ? 1 : 0 }
        )
    }
}

extension SettingsTab: Identifiable {
    var id: String { rawValue }

    var title: String {
        rawValue
    }

    var symbolName: String {
        switch self {
        case .general: return "slider.horizontal.3"
        case .graphics: return "display"
        case .audio: return "speaker.wave.2"
        case .overlay: return "rectangle.on.rectangle"
        case .account: return "person.crop.circle"
        case .debug: return "ladybug"
        }
    }
}

#if os(macOS)
    private var settingsWindowController: NSWindowController?

    @_cdecl("CemuShowSettingsWindow")
    public func CemuShowSettingsWindow() {
        DispatchQueue.main.async {
            if let window = settingsWindowController?.window {
                window.makeKeyAndOrderFront(nil)
                NSApp.activate(ignoringOtherApps: true)
                return
            }

            let view = SettingsView()
            let contentController = NSHostingController(rootView: view)

            let window = NSWindow(
                contentRect: NSRect(x: 0, y: 0, width: 900, height: 680),
                styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
                backing: .buffered,
                defer: false
            )
            window.title = "Preferences"
            window.titleVisibility = .hidden
            window.titlebarAppearsTransparent = true
            window.toolbarStyle = .unified
            window.contentViewController = contentController
            window.center()
            window.isReleasedWhenClosed = false

            let controller = NSWindowController(window: window)
            settingsWindowController = controller
            controller.showWindow(nil)
            NSApp.activate(ignoringOtherApps: true)
        }
    }
#endif
