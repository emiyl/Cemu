import AppKit
import SwiftUI

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

@_silgen_name("CemuGameListCreate")
private func CemuGameListCreate()

@_silgen_name("CemuGameListDestroy")
private func CemuGameListDestroy()

@_silgen_name("CemuGameListRefresh")
private func CemuGameListRefresh()

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

private struct CemuGameListRow {
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
    let icon: NSImage?
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

    var body: some View {
        GeometryReader { proxy in
            let columns = makeGameListColumns(totalWidth: proxy.size.width)

            VStack(spacing: 0) {
                VStack(spacing: 0) {
                    GameListHeaderView(columns: columns)
                    Divider()
                    ScrollView {
                        LazyVStack(spacing: 0) {
                            ForEach(Array(games.enumerated()), id: \.element.id) { index, game in
                                GameListRowView(
                                    game: game,
                                    columns: columns,
                                    isSelected: selectedTitleID == game.titleID,
                                    isAlternateRow: index.isMultiple(of: 2)
                                )
                                .contentShape(Rectangle())
                                .onTapGesture(count: 2) {
                                    selectedTitleID = game.titleID
                                    launchGame(titleID: game.titleID)
                                }
                                .onTapGesture {
                                    selectedTitleID = game.titleID
                                }
                                .contextMenu {
                                    Button("Start") {
                                        selectedTitleID = game.titleID
                                        launchGame(titleID: game.titleID)
                                    }
                                }
                                Divider()
                            }
                        }
                    }
                    .background(Color(nsColor: .controlBackgroundColor))
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
            CemuGameListCreate()
            loadGamesFromProvider()
            installEnterKeyMonitorIfNeeded()
        }
        .onDisappear {
            CemuGameListDestroy()
            removeEnterKeyMonitor()
        }
    }

    private func refreshGameList() {
        withAnimation(.easeInOut(duration: 0.2)) {
            showUpdatingBanner = true
        }

        CemuGameListRefresh()
        loadGamesFromProvider()

        withAnimation(.easeInOut(duration: 0.2)) {
            showUpdatingBanner = false
        }
    }

    private func launchGame(titleID: UInt64) {
        _ = CemuSwiftUILaunchTitleById(titleID)
    }

    private func launchSelectedGame() {
        guard let selectedTitleID else {
            return
        }
        launchGame(titleID: selectedTitleID)
    }

    private func installEnterKeyMonitorIfNeeded() {
        guard keyEventMonitor == nil else {
            return
        }

        keyEventMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
            if event.keyCode == 36 || event.keyCode == 76 {
                launchSelectedGame()
                return nil
            }
            return event
        }
    }

    private func removeEnterKeyMonitor() {
        guard let keyEventMonitor else {
            return
        }

        NSEvent.removeMonitor(keyEventMonitor)
        self.keyEventMonitor = nil
    }

    private func loadGamesFromProvider() {
        var newGames: [GameItem] = []
        let count = CemuGameListGetCount()
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
            if CemuGameListGetRow(i, &row) {
                let iconPtr = row.iconData
                let regionPtr = row.region
                if let namePtr = row.name {
                    defer { CemuGameListFreeBuffer(UnsafeMutableRawPointer(mutating: namePtr)) }

                    let name = String(cString: namePtr)
                    let region = regionPtr.map { String(cString: $0) } ?? ""
                    var image: NSImage?
                    if let iconData = iconPtr, row.iconSize > 0 {
                        let iconDataBuffer = Data(bytes: iconData, count: Int(row.iconSize))
                        image = NSImage(data: iconDataBuffer)
                    }
                    if let iconPtr {
                        CemuGameListFreeBuffer(UnsafeMutableRawPointer(mutating: iconPtr))
                    }
                    if let regionPtr {
                        CemuGameListFreeBuffer(UnsafeMutableRawPointer(mutating: regionPtr))
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
                        CemuGameListFreeBuffer(UnsafeMutableRawPointer(mutating: iconPtr))
                    }
                    if let regionPtr {
                        CemuGameListFreeBuffer(UnsafeMutableRawPointer(mutating: regionPtr))
                    }
                }
            }
        }
        games = newGames
    }
}

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
        .background(Color(nsColor: .windowBackgroundColor))
    }
}

private struct GameListRowView: View {
    let game: GameItem
    let columns: [GameListColumn]
    let isSelected: Bool
    let isAlternateRow: Bool

    var body: some View {
        HStack(spacing: 0) {
            Group {
                if let icon = game.icon {
                    Image(nsImage: icon)
                        .resizable()
                        .interpolation(.high)
                        .scaledToFit()
                        .frame(width: 32, height: 32)
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
            return Color(nsColor: .selectedContentBackgroundColor)
        }
        if isAlternateRow {
            return Color(nsColor: .controlBackgroundColor)
        }
        return Color(nsColor: .windowBackgroundColor)
    }
}

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
        .background(Color(nsColor: .unemphasizedSelectedContentBackgroundColor))
    }
}
