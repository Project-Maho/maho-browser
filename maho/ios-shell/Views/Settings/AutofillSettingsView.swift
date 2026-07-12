import SwiftUI
import LucideIcons

struct AutofillSettingsView: View {
    @State private var addressesEnabled: Bool = true
    @State private var paymentsEnabled: Bool = true
    @State private var passwordSearchQuery: String = ""

    private let bridge = MahoBridge.shared

    var body: some View {
        Form {
            Section("Autofill") {
                Toggle("Addresses", isOn: $addressesEnabled)
                    .onChange(of: addressesEnabled) { _, newValue in
                        bridge.updateAutofillSettings(
                            AutofillSettingsUpdate(addressesEnabled: newValue)
                        )
                    }

                Toggle("Payment Methods", isOn: $paymentsEnabled)
                    .onChange(of: paymentsEnabled) { _, newValue in
                        bridge.updateAutofillSettings(
                            AutofillSettingsUpdate(paymentsEnabled: newValue)
                        )
                    }
            }

            Section("Saved Passwords") {
                HStack {
                    Image(lucide: Lucide.search)
                        .foregroundStyle(.secondary)
                    TextField("Search passwords", text: $passwordSearchQuery)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .onSubmit {
                            bridge.sendEvent(.searchPasswords(query: passwordSearchQuery))
                        }
                }

                Text("Passwords are securely stored and encrypted.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .navigationTitle("Autofill & Passwords")
        .onAppear(perform: loadSettings)
    }

    private func loadSettings() {
        guard let vm = bridge.getSettings() else { return }
        for section in vm.sections {
            for item in section.items {
                switch item.key {
                case "autofill.addressesEnabled": addressesEnabled = (item.value.value as? Bool) ?? true
                case "autofill.paymentsEnabled": paymentsEnabled = (item.value.value as? Bool) ?? true
                default: break
                }
            }
        }
    }
}
