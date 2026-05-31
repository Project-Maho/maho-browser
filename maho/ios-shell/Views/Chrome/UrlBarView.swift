import SwiftUI
import UIKit
import LucideIcons

struct UrlBarView: View {
    @ObservedObject var viewModel: BrowserViewModel
    @State private var editingText: String = ""
    @FocusState private var isFocused: Bool

    var body: some View {
        HStack(spacing: 8) {
            secureIndicator

            urlField

            trailingButtons
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 8)
        .background(
            RoundedRectangle(cornerRadius: 10)
                .fill(Color(.secondarySystemBackground))
        )
    }

    @ViewBuilder
    private var secureIndicator: some View {
        if !viewModel.isUrlBarEditing {
            Image(lucide: viewModel.isSecure ? Lucide.lock : Lucide.lockOpen)
                .font(.caption)
                .foregroundStyle(viewModel.isSecure ? .green : .secondary)
        }
    }

    private var urlField: some View {
        TextField(displayText, text: $editingText)
            .textFieldStyle(.plain)
            .font(.system(.subheadline, design: .monospaced))
            .autocorrectionDisabled()
            .textInputAutocapitalization(.never)
            .keyboardType(.URL)
            .submitLabel(.go)
            .focused($isFocused)
            .onSubmit {
                commitUrlEntry()
            }
            .onChange(of: isFocused) { _, focused in
                viewModel.isUrlBarEditing = focused
                if focused {
                    editingText = viewModel.currentUrl
                }
            }
    }

    private var displayText: String {
        if viewModel.currentTitle.isEmpty {
            return viewModel.currentUrl.isEmpty ? "Search or enter URL" : viewModel.currentUrl
        }
        return viewModel.currentTitle
    }

    private var trailingButtons: some View {
        HStack(spacing: 12) {
            if viewModel.isUrlBarEditing {
                Button {
                    isFocused = false
                    editingText = ""
                } label: {
                    Image(lucide: Lucide.circleX)
                        .foregroundStyle(.secondary)
                }
            } else {
                shareButton
            }
        }
    }

    private var shareButton: some View {
        Button {
            shareCurrentUrl()
        } label: {
            Image(lucide: Lucide.share)
                .font(.subheadline)
                .foregroundStyle(.primary)
        }
        .disabled(viewModel.currentUrl.isEmpty)
    }

    private func commitUrlEntry() {
        let text = editingText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }

        let urlString: String
        if text.contains(".") && !text.contains(" ") {
            urlString = text.hasPrefix("http://") || text.hasPrefix("https://") ? text : "https://\(text)"
        } else {
            let encoded = text.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? text
            urlString = "https://www.google.com/search?q=\(encoded)"
        }

        viewModel.navigate(to: urlString)
        isFocused = false
    }

    private func shareCurrentUrl() {
        guard let url = Foundation.URL(string: viewModel.currentUrl) else { return }
        let activityVC = UIActivityViewController(activityItems: [url], applicationActivities: nil)
        guard let scene = UIApplication.shared.connectedScenes.first as? UIWindowScene,
              let rootVC = scene.windows.first?.rootViewController else {
            return
        }
        var presenter = rootVC
        while let presented = presenter.presentedViewController {
            presenter = presented
        }
        presenter.present(activityVC, animated: true)
    }
}
