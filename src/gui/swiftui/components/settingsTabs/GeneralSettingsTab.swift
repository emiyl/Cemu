import SwiftUI

extension SettingsView {
    var generalTab: some View {
        Form {
            Section("Interface") {
                Picker("Language", selection: $store.state.language) {
                    ForEach(store.availableLanguages) { language in
                        Text(language.title).tag(language.id)
                    }
                }
                Toggle("Discord Presence", isOn: boolBinding(\CemuSettingsState.useDiscordPresence))
                Toggle("Enable intro sound", isOn: boolBinding(\CemuSettingsState.playBootSound))
                Toggle("Save screenshot", isOn: boolBinding(\CemuSettingsState.saveScreenshots))
                Toggle(
                    "Automatically check for updates",
                    isOn: boolBinding(\CemuSettingsState.checkForUpdates))
                Toggle(
                    "Receive untested updates",
                    isOn: boolBinding(\CemuSettingsState.receiveUntestedUpdates))
            }
            
            Section {
                HStack {
                    let displayPath = store.mlcPath.isEmpty ? store.defaultMlcPath : store.mlcPath
                    Text(displayPath)
                        .lineLimit(1)
                        .truncationMode(.middle)
                        .foregroundStyle(.secondary)
                    Spacer()
                    HStack {
                        Button("Select Folder") {
                            let panel = NSOpenPanel()
                            panel.canChooseFiles = false
                            panel.canChooseDirectories = true
                            panel.allowsMultipleSelection = false
                            if panel.runModal() == .OK, let url = panel.url {
                                store.mlcPath = url.path
                            }
                        }
                        Button("Reset") {
                            store.mlcPath = ""
                        }
                    }
                }
            } header: {
                VStack(alignment: .leading, spacing: 2) {
                    Text("MLC Path")
                    Text(
                        "Configure the path for the emulated internal Wii U storage (MLC). This is where Cemu stores saves, accounts, and other Wii U system files."
                    )
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                }
            } footer: {
                if false {
                    Button("Open Folder") {
                        let displayPath =
                        store.mlcPath.isEmpty ? store.defaultMlcPath : store.mlcPath
                        NSWorkspace.shared.open(URL(fileURLWithPath: displayPath))
                    }
                }
            }
            
            Section("Game Paths") {
                List(selection: $store.selectedGamePath) {
                    ForEach(store.gamePaths, id: \.self) { path in
                        Text(path)
                            .lineLimit(1)
                    }
                }
                .frame(height: CGFloat(max(store.gamePaths.count, 1)) * 26.0)
                
                HStack {
                    Button("Add Path") {
                        store.addGamePath()
                    }
                    Button("Remove") {
                        store.removeSelectedGamePath()
                    }
                    .disabled(store.selectedGamePath == nil)
                }
            }
        }
        .formStyle(.grouped)
    }
}
