import SwiftUI

struct LanguageSettingsView: View {
    private var currentLanguageName: String {
        Locale.current.localizedString(forIdentifier: Locale.current.identifier) ?? Locale.current.identifier
    }

    var body: some View {
        List {
            Section {
                Text("Maho follows your iPhone system language. To change the app language, update your preferred language in iOS Settings.")
                    .foregroundStyle(.secondary)

                HStack {
                    Text("Current Language")
                    Spacer()
                    Text(currentLanguageName)
                        .foregroundStyle(.secondary)
                }
            } header: {
                Text("System Language")
            }
        }
        .navigationTitle("Language")
    }
}
