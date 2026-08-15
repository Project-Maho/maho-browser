import SwiftUI

struct DownloadsView: View {
    @State private var downloads: [DownloadViewModel] = []
    @State private var isLoading = false
    @State private var pendingDeleteDownload: DownloadViewModel?
    @State private var showDeleteConfirmation = false

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
        }
    }

    // MARK: - Subviews

    private var downloadList: some View {
        List {
            ForEach(downloads) { download in
                DownloadRow(download: download)
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
                    .swipeActions(edge: .leading) {
                        if download.state == .downloading {
                            Button {
                                pauseDownload(download)
                            } label: {
                                Label { Text("Pause") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("pause")) }
                            }
                            .tint(.yellow)
                        } else if download.state == .paused {
                            Button {
                                resumeDownload(download)
                            } label: {
                                Label { Text("Resume") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("play")) }
                            }
                            .tint(.green)
                        }
                    }
            }
        }
        .listStyle(.plain)
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

    private func loadDownloads() {
        isLoading = true
        downloads = MahoBridge.shared.getDownloads()
        isLoading = false
    }

    private func pauseDownload(_ download: DownloadViewModel) {
        MahoBridge.shared.pauseDownload(id: download.id)
        refreshDownloads()
    }

    private func resumeDownload(_ download: DownloadViewModel) {
        MahoBridge.shared.resumeDownload(id: download.id)
        refreshDownloads()
    }

    private func cancelDownload(_ download: DownloadViewModel) {
        MahoBridge.shared.cancelDownload(id: download.id)
        refreshDownloads()
    }

    private func removeDownload(_ download: DownloadViewModel) {
        MahoBridge.shared.removeDownload(id: download.id)
        downloads.removeAll { $0.id == download.id }
    }

    private func refreshDownloads() {
        downloads = MahoBridge.shared.getDownloads()
    }

    // MARK: - CoreUpdate Handling

    func handleDownloadStarted(_ download: DownloadViewModel) {
        if !downloads.contains(where: { $0.id == download.id }) {
            downloads.append(download)
        }
    }

    func handleDownloadProgress(downloadId: DownloadId, progress: Double) {
        refreshDownloads()
    }

    func handleDownloadCompleted(downloadId: DownloadId) {
        refreshDownloads()
    }
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
