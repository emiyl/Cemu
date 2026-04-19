import SwiftUI

#if os(macOS)
    import AppKit
    typealias PlatformImage = NSImage
    typealias PlatformColor = NSColor
    typealias PlatformViewRepresentable = NSViewRepresentable
    typealias PlatformView = NSView
#else
    import UIKit
    typealias PlatformImage = UIImage
    typealias PlatformColor = UIColor
    typealias PlatformViewRepresentable = UIViewRepresentable
    typealias PlatformView = UIView
#endif

extension Color {
    #if os(macOS)
        fileprivate init(platformColor: NSColor) {
            self.init(nsColor: platformColor)
        }
    #else
        fileprivate init(platformColor: UIColor) {
            self.init(uiColor: platformColor)
        }
    #endif
}

extension PlatformColor {
    fileprivate static var windowBackground: PlatformColor {
        #if os(macOS)
            .windowBackgroundColor
        #else
            .systemBackground
        #endif
    }

    fileprivate static var controlBackground: PlatformColor {
        #if os(macOS)
            .controlBackgroundColor
        #else
            .secondarySystemBackground
        #endif
    }

    fileprivate static var selectedBackground: PlatformColor {
        #if os(macOS)
            .selectedContentBackgroundColor
        #else
            .secondarySystemFill
        #endif
    }

    fileprivate static var infoBarBackground: PlatformColor {
        #if os(macOS)
            .unemphasizedSelectedContentBackgroundColor
        #else
            .tertiarySystemFill
        #endif
    }
}

private struct GameListColumn {
    let title: String
    let width: CGFloat
    let alignment: Alignment
}

private enum GameListColumnIndex {
    static let icon = 0
    static let name = 1
    static let version = 2
    static let dlc = 3
    static let region = 4
    static let titleID = 5
}

private func makeGameListColumns(totalWidth: CGFloat) -> [GameListColumn] {
    let iconWidth: CGFloat = 48
    let contentWidth = max(0, totalWidth - iconWidth)

    return [
        GameListColumn(title: "", width: iconWidth, alignment: .center),
        GameListColumn(title: "Name", width: contentWidth * 0.46, alignment: .leading),
        GameListColumn(title: "Version", width: contentWidth * 0.12, alignment: .center),
        GameListColumn(title: "DLC", width: contentWidth * 0.10, alignment: .center),
        GameListColumn(title: "Region", width: contentWidth * 0.10, alignment: .center),
        GameListColumn(title: "Title ID", width: contentWidth * 0.22, alignment: .leading),
    ]
}

#if !os(iOS)
    @_silgen_name("CemuGameListCreate")
    private func CemuGameListCreate()

    @_silgen_name("CemuGameListDestroy")
    private func CemuGameListDestroy()

    @_silgen_name("CemuGameListRefresh")
    private func CemuGameListRefresh()

    @_silgen_name("CemuGameListIsScanning")
    private func CemuGameListIsScanning() -> Bool

    @_silgen_name("CemuGameListGetCount")
    private func CemuGameListGetCount() -> UInt64

    @_silgen_name("CemuGameListGetRow")
    private func CemuGameListGetRow(
        _ index: UInt64, _ outRow: UnsafeMutablePointer<CemuGameListRow>
    ) -> Bool

    @_silgen_name("CemuGameListFreeBuffer")
    private func CemuGameListFreeBuffer(_ ptr: UnsafeMutableRawPointer?)

    @_silgen_name("CemuSwiftUILaunchTitleById")
    private func CemuSwiftUILaunchTitleById(_ titleId: UInt64) -> Bool

    final class CemuBackend: GameListBackend {
        func create() {
            CemuGameListCreate()
        }

        func destroy() {
            CemuGameListDestroy()
        }

        func refresh() {
            CemuGameListRefresh()
        }

        func count() -> UInt64 {
            CemuGameListGetCount()
        }

        func isScanning() -> Bool {
            CemuGameListIsScanning()
        }

        func row(index: UInt64, outRow: UnsafeMutablePointer<CemuGameListRow>) -> Bool {
            CemuGameListGetRow(index, outRow)
        }

        func freeBuffer(_ ptr: UnsafeMutableRawPointer?) {
            CemuGameListFreeBuffer(ptr)
        }

        func launchTitleById(_ titleId: UInt64) -> Bool {
            CemuSwiftUILaunchTitleById(titleId)
        }
    }
#endif

