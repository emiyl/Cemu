import SwiftUI

extension SettingsView {
    var graphicsTab: some View {
        Form {
            Section("Graphics API") {
                Picker("API", selection: $store.state.graphicApi) {
                    ForEach(store.availableGraphicsAPIs) { api in
                        Text(api.title).tag(api.rawValue)
                    }
                }
                
                Picker("VSync", selection: $store.state.vsync) {
                    Text("Off").tag(Int32(0))
                    Text("On").tag(Int32(1))
                    Text("Double buffering").tag(Int32(2))
                    Text("Match emulated display").tag(Int32(3))
                }
            }
            
            Section("Behavior") {
                Toggle("Async shader compile", isOn: boolBinding(\CemuSettingsState.asyncCompile))
                if store.state.supportsMetal != 0 {
                    Toggle(
                        "Force mesh shaders", isOn: boolBinding(\CemuSettingsState.forceMeshShaders)
                    )
                    .disabled(store.state.graphicApi == GraphicsAPI.vulkan.rawValue)
                }
            }
            
            if false {
                Section("Gamma") {
                    Toggle(
                        "Override game gamma preference",
                        isOn: boolBinding(\CemuSettingsState.overrideGamma))
                    HStack {
                        Text("Target Gamma")
                        Slider(
                            value: Binding(
                                get: { Double(store.state.overrideGammaValue) },
                                set: { store.state.overrideGammaValue = Float($0) }
                            ), in: 0.1...4.0)
                        Text(String(format: "%.2f", store.state.overrideGammaValue))
                            .frame(width: 48)
                    }
                    Toggle(
                        "Display uses sRGB curve",
                        isOn: boolBinding(\CemuSettingsState.displayGammaIsSRGB))
                    if store.state.displayGammaIsSRGB == 0 {
                        HStack {
                            Text("Display Gamma")
                            Slider(
                                value: Binding(
                                    get: { Double(store.state.displayGammaValue) },
                                    set: { store.state.displayGammaValue = Float($0) }
                                ), in: 0.1...4.0)
                            Text(String(format: "%.2f", store.state.displayGammaValue))
                                .frame(width: 48)
                        }
                    }
                }
            }
            
            Section("Filtering") {
                Picker("Upscale filter", selection: $store.state.upscaleFilter) {
                    Text("Bilinear").tag(Int32(0))
                    Text("Bicubic").tag(Int32(1))
                    Text("Hermite").tag(Int32(2))
                    Text("Nearest Neighbor").tag(Int32(3))
                }
                Picker("Downscale filter", selection: $store.state.downscaleFilter) {
                    Text("Bilinear").tag(Int32(0))
                    Text("Bicubic").tag(Int32(1))
                    Text("Hermite").tag(Int32(2))
                    Text("Nearest Neighbor").tag(Int32(3))
                }
                Picker("Fullscreen scaling", selection: $store.state.fullscreenScaling) {
                    Text("Keep aspect ratio").tag(Int32(0))
                    Text("Stretch").tag(Int32(1))
                }
            }
        }
        .formStyle(.grouped)
    }
}
