import Foundation
import SwiftUI
import Speech

struct LanguageSettingsView: View {
    @AppStorage("voice.sttLocale") private var voiceSTTLocale = ""

    private static let voiceSTTLocaleDefaultsKey = "voice.sttLocale"
    private static let voiceSTTLocaleDidChangeNotification = Notification.Name("dev.maho.browser.voice.sttLocaleDidChange")

    private var currentLanguageName: String {
        Locale.current.localizedString(forIdentifier: Locale.current.identifier) ?? Locale.current.identifier
    }

    private var voiceLocaleOptions: [Locale] {
        let storedIdentifier = voiceSTTLocale.trimmingCharacters(in: .whitespacesAndNewlines)
        var locales = Array(SFSpeechRecognizer.supportedLocales())

        if !storedIdentifier.isEmpty,
           !locales.contains(where: { $0.identifier == storedIdentifier }) {
            locales.append(Locale(identifier: storedIdentifier))
        }

        return locales.sorted { lhs, rhs in
            let lhsName = Self.localizedName(for: lhs)
            let rhsName = Self.localizedName(for: rhs)
            let comparison = lhsName.localizedStandardCompare(rhsName)

            if comparison == .orderedSame {
                return lhs.identifier < rhs.identifier
            }

            return comparison == .orderedAscending
        }
    }

    private var selectedVoiceLanguageName: String {
        let storedIdentifier = voiceSTTLocale.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !storedIdentifier.isEmpty else {
            return "System default"
        }

        return Self.localizedName(forIdentifier: storedIdentifier)
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

            Section {
                Picker("Recognition Language", selection: $voiceSTTLocale) {
                    Text("System default").tag("")
                        .accessibilityAddTraits(voiceSTTLocale.isEmpty ? .isSelected : [])

                    ForEach(voiceLocaleOptions, id: \.identifier) { locale in
                        Text(Self.localizedName(for: locale)).tag(locale.identifier)
                            .accessibilityAddTraits(locale.identifier == voiceSTTLocale ? .isSelected : [])
                    }
                }

                HStack {
                    Text("Current Voice Language")
                    Spacer()
                    Text(selectedVoiceLanguageName)
                        .foregroundStyle(.secondary)
                }
            } header: {
                Text("Voice Recognition Language")
            } footer: {
                Text("Voice search and AI chat dictation use this language. The app interface still follows your iPhone system language.")
            }
        }
        .navigationTitle("Language")
        .onChange(of: voiceSTTLocale) { _, newValue in
            NotificationCenter.default.post(
                name: Self.voiceSTTLocaleDidChangeNotification,
                object: nil,
                userInfo: [Self.voiceSTTLocaleDefaultsKey: newValue]
            )
        }
    }

    private static func localizedName(for locale: Locale) -> String {
        localizedName(forIdentifier: locale.identifier)
    }

    private static func localizedName(forIdentifier identifier: String) -> String {
        Locale.current.localizedString(forIdentifier: identifier)
            ?? Locale(identifier: identifier).localizedString(forIdentifier: identifier)
            ?? identifier
    }
}
