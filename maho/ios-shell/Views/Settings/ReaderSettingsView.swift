import SwiftUI

struct ReaderSettingsView: View {
    // Same keys the reader page and its in-reader settings sheet use, so a
    // change here is what the next reader page renders with.
    @AppStorage(ReaderStyle.fontFamilyKey) private var fontFamily: String = ReaderStyle.defaultFontFamily
    @AppStorage(ReaderStyle.fontSizeKey) private var fontSize: Double = ReaderStyle.defaultFontSize
    @AppStorage(ReaderStyle.themeKey) private var readerTheme: ReaderTheme = .light

    private let bridge = MahoBridge.shared

    var body: some View {
        Form {
            Section("Font") {
                Picker("Font Family", selection: $fontFamily) {
                    ForEach(ReaderStyle.fontFamilies, id: \.self) { font in
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
                    Slider(value: $fontSize, in: ReaderStyle.fontSizeRange, step: 1)
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
    }

    @ViewBuilder
    private var previewText: some View {
        Text("The quick brown fox jumps over the lazy dog. This is a preview of how reader mode content will appear with the selected font and size settings.")
            .font(ReaderStyle.previewFont(family: fontFamily, size: fontSize))
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
}
