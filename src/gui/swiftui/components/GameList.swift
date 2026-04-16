import AppKit
import SwiftUI

private struct GameListColumn {
    let title: String
    let width: CGFloat
    let alignment: Alignment
}

private let gameListColumns: [GameListColumn] = [
    GameListColumn(title: "", width: 48, alignment: .center),
    GameListColumn(title: "Name", width: 340, alignment: .leading),
    GameListColumn(title: "Version", width: 80, alignment: .center),
    GameListColumn(title: "DLC", width: 80, alignment: .center),
    GameListColumn(title: "Region", width: 80, alignment: .center),
    GameListColumn(title: "Title ID", width: 200, alignment: .leading),
]

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

private struct CemuGameListRow {
    var titleId: UInt64
    var iconData: UnsafePointer<UInt8>?
    var iconSize: UInt
    var name: UnsafePointer<CChar>?
    var version: UInt16
    var dlc: UInt16
    var region: Int16
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

    var body: some View {
        VStack(spacing: 0) {
            ScrollView(.horizontal) {
                VStack(spacing: 0) {
                    GameListHeaderView()
                    Divider()
                    ScrollView {
                        LazyVStack(spacing: 0) {
                            ForEach(Array(games.enumerated()), id: \.element.id) { index, game in
                                GameListRowView(
                                    game: game,
                                    isSelected: selectedTitleID == game.titleID,
                                    isAlternateRow: index.isMultiple(of: 2)
                                )
                                .contentShape(Rectangle())
                                .onTapGesture {
                                    selectedTitleID = game.titleID
                                }
                                Divider()
                            }
                        }
                    }
                    .background(Color(nsColor: .controlBackgroundColor))
                }
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
        }
        .onDisappear {
            CemuGameListDestroy()
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

    private func loadGamesFromProvider() {
        var newGames: [GameItem] = []
        let count = CemuGameListGetCount()
        for i in 0..<count {
            var row = CemuGameListRow(
                titleId: 0,
                iconData: nil,
                iconSize: 0,
                name: nil,
                version: 0,
                dlc: 0,
                region: 0
            )
            if CemuGameListGetRow(i, &row) {
                if let iconPtr = row.iconData {
                    defer { CemuGameListFreeBuffer(UnsafeMutableRawPointer(mutating: iconPtr)) }
                }
                if let namePtr = row.name {
                    defer { CemuGameListFreeBuffer(UnsafeMutableRawPointer(mutating: namePtr)) }

                    let name = String(cString: namePtr)
                    let region = row.region != 0 ? String(format: "%d", row.region) : ""
                    var image: NSImage?
                    if let iconData = row.iconData, row.iconSize > 0 {
                        let iconDataBuffer = Data(bytes: iconData, count: Int(row.iconSize))
                        image = NSImage(data: iconDataBuffer)
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
                }
            }
        }
        games = newGames
    }
}

struct GameListHeaderView: View {
    var body: some View {
        HStack(spacing: 0) {
            ForEach(gameListColumns, id: \.title) { column in
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

struct GameListRowView: View {
    let game: GameItem
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
            .frame(width: gameListColumns[0].width, alignment: gameListColumns[0].alignment)
            Text(game.name)
                .font(.system(size: 12))
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(width: gameListColumns[1].width, alignment: gameListColumns[1].alignment)
            Text(String(format: "%u", game.version))
                .font(.system(size: 12))
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(width: gameListColumns[2].width, alignment: gameListColumns[2].alignment)
            Text(game.dlc > 0 ? String(format: "%u", game.dlc) : "-")
                .font(.system(size: 12))
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(width: gameListColumns[3].width, alignment: gameListColumns[3].alignment)
            Text(game.region)
                .font(.system(size: 12))
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(width: gameListColumns[4].width, alignment: gameListColumns[4].alignment)
            Text(String(format: "%016llx", game.titleID))
                .font(.system(size: 12))
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(width: gameListColumns[5].width, alignment: gameListColumns[5].alignment)
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
