import SwiftUI

extension SettingsView {
    var audioTab: some View {
        Form {
            Section("General") {
                Picker("Audio API", selection: $store.state.audioApi) {
                    ForEach(AudioAPI.allCases) { api in
                        Text(api.title).tag(api.rawValue)
                    }
                }
                
                HStack {
                    Text("Latency: \(store.state.audioDelay * 12)ms")
                    Slider(
                        value: Binding(
                            get: { Double(store.state.audioDelay) },
                            set: { store.state.audioDelay = Int32($0.rounded()) }
                        ), in: 0...23, step: 1)
                }
            }
            
            Section("Volume") {
                sliderRow("TV", value: $store.state.tvVolume, step: 5)
                sliderRow("GamePad", value: $store.state.padVolume, step: 5)
                sliderRow("Microphone", value: $store.state.inputVolume, step: 5)
                if false {
                    sliderRow("Portal", value: $store.state.portalVolume, step: 5)
                }
            }
            
            Section("Channels") {
                Picker("TV", selection: $store.state.tvChannels) {
                    Text("Mono").tag(Int32(0))
                    Text("Stereo").tag(Int32(1))
                    Text("Surround").tag(Int32(2))
                }
                Picker("GamePad", selection: $store.state.padChannels) {
                    Text("Mono").tag(Int32(0))
                    Text("Stereo").tag(Int32(1))
                    Text("Surround").tag(Int32(2))
                }
                Picker("Microphone", selection: $store.state.inputChannels) {
                    Text("Mono").tag(Int32(0))
                    Text("Stereo").tag(Int32(1))
                    Text("Surround").tag(Int32(2))
                }
                .disabled(true)
            }
        }
        .formStyle(.grouped)
    }
}
