import Foundation

extension MahoBridge {
    func getDownloads() -> [DownloadViewModel] {
        (withCore { ptr in
            FFIString.consumeJSON(maho_core_get_download_view_models(ptr))
        } ?? nil) ?? []
    }

    func pauseDownload(id: String) {
        sendEvent(.pauseDownload(downloadId: id))
    }

    func resumeDownload(id: String) {
        sendEvent(.resumeDownload(downloadId: id))
    }

    func cancelDownload(id: String) {
        sendEvent(.cancelDownload(downloadId: id))
    }

    func removeDownload(id: String) {
        sendEvent(.removeDownload(downloadId: id))
    }
}
