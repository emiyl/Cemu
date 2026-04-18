import SwiftUI

extension SettingsView {
    var debugTab: some View {
        Form {
            Section("Debug") {
                Picker("Crash dump", selection: $store.state.crashDump) {
                    Text("Disabled").tag(Int32(0))
                    Text("Lite/Enabled").tag(Int32(1))
                    Text("Full").tag(Int32(2))
                }
                HStack {
                    Text("GDB Stub Port")
                    Spacer()
                    TextField(
                        "", value: $store.state.gdbPort, formatter: NumberFormatter()
                    )
                    .textFieldStyle(.roundedBorder)
                    .onChange(of: store.state.gdbPort) {
                        store.state.gdbPort = min(max(store.state.gdbPort, 1024), 65535)
                    }
                    .frame(minWidth: 50, maxWidth: 80)
                }
                HStack {
                    Text("GPU Capture Directory")
                    Text(
                        store.gpuCaptureDir.isEmpty
                        ? store.defaultGpuCaptureDir : store.gpuCaptureDir
                    )
                    .lineLimit(1)
                    .truncationMode(.middle)
                    .foregroundStyle(.secondary)
                    Spacer()
                    Button("Select Folder") {
                        let panel = NSOpenPanel()
                        panel.canChooseFiles = false
                        panel.canChooseDirectories = true
                        panel.allowsMultipleSelection = false
                        if panel.runModal() == .OK, let url = panel.url {
                            store.gpuCaptureDir = url.path
                        }
                    }
                    Button("Clear") {
                        store.gpuCaptureDir = ""
                    }
                }
                Toggle(
                    "Framebuffer fetch", isOn: boolBinding(\CemuSettingsState.framebufferFetch))
            }
        }
        .formStyle(.grouped)
    }
}
