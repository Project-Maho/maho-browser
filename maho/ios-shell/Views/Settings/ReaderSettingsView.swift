import SwiftUI

struct ReaderSettingsView: View {
    @State private var fontFamily: String = "System"
    @State private var fontSize: Double = 18.0
    @State private var readerTheme: ReaderTheme = .light

    private let bridge = MahoBridge.shared

    private let availableFonts = [
        "System", "Georgia", "Palatino", "Times New Roman",
        "Helvetica", "Arial", "Verdana"
    ]

    var body: some View {
        Form {
            Section("Font") {
                Picker("Font Family", selection: $fontFamily) {
                    ForEach(availableFonts, id: \.self) { font in
                        Text(font).tag(font)
                    }
                }
                .onChange(of: fontFamily) { _, newValue in
                    bridge.setReaderFont(newValue)
                }

                VStack(alignment: .leading, spacing: 8) {
                    HStack {
                        Text("Font Size")
                        Spacer()
                        Text("\(Int(fontSize))pt")
                            .foregroundStyle(.secondary)
                    }
                    Slider(value: $fontSize, in: 12...32, step: 1)
                        .onChange(of: fontSize) { _, newValue in
                            bridge.setReaderFontSize(newValue)
                        }
                }
            }

            Section("Theme") {
                Picker("Reader Theme", selection: $readerTheme) {
                    Text("Light").tag(ReaderTheme.light)
                    Text("Sepia").tag(ReaderTheme.sepia)
                    Text("Dark").tag(ReaderTheme.dark)
                }
                .pickerStyle(.segmented)
                .onChange(of: readerTheme) { _, newValue in
                    bridge.setReaderTheme(newValue)
                }
            }

            Section("Preview") {
                previewText
            }
        }
        .navigationTitle("Reader Mode")
        .onAppear(perform: loadSettings)
    }

    @ViewBuilder
    private var previewText: some View {
        Text("The quick brown fox jumps over the lazy dog. This is a preview of how reader mode content will appear with the selected font and size settings.")
            .font(.system(size: CGFloat(fontSize)))
            .padding()
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(readerBackground)
            .foregroundStyle(readerForeground)
            .clipShape(RoundedRectangle(cornerRadius: 8))
    }

    private var readerBackground: Color {
        ShellTheme.ReaderPalette.background(for: readerTheme)
    }

    private var readerForeground: Color {
        ShellTheme.ReaderPalette.foreground(for: readerTheme)
    }

    private func loadSettings() {
        guard let vm = bridge.getSettings() else { return }
        for section in vm.sections {
            for item in section.items {
                switch item.key {
                case "reader.fontFamily": fontFamily = (item.value.value as? String) ?? "System"
                case "reader.fontSize": fontSize = (item.value.value as? Double) ?? 18.0
                case "reader.theme":
                    if let raw = item.value.value as? String, let t = ReaderTheme(rawValue: raw) {
                        readerTheme = t
                    }
                default: break
                }
            }
        }
    }
}