protocol GameListBackend {
    func create()
    func destroy()
    func refresh()
    func count() -> UInt64
    func isScanning() -> Bool
    func row(index: UInt64, outRow: UnsafeMutablePointer<CemuGameListRow>) -> Bool
    func freeBuffer(_ ptr: UnsafeMutableRawPointer?)
    func launchTitleById(_ titleId: UInt64) -> Bool
}

final class MockBackend: GameListBackend {
    func create() {}
    func destroy() {}
    func refresh() {}

    func count() -> UInt64 {
        3
    }

    func isScanning() -> Bool {
        false
    }

    func row(index: UInt64, outRow: UnsafeMutablePointer<CemuGameListRow>) -> Bool {
        struct Game {
            var titleId: UInt64
            var name: String
            var region: String
            var version: UInt16
            var dlc: UInt16
        }
        let games = [
            Game(
                titleId: 0x0005_0000_1010_ed00, name: "MARIO KART 8",
                region: "EUR", version: 81, dlc: 80
            ),
            Game(
                titleId: 0x0005_0000_101c_9500, name: "The Legend of Zelda Breath of the Wild",
                region: "EUR", version: 0, dlc: 0
            ),
            Game(
                titleId: 0x0005_0000_1014_3600, name: "THE LEGEND OF ZELDA The Wind Waker HD",
                region: "EUR", version: 208, dlc: 80
            ),
        ]

        guard index < games.count else {
            return false
        }

        let game = games[Int(index)]
        outRow.pointee.titleId = game.titleId
        outRow.pointee.name = strdup(game.name).map { UnsafePointer<CChar>($0) }
        outRow.pointee.region = strdup(game.region).map { UnsafePointer<CChar>($0) }
        outRow.pointee.version = game.version
        outRow.pointee.dlc = game.dlc
        return true
    }

    func freeBuffer(_ ptr: UnsafeMutableRawPointer?) {
        guard let ptr else { return }
        free(ptr)
    }

    func launchTitleById(_ titleId: UInt64) -> Bool {
        false
    }
}

struct CemuGameListRow {
    var titleId: UInt64
    var iconData: UnsafePointer<UInt8>?
    var iconSize: UInt
    var name: UnsafePointer<CChar>?
    var region: UnsafePointer<CChar>?
    var version: UInt16
    var dlc: UInt16
}

struct GameItem: Identifiable {
    let titleID: UInt64
    let icon: PlatformImage?
    let name: String
    let version: UInt16
    let dlc: UInt16
    let region: String

    var id: UInt64 { titleID }
}

struct GameList: View {
    @State private var selectedTitleID: UInt64?
    @State private var showUpdatingBanner = false
    @State private var games: [GameItem] = []
    @State private var keyEventMonitor: Any?
    @State private var refreshRequestID = 0
    let backend: GameListBackend

