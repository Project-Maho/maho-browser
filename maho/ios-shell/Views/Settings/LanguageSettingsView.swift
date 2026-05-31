import SwiftUI
import LucideIcons

struct LanguageSettingsView: View {
    @AppStorage("settings.preferredLanguage") private var preferredLanguage: String = ""

    private var availableLanguages: [Locale] {
        Locale.availableIdentifiers
            .compactMap { Locale(identifier: $0) }
            .sorted { lhs, rhs in
                lhs.localizedString(forLanguageCode: lhs.language.languageCode?.identifier ?? "")
                    ?? lhs.identifier
                    < lhs.localizedString(forLanguageCode: rhs.language.languageCode?.identifier ?? "")
                    ?? rhs.identifier
            }
    }

    var body: some View {
        List {
            Section {
                Button {
                    preferredLanguage = ""
                } label: {
                    HStack {
                        Text("System Default")
                        Spacer()
                        if preferredLanguage.isEmpty {
                            Image(lucide: Lucide.check)
                                .foregroundStyle(Color.accentColor)
                        }
                    }
                }
            }

            Section {
                ForEach(availableLanguages, id: \.identifier) { locale in
                    Button {
                        preferredLanguage = locale.identifier
                    } label: {
                        HStack {
                            Text(locale.localizedString(forIdentifier: locale.identifier) ?? locale.identifier)
                            Spacer()
                            if preferredLanguage == locale.identifier {
                                Image(lucide: Lucide.check)
                                    .foregroundStyle(Color.accentColor)
                            }
                        }
                    }
                }
            }
        }
        .navigationTitle("Language")
    }
}
