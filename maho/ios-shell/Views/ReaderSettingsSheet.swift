import SwiftUI

struct ReaderSettingsSheet: View {
    @AppStorage(ReaderStyle.fontFamilyKey) private var fontFamily: String = ReaderStyle.defaultFontFamily
    @AppStorage(ReaderStyle.fontSizeKey) private var fontSize: Double = ReaderStyle.defaultFontSize
    @AppStorage(ReaderStyle.themeKey) private var readerTheme: ReaderTheme = .light
    
    @Environment(\.dismiss) private var dismiss
    
    private let bridge = MahoBridge.shared
    
    var body: some View {
        NavigationStack {
            Form {
                Section("Font Style") {
                    Picker("Font Family", selection: $fontFamily) {
                        ForEach(ReaderStyle.fontFamilies, id: \.self) { font in
                            Text(font).tag(font)
                        }
                    }
                    .pickerStyle(.menu)
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
                    Picker("Theme", selection: $readerTheme) {
                        Text("Light").tag(ReaderTheme.light)
                        Text("Sepia").tag(ReaderTheme.sepia)
                        Text("Dark").tag(ReaderTheme.dark)
                    }
                    .pickerStyle(.segmented)
                    .onChange(of: readerTheme) { _, newValue in
                        bridge.setReaderTheme(newValue)
                    }
                }
            }
            .navigationTitle("Reader Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                SwiftUI.ToolbarItem(placement: .topBarTrailing) {
                    Button("Done") {
                        dismiss()
                    }
                }
            }
        }
        .presentationDetents([.fraction(0.35)])
    }
}
