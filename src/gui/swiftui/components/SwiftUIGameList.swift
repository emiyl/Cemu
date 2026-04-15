import AppKit
import SwiftUI

private struct GameListColumn {
    let title: String
    let width: CGFloat
    let alignment: Alignment
}

private let gameListColumns: [GameListColumn] = [
    GameListColumn(title: "Title ID", width: 200, alignment: .leading),
    GameListColumn(title: "Name", width: 340, alignment: .leading),
]

@_silgen_name("CemuSwiftUIGameListCreate")
private func CemuSwiftUIGameListCreate()

@_silgen_name("CemuSwiftUIGameListDestroy")
private func CemuSwiftUIGameListDestroy()

@_silgen_name("CemuSwiftUIGameListRefresh")
private func CemuSwiftUIGameListRefresh()

@_silgen_name("CemuSwiftUIGameListGetCount")
private func CemuSwiftUIGameListGetCount() -> UInt64

@_silgen_name("CemuSwiftUIGameListGetRow")
private func CemuSwiftUIGameListGetRow(
    _ index: UInt64, _ outRow: UnsafeMutablePointer<CemuSwiftUIGameListRow>
) -> Bool

private struct CemuSwiftUIGameListRow {
    var titleId: UInt64
    var name: UnsafePointer<CChar>?
}

struct SwiftUIGameList: View {
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
            CemuSwiftUIGameListCreate()
            loadGamesFromProvider()
        }
        .onDisappear {
            CemuSwiftUIGameListDestroy()
        }
    }

    private func refreshGameList() {
        withAnimation(.easeInOut(duration: 0.2)) {
            showUpdatingBanner = true
        }

        CemuSwiftUIGameListRefresh()
        loadGamesFromProvider()

        withAnimation(.easeInOut(duration: 0.2)) {
            showUpdatingBanner = false
        }
    }

    private func loadGamesFromProvider() {
        var newGames: [GameItem] = []
        let count = CemuSwiftUIGameListGetCount()
        for i in 0..<count {
            var row = CemuSwiftUIGameListRow(titleId: 0, name: nil)
            if CemuSwiftUIGameListGetRow(i, &row), let namePtr = row.name {
                let name = String(cString: namePtr)
                newGames.append(GameItem(titleID: row.titleId, name: name))
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
            Text(String(format: "%016llx", game.titleID))
                .font(.system(size: 12))
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(width: gameListColumns[0].width, alignment: gameListColumns[0].alignment)
            Text(game.name)
                .font(.system(size: 12))
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(width: gameListColumns[1].width, alignment: gameListColumns[1].alignment)
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

struct GameItem: Identifiable {
    let titleID: UInt64
    let name: String

    var id: UInt64 { titleID }
}
