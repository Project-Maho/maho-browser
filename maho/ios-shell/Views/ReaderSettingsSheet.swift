import SwiftUI

struct ReaderSettingsSheet: View {
    @AppStorage("readerFontFamily") private var fontFamily: String = "System"
    @AppStorage("readerFontSize") private var fontSize: Double = 18.0
    @AppStorage("readerTheme") private var readerTheme: ReaderTheme = .light
    
    @Environment(\.dismiss) private var dismiss
    
    let availableFonts = ["System", "Georgia", "Courier New"]
    
    var body: some View {
        NavigationStack {
            Form {
                Section("Font Style") {
                    Picker("Font Family", selection: $fontFamily) {
                        ForEach(availableFonts, id: \.self) { font in
                            Text(font).tag(font)
                        }
                    }
                    .pickerStyle(.menu)
                    
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Text("Font Size")
                            Spacer()
                            Text("\(Int(fontSize))pt")
                                .foregroundStyle(.secondary)
                        }
                        Slider(value: $fontSize, in: 12...36, step: 1)
                    }
                }
                
                Section("Theme") {
                    Picker("Theme", selection: $readerTheme) {
                        Text("Light").tag(ReaderTheme.light)
                        Text("Sepia").tag(ReaderTheme.sepia)
                        Text("Dark").tag(ReaderTheme.dark)
                    }
                    .pickerStyle(.segmented)
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
