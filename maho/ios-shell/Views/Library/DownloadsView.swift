import SwiftUI
import Combine
import QuickLook
import UIKit

struct DownloadsView: View {
    @State private var downloads: [DownloadViewModel] = []
    @State private var isLoading = false
    @State private var pendingDeleteDownload: DownloadViewModel?
    @State private var showDeleteConfirmation = false
    @State private var previewURL: URL?
    @State private var shareURL: URL?

    /// Progress KVO fires per received chunk; coalesce to a few list reloads a second.
    private let downloadChanges = NotificationCenter.default
        .publisher(for: WebDownloadCoordinator.downloadsDidChange)
        .throttle(for: .milliseconds(250), scheduler: RunLoop.main, latest: true)

    var body: some View {
        NavigationStack {
            Group {
                if isLoading {
                    ProgressView()
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else if downloads.isEmpty {
                    ContentUnavailableView {
                        Label { Text("No Downloads") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("arrow.down.circle")) }
                    } description: {
                        Text("Files you download will appear here.")
                    }
                } else {
                    downloadList
                }
            }
            .navigationTitle("Downloads")
            .onAppear(perform: loadDownloads)
            .onReceive(downloadChanges) { _ in
                refreshDownloads()
            }
        }
    }

    // MARK: - Subviews

    private var downloadList: some View {
        List {
            ForEach(downloads) { download in
                DownloadRow(download: download)
                    .contentShape(Rectangle())
                    .onTapGesture {
                        if let url = openableFileURL(for: download) {
                            previewURL = url
                        }
                    }
                    .accessibilityAddTraits(openableFileURL(for: download) != nil ? .isButton : [])
                    .swipeActions(edge: .leading, allowsFullSwipe: true) {
                        if let url = openableFileURL(for: download) {
                            Button {
                                shareURL = url
                            } label: {
                                Label { Text("Share") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("square.and.arrow.up")) }
                            }
                            .tint(.blue)
                        }
                    }
                    .swipeActions(edge: .trailing, allowsFullSwipe: true) {
                        Button(role: .destructive) {
                            pendingDeleteDownload = download
                            showDeleteConfirmation = true
                        } label: {
                            Label { Text("Delete") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("trash")) }
                        }
                        if download.state == .downloading {
                            Button {
                                cancelDownload(download)
                            } label: {
                                Label { Text("Cancel") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("xmark.circle")) }
                            }
                            .tint(.orange)
                        }
                    }
            }
        }
        .listStyle(.plain)
        .sheet(item: Binding(
            get: { previewURL.map(IdentifiedURL.init) },
            set: { previewURL = $0?.url }
        )) { item in
            DownloadPreview(url: item.url)
                .ignoresSafeArea()
        }
        .sheet(item: Binding(
            get: { shareURL.map(IdentifiedURL.init) },
            set: { shareURL = $0?.url }
        )) { item in
            DownloadShareSheet(url: item.url)
        }
        .confirmationDialog(
            "Delete Download?",
            isPresented: $showDeleteConfirmation,
            titleVisibility: .visible,
            presenting: pendingDeleteDownload
        ) { download in
            Button("Delete", role: .destructive) {
                removeDownload(download)
                pendingDeleteDownload = nil
            }
            Button("Cancel", role: .cancel) {
                pendingDeleteDownload = nil
            }
        } message: { download in
            Text("\"\(download.filename)\" will be removed from the downloads list.")
        }
    }

    // MARK: - Actions

    /// Completed downloads whose own file still exists can be opened or shared.
    /// Downloads now get unique paths, so a file is trusted as this row's. Only
    /// rows recorded before that, which share a path with another row, may point
    /// at a file a later download replaced; for those a size that disagrees with
    /// the row means the file is not this download's, so it is not offered.
    private func openableFileURL(for download: DownloadViewModel) -> URL? {
        guard download.state == .completed, let path = download.filePath, !path.isEmpty,
              let attributes = try? FileManager.default.attributesOfItem(atPath: path) else {
            return nil
        }
        // A row that is not completed still holds this path: it is downloading
        // into it right now, or it failed/was cancelled after leaving partial
        // data there. The file is then not this row's, and the size guard below
        // cannot tell, because it only compares against completed rows.
        let ownedByLiveRow = downloads.contains {
            $0.id != download.id && $0.state != .completed && $0.filePath == path
        }
        if ownedByLiveRow {
            return nil
        }
        // Rows recorded before unique paths share this path; when another one of
        // them completed, a size that disagrees with this row means the file
        // belongs to that download instead.
        let sharesPath = downloads.contains {
            $0.id != download.id && $0.state == .completed && $0.filePath == path
        }
        if sharesPath, download.totalBytes > 0,
           let size = (attributes[.size] as? NSNumber)?.uint64Value,
           size != download.totalBytes {
            return nil
        }
        return URL(fileURLWithPath: path)
    }

    private func loadDownloads() {
        isLoading = true
        downloads = MahoBridge.shared.getDownloads()
        isLoading = false
    }

    // WKDownload cannot pause, so active downloads only offer Cancel, which
    // stops the real transfer before recording the cancellation in core.
    private func cancelDownload(_ download: DownloadViewModel) {
        WebDownloadCoordinator.shared.cancel(downloadId: download.id)
        MahoBridge.shared.cancelDownload(id: download.id)
        refreshDownloads()
    }

    private func removeDownload(_ download: DownloadViewModel) {
        WebDownloadCoordinator.shared.cancel(downloadId: download.id)
        MahoBridge.shared.removeDownload(id: download.id)
        downloads.removeAll { $0.id == download.id }
    }

    private func refreshDownloads() {
        downloads = MahoBridge.shared.getDownloads()
    }
}

