import SwiftUI

extension SettingsView {
    var overlayTab: some View {
        Form {
            Section("Overlay") {
                Picker("Position", selection: $store.state.overlayPosition) {
                    ForEach(SettingsPosition.allCases) { option in
                        Text(option.title).tag(option.rawValue)
                    }
                }
                HStack {
                    Text("Scale")
                    Slider(
                        value: Binding(
                            get: { Double(store.state.overlayTextScale) },
                            set: {
                                let snapped = (Int32(($0 / 25.0).rounded()) * 25)
                                store.state.overlayTextScale = min(300, max(50, snapped))
                            }
                        ),
                        in: 50...300,
                        step: 25
                    )
                    Text("\(store.state.overlayTextScale)%")
                        .frame(width: 52, alignment: .trailing)
                }
                colorRow("Text Color", value: $store.state.overlayTextColor)
                Toggle("FPS", isOn: boolBinding(\CemuSettingsState.overlayFps))
                Toggle("Draw calls", isOn: boolBinding(\CemuSettingsState.overlayDrawcalls))
                Toggle("CPU usage", isOn: boolBinding(\CemuSettingsState.overlayCpuUsage))
                Toggle("CPU per core", isOn: boolBinding(\CemuSettingsState.overlayCpuPerCoreUsage))
                Toggle("RAM usage", isOn: boolBinding(\CemuSettingsState.overlayRamUsage))
                Toggle("VRAM usage", isOn: boolBinding(\CemuSettingsState.overlayVramUsage))
                Toggle("Debug", isOn: boolBinding(\CemuSettingsState.overlayDebug))
            }
            
            Section("Notifications") {
                Picker("Position", selection: $store.state.notificationPosition) {
                    ForEach(SettingsPosition.allCases) { option in
                        Text(option.title).tag(option.rawValue)
                    }
                }
                HStack {
                    Text("Scale")
                    Slider(
                        value: Binding(
                            get: { Double(store.state.notificationTextScale) },
                            set: {
                                let snapped = (Int32(($0 / 25.0).rounded()) * 25)
                                store.state.notificationTextScale = min(300, max(50, snapped))
                            }
                        ),
                        in: 50...300,
                        step: 25
                    )
                    Text("\(store.state.notificationTextScale)%")
                        .frame(width: 52, alignment: .trailing)
                }
                colorRow("Text Color", value: $store.state.notificationTextColor)
                Toggle(
                    "Controller profiles",
                    isOn: boolBinding(\CemuSettingsState.notificationControllerProfiles))
                Toggle(
                    "Low battery",
                    isOn: boolBinding(\CemuSettingsState.notificationControllerBattery))
                Toggle(
                    "Shader compiling",
                    isOn: boolBinding(\CemuSettingsState.notificationShaderCompiling))
                Toggle("Friend list", isOn: boolBinding(\CemuSettingsState.notificationFriends))
            }
        }
        .formStyle(.grouped)
    }
}
