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
    @State private var selectedGame: String? = nil
    @State private var games: [GameItem] = [
        GameItem(id: 1, name: "The Legend of Zelda: Breath of the Wild", developer: "Nintendo EPD"),
        GameItem(id: 2, name: "Mario Kart 8", developer: "Nintendo EAD"),
        GameItem(id: 3, name: "Splatoon 2", developer: "Nintendo EPD"),
        GameItem(id: 4, name: "Super Smash Bros. for Wii U", developer: "Bandai Namco Studios"),
    ]

    var body: some View {
        NavigationSplitView {
            List(games, id: \.id, selection: $selectedGame) { game in
                GameListItemView(game: game)
                    .tag(String(game.id))
            }
            .navigationTitle("Games")
            .toolbar {
                ToolbarItem(placement: .primaryAction) {
                    HStack {
                        Button(action: { /* Refresh game list */  }) {
                            Image(systemName: "arrow.clockwise")
                        }
                        .help("Refresh game list")

                        Button(action: { /* Open settings */  }) {
                            Image(systemName: "gear")
                        }
                        .help("Settings")
                    }
                }
            }
        } detail: {
            if let selectedId = selectedGame,
                let game = games.first(where: { String($0.id) == selectedId })
            {
                GameDetailView(game: game)
            } else {
                VStack {
                    Image(systemName: "gamecontroller")
                        .font(.system(size: 64))
                        .foregroundColor(.gray)
                    Text("Select a game to play")
                        .font(.headline)
                        .padding()
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(Color(.controlBackgroundColor))
            }
        }
        .frame(minWidth: 400, minHeight: 300)
    }
}

struct GameListItemView: View {
    let game: GameItem

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(game.name)
                .font(.headline)
            Text(game.developer)
                .font(.caption)
                .foregroundColor(.gray)
        }
        .padding(.vertical, 4)
    }
}

struct GameDetailView: View {
    let game: GameItem

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack(spacing: 16) {
                RoundedRectangle(cornerRadius: 12)
                    .fill(Color.blue.opacity(0.3))
                    .frame(width: 160, height: 240)
                    .overlay(
                        Image(systemName: "photo")
                            .font(.system(size: 48))
                            .foregroundColor(.gray)
                    )

                VStack(alignment: .leading, spacing: 8) {
                    Text(game.name)
                        .font(.title2)
                        .fontWeight(.bold)

                    Text(game.developer)
                        .font(.body)
                        .foregroundColor(.gray)

                    Spacer()

                    Button(action: { /* Play game */  }) {
                        Label("Play", systemImage: "play.fill")
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 8)
                    }
                    .buttonStyle(.borderedProminent)
                }
            }
            .padding()

            Divider()

            Text("About")
                .font(.headline)
                .padding(.horizontal)

            Text("Placeholder for game information and controls.")
                .font(.body)
                .foregroundColor(.gray)
                .padding(.horizontal)

            Spacer()
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(Color(.controlBackgroundColor))
    }
}

struct GameItem: Identifiable {
    let id: Int
    let name: String
    let developer: String
}
