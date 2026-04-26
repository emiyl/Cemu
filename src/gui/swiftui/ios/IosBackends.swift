import Foundation

#if os(iOS)
    final class IOSGameAccessManager {
        static let shared = IOSGameAccessManager()
        static let bookmarkDefaultsKey = "cemuGamePathBookmarks"

        private var securityScopedURLs: [String: URL] = [:]

        private init() {
            restore()
        }

        private func restore() {
            guard let bookmarks = UserDefaults.standard.dictionary(forKey: Self.bookmarkDefaultsKey)
                as? [String: Data]
            else { return }

            var updatedBookmarks = bookmarks
            var changed = false
            for (path, bookmarkData) in bookmarks {
                var isStale = false

                guard let resolvedURL = try? URL(
                    resolvingBookmarkData: bookmarkData,
                    bookmarkDataIsStale: &isStale
                ) else {
                    continue
                }

                guard let url = resolvedURL else {
                    continue
                }

                if url.startAccessingSecurityScopedResource() {
                    securityScopedURLs[path] = url
                }

                if isStale, let newData = try? url.bookmarkData() {
                    updatedBookmarks[path] = newData
                    changed = true
                }
            }
            if changed {
                UserDefaults.standard.set(updatedBookmarks, forKey: Self.bookmarkDefaultsKey)
            }
        }

        func add(url: URL) -> String {
            let path = url.path.trimmingCharacters(in: .whitespacesAndNewlines)
            if url.startAccessingSecurityScopedResource() {
                securityScopedURLs[path] = url
            }
            if let bookmarkData = try? url.bookmarkData() {
                var bookmarks =
                    UserDefaults.standard.dictionary(forKey: Self.bookmarkDefaultsKey)
                    as? [String: Data] ?? [:]
                bookmarks[path] = bookmarkData
                UserDefaults.standard.set(bookmarks, forKey: Self.bookmarkDefaultsKey)
            }
            return path
        }

        func remove(path: String) {
            securityScopedURLs[path]?.stopAccessingSecurityScopedResource()
            securityScopedURLs.removeValue(forKey: path)
            if var bookmarks = UserDefaults.standard.dictionary(forKey: Self.bookmarkDefaultsKey)
                as? [String: Data]
            {
                bookmarks.removeValue(forKey: path)
                UserDefaults.standard.set(bookmarks, forKey: Self.bookmarkDefaultsKey)
            }
        }
    }

    enum IOSBackends {
        static let settings: SettingsBackend = CemuSettingsBackend()
        static let gameList: GameListBackend = CemuBackend()
    }
#endif
