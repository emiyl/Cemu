import Foundation

#if os(iOS)
    enum IOSBackends {
        static let settings: SettingsBackend = CemuSettingsBackend()
        static let gameList: GameListBackend = CemuBackend()
    }
#endif
