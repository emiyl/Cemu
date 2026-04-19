import SwiftUI

extension SettingsView {
    var accountTab: some View {
        Form {
            Section("Accounts") {
                Picker("Active account", selection: $store.state.activeAccountPersistentId) {
                    ForEach(store.accounts) { account in
                        Text(account.displayName).tag(account.persistentId)
                    }
                }
                .disabled(store.state.isTitleRunning != 0)
                
                Picker("Network Service", selection: $store.state.activeAccountNetworkService) {
                    Text(NetworkServiceOption.offline.title).tag(
                        NetworkServiceOption.offline.rawValue)
                    Text(NetworkServiceOption.nintendo.title).tag(
                        NetworkServiceOption.nintendo.rawValue)
                    Text(NetworkServiceOption.pretendo.title).tag(
                        NetworkServiceOption.pretendo.rawValue)
                    if store.showCustomNetwork {
                        Text(NetworkServiceOption.custom.title).tag(
                            NetworkServiceOption.custom.rawValue)
                    }
                }
                .disabled(store.state.isTitleRunning != 0)
                
                HStack {
                    TextField("New account name", text: $store.newAccountName)
                    Button("Create") {
                        store.createAccount()
                    }
                    .disabled(store.state.isTitleRunning != 0)
                    
                    Button("Delete") {
                        store.deleteSelectedAccount()
                    }
                    .disabled(store.state.isTitleRunning != 0 || store.accounts.count <= 1)
                }
                
                Button("Reload Accounts") {
                    store.reloadAccounts()
                }
            }
        }
        .formStyle(.grouped)
    }
}