// MARK: - File access

private struct IdentifiedURL: Identifiable {
    let url: URL
    var id: String { url.path }
}

/// QuickLook preview of a downloaded file; its toolbar also offers Share.
private struct DownloadPreview: UIViewControllerRepresentable {
    let url: URL

    func makeCoordinator() -> Coordinator { Coordinator(url: url) }

    func makeUIViewController(context: Context) -> UINavigationController {
        let controller = QLPreviewController()
        controller.dataSource = context.coordinator
        controller.navigationItem.leftBarButtonItem = UIBarButtonItem(
            systemItem: .done,
            primaryAction: UIAction { [weak controller] _ in controller?.dismiss(animated: true) }
        )
        return UINavigationController(rootViewController: controller)
    }

    func updateUIViewController(_ controller: UINavigationController, context: Context) {}

    final class Coordinator: NSObject, QLPreviewControllerDataSource {
        let url: URL
        init(url: URL) { self.url = url }

        func numberOfPreviewItems(in controller: QLPreviewController) -> Int { 1 }

        func previewController(_ controller: QLPreviewController, previewItemAt index: Int) -> QLPreviewItem {
            url as NSURL
        }
    }
}

private struct DownloadShareSheet: UIViewControllerRepresentable {
    let url: URL

    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: [url], applicationActivities: nil)
    }

    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}

// MARK: - DownloadRow

private struct DownloadRow: View {
    let download: DownloadViewModel

    var body: some View {
        HStack(spacing: 12) {
            VStack(alignment: .leading, spacing: 4) {
                Text(download.filename)
                    .font(.body)
                    .lineLimit(1)

                HStack(spacing: 6) {
                    if download.state == .downloading {
                        ProgressView(value: progressValue, total: 1.0)
                            .progressViewStyle(.linear)
                            .frame(width: 80)
                    }

                    Text(sizeText)
                        .font(.caption)
                        .foregroundStyle(.secondary)

                    Text(stateText)
                        .font(.caption2)
                        .fontWeight(.semibold)
                        .padding(.horizontal, 6)
                        .padding(.vertical, 2)
                        .background(stateColor.opacity(0.15))
                        .foregroundStyle(stateColor)
                        .clipShape(Capsule())
                }
            }

            Spacer()
        }
        .padding(.vertical, 4)
    }

    private var progressValue: Double {
        guard download.totalBytes > 0 else { return 0 }
        return Double(download.receivedBytes) / Double(download.totalBytes)
    }

    private var sizeText: String {
        let received = byteString(download.receivedBytes)
        if download.state == .completed || download.totalBytes == 0 {
            return received
        }
        return "\(received) / \(byteString(download.totalBytes))"
    }

    private var stateText: String {
        switch download.state {
        case .downloading: return "Downloading"
        case .paused: return "Paused"
        case .completed: return "Completed"
        case .failed: return "Failed"
        case .cancelled: return "Cancelled"
        }
    }

    private var stateColor: Color {
        switch download.state {
        case .downloading: return .blue
        case .paused: return ShellTheme.Palette.warning
        case .completed: return ShellTheme.Palette.success
        case .failed: return ShellTheme.Palette.error
        case .cancelled: return .gray
        }
    }

    private func byteString(_ bytes: UInt64) -> String {
        let formatter = ByteCountFormatter()
        formatter.countStyle = .file
        return formatter.string(fromByteCount: Int64(bytes))
    }
}