    var body: some View {
        GeometryReader { proxy in
            let columns = makeGameListColumns(totalWidth: proxy.size.width)

            VStack(spacing: 0) {
                VStack(spacing: 0) {
                    GameListHeaderView(columns: columns)
                    Divider()
                    ScrollView {
                        LazyVStack(spacing: 0) {
                            ForEach(games.indices, id: \.self) { index in
                                let game = games[index]
                                GameListRowView(
                                    game: game,
                                    columns: columns,
                                    isSelected: selectedTitleID == game.titleID,
                                    isAlternateRow: index.isMultiple(of: 2)
                                )
                                .overlay(
                                    GameListRowInteractionView(
                                        onSelect: {
                                            selectedTitleID = game.titleID
                                        },
                                        onLaunch: {
                                            selectedTitleID = game.titleID
                                            launchGame(titleID: game.titleID)
                                        }
                                    )
                                )
                                Divider()
                            }
                        }
                    }
                    .background(Color(platformColor: .controlBackground))
                }

                if showUpdatingBanner {
                    GameListInfoBarView(message: "Updating game list...") {
                        withAnimation(.easeInOut(duration: 0.2)) {
                            showUpdatingBanner = false
                        }
                    }
                    .transition(.move(edge: .bottom).combined(with: .opacity))
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .toolbar {
            ToolbarItemGroup(placement: .automatic) {
                Button(action: refreshGameList) {
                    Image(systemName: "arrow.clockwise")
                }
                .help("Refresh game list")
            }
        }
        .onAppear {
            backend.create()
            loadGamesFromProvider()
        }
        .onDisappear {
            backend.destroy()
            refreshRequestID += 1
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        #if os(iOS)
            .background(Color(uiColor: .systemBackground))
        #endif
    }

    private func refreshGameList() {
        refreshRequestID += 1
        let currentRefreshRequestID = refreshRequestID

        withAnimation(.easeInOut(duration: 0.2)) {
            showUpdatingBanner = true
        }

        backend.refresh()
        waitForRefreshCompletion(requestID: currentRefreshRequestID)
    }

    private func waitForRefreshCompletion(requestID: Int) {
        guard requestID == refreshRequestID else {
            return
        }

        if !backend.isScanning() {
            loadGamesFromProvider {
                guard requestID == refreshRequestID else {
                    return
                }

                withAnimation(.easeInOut(duration: 0.2)) {
                    showUpdatingBanner = false
                }
            }
            return
        }

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            waitForRefreshCompletion(requestID: requestID)
        }
    }

    private func launchGame(titleID: UInt64) {
        _ = backend.launchTitleById(titleID)
    }

    private func launchSelectedGame() {
        guard let selectedTitleID else {
            return
        }
        launchGame(titleID: selectedTitleID)
    }

    private func loadGamesFromProvider(completion: (() -> Void)? = nil) {
        DispatchQueue.global(qos: .userInitiated).async {
            var newGames: [GameItem] = []
            let count = backend.count()
            for i in 0..<count {
                var row = CemuGameListRow(
                    titleId: 0,
                    iconData: nil,
                    iconSize: 0,
                    name: nil,
                    region: nil,
                    version: 0,
                    dlc: 0
                )
                if backend.row(index: i, outRow: &row) {
                    let iconPtr = row.iconData
                    let regionPtr = row.region
                    if let namePtr = row.name {
                        defer { backend.freeBuffer(UnsafeMutableRawPointer(mutating: namePtr)) }

                        let name = String(cString: namePtr)
                        let region = regionPtr.map { String(cString: $0) } ?? ""
                        var image: PlatformImage?
                        if let iconData = iconPtr, row.iconSize > 0 {
                            let iconDataBuffer = Data(bytes: iconData, count: Int(row.iconSize))
                            image = PlatformImage(data: iconDataBuffer)
                        }
                        if let iconPtr {
                            backend.freeBuffer(UnsafeMutableRawPointer(mutating: iconPtr))
                        }
                        if let regionPtr {
                            backend.freeBuffer(UnsafeMutableRawPointer(mutating: regionPtr))
                        }
                        newGames.append(
                            GameItem(
                                titleID: row.titleId,
                                icon: image,
                                name: name,
                                version: row.version,
                                dlc: row.dlc,
                                region: region
                            ))
                    } else {
                        if let iconPtr {
                            backend.freeBuffer(UnsafeMutableRawPointer(mutating: iconPtr))
                        }
                        if let regionPtr {
                            backend.freeBuffer(UnsafeMutableRawPointer(mutating: regionPtr))
                        }
                    }
                }
            }

            DispatchQueue.main.async {
                self.games = newGames
                completion?()
            }
        }
    }
}

#if !os(iOS)
    #Preview {
        GameList(backend: MockBackend())
    }
#endif

private struct GameListHeaderView: View {
    let columns: [GameListColumn]

    var body: some View {
        HStack(spacing: 0) {
            ForEach(columns.indices, id: \.self) { index in
                let column = columns[index]
                Text(column.title)
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundStyle(.primary)
                    .lineLimit(1)
                    .frame(width: column.width, alignment: column.alignment)
            }
        }
        .padding(.vertical, 4)
        .background(Color(platformColor: .windowBackground))
    }
}

private struct GameListRowView: View {
    let game: GameItem
    let columns: [GameListColumn]
    let isSelected: Bool
    let isAlternateRow: Bool

    @ViewBuilder
    private func gameIconView(_ icon: PlatformImage) -> some View {
        #if os(macOS)
            let image = Image(nsImage: icon)
        #else
            let image = Image(uiImage: icon)
        #endif
        image
            .resizable()
            .interpolation(.high)
            .scaledToFit()
            .frame(width: 32, height: 32)
    }

    var body: some View {
        HStack(spacing: 0) {
            Group {
                if let icon = game.icon {
                    gameIconView(icon)
                } else {
                    Image(systemName: "gamecontroller")
                        .font(.system(size: 16, weight: .regular))
                        .foregroundStyle(.secondary)
                        .frame(width: 32, height: 32)
                }
            }
            .frame(
                width: columns[GameListColumnIndex.icon].width,
                alignment: columns[GameListColumnIndex.icon].alignment)
            Text(game.name)
                .font(.system(size: 12))
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(
                    width: columns[GameListColumnIndex.name].width,
                    alignment: columns[GameListColumnIndex.name].alignment)
            Text(String(format: "%u", game.version))
                .font(.system(size: 12))
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(
                    width: columns[GameListColumnIndex.version].width,
                    alignment: columns[GameListColumnIndex.version].alignment)
            Text(game.dlc > 0 ? String(format: "%u", game.dlc) : "-")
                .font(.system(size: 12))
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(
                    width: columns[GameListColumnIndex.dlc].width,
                    alignment: columns[GameListColumnIndex.dlc].alignment)
            Text(game.region)
                .font(.system(size: 12))
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(
                    width: columns[GameListColumnIndex.region].width,
                    alignment: columns[GameListColumnIndex.region].alignment)
            Text(String(format: "%016llx", game.titleID))
                .font(.system(size: 12))
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(
                    width: columns[GameListColumnIndex.titleID].width,
                    alignment: columns[GameListColumnIndex.titleID].alignment)
        }
        .frame(maxWidth: .infinity, minHeight: 40, alignment: .leading)
        .background(backgroundColor)
    }

    private var backgroundColor: Color {
        if isSelected {
            return Color(platformColor: .selectedBackground)
        }
        if isAlternateRow {
            return Color(platformColor: .controlBackground)
        }
        return Color(platformColor: .windowBackground)
    }
}

private struct GameListRowInteractionView: PlatformViewRepresentable {
    let onSelect: () -> Void
    let onLaunch: () -> Void

    func makeView() -> InteractionView {
        let view = InteractionView()
        view.configure(onSelect: onSelect, onLaunch: onLaunch)
        return view
    }

    func updateView(_ view: InteractionView) {
        view.configure(onSelect: onSelect, onLaunch: onLaunch)
    }

    #if os(macOS)
        func makeNSView(context: Context) -> InteractionView { makeView() }
        func updateNSView(_ nsView: InteractionView, context: Context) { updateView(nsView) }
    #else
        func makeUIView(context: Context) -> InteractionView { makeView() }
        func updateUIView(_ uiView: InteractionView, context: Context) { updateView(uiView) }
    #endif
}

private final class InteractionView: PlatformView {
    private var onSelect: (() -> Void)?
    private var onLaunch: (() -> Void)?

    #if os(iOS)
        override init(frame: CGRect) {
            super.init(frame: frame)
            backgroundColor = .clear

            let doubleTap = UITapGestureRecognizer(target: self, action: #selector(handleDoubleTap))
            doubleTap.numberOfTapsRequired = 2

            let singleTap = UITapGestureRecognizer(target: self, action: #selector(handleSingleTap))
            singleTap.require(toFail: doubleTap)

            addGestureRecognizer(doubleTap)
            addGestureRecognizer(singleTap)
        }

        @available(*, unavailable)
        required init?(coder: NSCoder) {
            fatalError("init(coder:) has not been implemented")
        }
    #endif

    func configure(onSelect: @escaping () -> Void, onLaunch: @escaping () -> Void) {
        self.onSelect = onSelect
        self.onLaunch = onLaunch
    }
}

#if os(macOS)
    extension InteractionView {
        override var acceptsFirstResponder: Bool { true }

        override func hitTest(_ point: NSPoint) -> NSView? { self }

        override func mouseDown(with event: NSEvent) {
            if event.clickCount >= 2 {
                onLaunch?()
            } else {
                onSelect?()
            }
        }

        override func menu(for event: NSEvent) -> NSMenu? {
            let menu = NSMenu()
            let item = NSMenuItem(
                title: "Start",
                action: #selector(startAction),
                keyEquivalent: ""
            )
            item.target = self
            menu.addItem(item)
            return menu
        }

        @objc private func startAction() {
            onSelect?()
            onLaunch?()
        }
    }
#else
    extension InteractionView {
        override func point(inside point: CGPoint, with event: UIEvent?) -> Bool { true }

        @objc private func handleSingleTap() {
            onSelect?()
        }

        @objc private func handleDoubleTap() {
            onSelect?()
            onLaunch?()
        }
    }
#endif

struct GameListInfoBarView: View {
    let message: String
    let onDismiss: () -> Void

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: "arrow.triangle.2.circlepath")
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(.secondary)

            Text(message)
                .font(.system(size: 12))

            Spacer()

            Button("Dismiss", action: onDismiss)
                .buttonStyle(.plain)
                .font(.system(size: 12, weight: .medium))
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 7)
        .background(Color(platformColor: .infoBarBackground))
    }
}
