import Foundation

#if os(iOS)
    final class IOSSettingsBackend: SettingsBackend {
        private struct Payload: Codable {
            var state: CemuSettingsState
            var mlcPath: String
            var gpuCaptureDir: String
            var gamePaths: [String]
            var accounts: [AccountEntryPayload]
            var nextAccountId: UInt32
        }

        private struct AccountEntryPayload: Codable {
            var persistentId: UInt32
            var displayName: String

            init(_ entry: AccountEntry) {
                self.persistentId = entry.persistentId
                self.displayName = entry.displayName
            }

            var accountEntry: AccountEntry {
                AccountEntry(persistentId: persistentId, displayName: displayName)
            }
        }

        private enum Keys {
            static let payload = "cemu.swiftui.ios.settings.payload"
        }

        private let defaults = UserDefaults.standard

        private var state = CemuSettingsState(
            language: 2,
            useDiscordPresence: 0,
            saveScreenshots: 1,
            checkForUpdates: 1,
            receiveUntestedUpdates: 0,
            playBootSound: 0,
            isTitleRunning: 0,
            supportsCustomNetworkService: 0,
            graphicApi: 2,
            vsync: 1,
            asyncCompile: 1,
            gx2DrawDoneSync: 0,
            forceMeshShaders: 0,
            supportsVulkan: 0,
            supportsMetal: 1,
            overrideGamma: 0,
            overrideGammaValue: 2.2,
            displayGammaValue: 2.2,
            displayGammaIsSRGB: 1,
            upscaleFilter: 0,
            downscaleFilter: 0,
            fullscreenScaling: 0,
            audioApi: 3,
            audioDelay: 5,
            tvChannels: 1,
            padChannels: 1,
            inputChannels: 0,
            tvVolume: 100,
            padVolume: 100,
            inputVolume: 100,
            portalVolume: 100,
            overlayPosition: 1,
            overlayTextScale: 100,
            overlayTextColor: 0xFFFF_FFFF,
            overlayFps: 1,
            overlayDrawcalls: 0,
            overlayCpuUsage: 0,
            overlayCpuPerCoreUsage: 0,
            overlayRamUsage: 0,
            overlayVramUsage: 0,
            overlayDebug: 0,
            notificationPosition: 1,
            notificationTextScale: 100,
            notificationTextColor: 0xFFFF_FFFF,
            notificationControllerProfiles: 1,
            notificationControllerBattery: 0,
            notificationShaderCompiling: 1,
            notificationFriends: 1,
            activeAccountPersistentId: 1,
            activeAccountNetworkService: 0,
            crashDump: 0,
            gdbPort: 1337,
            framebufferFetch: 0
        )

        private var mlcPath: String = ""
        private var gpuCaptureDir: String = ""
        private var gamePaths: [String] = []
        private var accounts: [AccountEntry] = [
            AccountEntry(persistentId: 1, displayName: "Player")
        ]
        private var nextAccountId: UInt32 = 2

        init() {
            if let documentsPath = Self.documentsPath {
                mlcPath = documentsPath
                gpuCaptureDir = documentsPath
                gamePaths = [documentsPath]
            }
            loadFromDefaults()
        }

        func loadState() -> CemuSettingsState? {
            state
        }

        func saveState(_ state: CemuSettingsState, mlcPath: String, gpuCaptureDir: String) {
            self.state = state
            self.mlcPath = mlcPath
            self.gpuCaptureDir = gpuCaptureDir
            saveToDefaults()
        }

        func getMlcPath() -> String {
            mlcPath
        }

        func getDefaultMlcPath() -> String {
            Self.documentsPath ?? ""
        }

        func setMlcPath(_ path: String) -> Bool {
            mlcPath = path
            saveToDefaults()
            return true
        }

        func getGpuCaptureDir() -> String {
            gpuCaptureDir
        }

        func getDefaultGpuCaptureDir() -> String {
            Self.documentsPath ?? ""
        }

        func setGpuCaptureDir(_ path: String) -> Bool {
            gpuCaptureDir = path
            saveToDefaults()
            return true
        }

        func getGamePaths() -> [String] {
            gamePaths
        }

        func addGamePath(_ path: String) -> Bool {
            guard !path.isEmpty, !gamePaths.contains(path) else {
                return false
            }
            gamePaths.append(path)
            saveToDefaults()
            return true
        }

        func removeGamePath(at index: UInt64) -> Bool {
            guard index < UInt64(gamePaths.count) else {
                return false
            }
            gamePaths.remove(at: Int(index))
            saveToDefaults()
            return true
        }

        func getAccounts() -> [AccountEntry] {
            accounts
        }

        func createAccount(name: String) -> UInt32? {
            let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !trimmed.isEmpty else {
                return nil
            }
            let id = nextAccountId
            nextAccountId += 1
            accounts.append(AccountEntry(persistentId: id, displayName: trimmed))
            saveToDefaults()
            return id
        }

        func deleteAccount(_ persistentId: UInt32) -> Bool {
            guard accounts.count > 1 else {
                return false
            }
            let oldCount = accounts.count
            accounts.removeAll { $0.persistentId == persistentId }
            if accounts.count == oldCount {
                return false
            }
            saveToDefaults()
            return true
        }

        private static var documentsPath: String? {
            FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?.path
        }

        private func loadFromDefaults() {
            guard let data = defaults.data(forKey: Keys.payload) else {
                return
            }
            guard let payload = try? JSONDecoder().decode(Payload.self, from: data) else {
                return
            }

            state = payload.state
            mlcPath = payload.mlcPath
            gpuCaptureDir = payload.gpuCaptureDir
            gamePaths = payload.gamePaths
            accounts = payload.accounts.map { $0.accountEntry }
            nextAccountId = payload.nextAccountId

            if accounts.isEmpty {
                accounts = [AccountEntry(persistentId: 1, displayName: "Player")]
            }
            if nextAccountId <= (accounts.map(\.persistentId).max() ?? 1) {
                nextAccountId = (accounts.map(\.persistentId).max() ?? 1) + 1
            }
        }

        private func saveToDefaults() {
            let payload = Payload(
                state: state,
                mlcPath: mlcPath,
                gpuCaptureDir: gpuCaptureDir,
                gamePaths: gamePaths,
                accounts: accounts.map(AccountEntryPayload.init),
                nextAccountId: nextAccountId
            )
            guard let data = try? JSONEncoder().encode(payload) else {
                return
            }
            defaults.set(data, forKey: Keys.payload)
        }
    }

    final class IOSGameListBackend: GameListBackend {
        private struct ScannedGame {
            var titleId: UInt64
            var name: String
            var region: String
            var version: UInt16
            var dlc: UInt16
        }

        private let settingsBackend: IOSSettingsBackend
        private let queue = DispatchQueue(label: "cemu.swiftui.ios.game.scan", qos: .userInitiated)
        private let lock = NSLock()

        private var games: [ScannedGame] = []
        private var scanning = false

        init(settingsBackend: IOSSettingsBackend) {
            self.settingsBackend = settingsBackend
        }

        func create() {
            refresh()
        }

        func destroy() {
            lock.lock()
            defer { lock.unlock() }
            games.removeAll()
            scanning = false
        }

        func refresh() {
            lock.lock()
            scanning = true
            lock.unlock()

            queue.async { [weak self] in
                guard let self else {
                    return
                }
                let scannedGames = self.scanGames()
                self.lock.lock()
                self.games = scannedGames
                self.scanning = false
                self.lock.unlock()
            }
        }

        func count() -> UInt64 {
            lock.lock()
            defer { lock.unlock() }
            return UInt64(games.count)
        }

        func isScanning() -> Bool {
            lock.lock()
            defer { lock.unlock() }
            return scanning
        }

        func row(index: UInt64, outRow: UnsafeMutablePointer<CemuGameListRow>) -> Bool {
            lock.lock()
            defer { lock.unlock() }

            guard index < UInt64(games.count) else {
                return false
            }

            let game = games[Int(index)]
            outRow.pointee.titleId = game.titleId
            outRow.pointee.iconData = nil
            outRow.pointee.iconSize = 0
            outRow.pointee.name = strdup(game.name).map { UnsafePointer<CChar>($0) }
            outRow.pointee.region = strdup(game.region).map { UnsafePointer<CChar>($0) }
            outRow.pointee.version = game.version
            outRow.pointee.dlc = game.dlc
            return true
        }

        func freeBuffer(_ ptr: UnsafeMutableRawPointer?) {
            guard let ptr else {
                return
            }
            free(ptr)
        }

        func launchTitleById(_ titleId: UInt64) -> Bool {
            false
        }

        private func scanGames() -> [ScannedGame] {
            var roots = settingsBackend.getGamePaths()
            if roots.isEmpty,
                let documents = FileManager.default.urls(
                    for: .documentDirectory, in: .userDomainMask
                ).first?.path
            {
                roots = [documents]
            }

            var found: [ScannedGame] = []
            for rootPath in roots {
                let rootURL = URL(fileURLWithPath: rootPath, isDirectory: true)
                found.append(contentsOf: scanGames(in: rootURL))
            }

            found.sort { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
            return found
        }

        private func scanGames(in rootURL: URL) -> [ScannedGame] {
            guard
                let enumerator = FileManager.default.enumerator(
                    at: rootURL,
                    includingPropertiesForKeys: [.isRegularFileKey],
                    options: [.skipsHiddenFiles, .skipsPackageDescendants]
                )
            else {
                return []
            }

            let validExtensions = Set(["wua", "wud", "wux", "rpx", "elf", "iso"])
            var scanned: [ScannedGame] = []

            for case let fileURL as URL in enumerator {
                guard validExtensions.contains(fileURL.pathExtension.lowercased()) else {
                    continue
                }

                let baseName = fileURL.deletingPathExtension().lastPathComponent
                let id = stableTitleId(for: fileURL.path)
                scanned.append(
                    ScannedGame(
                        titleId: id,
                        name: baseName,
                        region: "UNK",
                        version: 0,
                        dlc: 0
                    )
                )
            }
            return scanned
        }

        private func stableTitleId(for string: String) -> UInt64 {
            let bytes = Array(string.utf8)
            var hash: UInt64 = 0xcbf2_9ce4_8422_2325
            for byte in bytes {
                hash ^= UInt64(byte)
                hash &*= 0x0000_0100_0000_01B3
            }
            return hash
        }
    }

    enum IOSBackends {
        static let settings = IOSSettingsBackend()
        static let gameList = IOSGameListBackend(settingsBackend: settings)
    }
#endif
