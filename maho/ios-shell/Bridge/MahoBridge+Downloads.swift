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

    func startDownload(filename: String, url: String, totalBytes: Int64 = 0, filePath: String? = nil, mimeType: String? = nil) -> String? {
        withCore { ptr in
            var dict: [String: Any] = [
                "filename": filename,
                "url": url,
                "total_bytes": totalBytes,
            ]
            if let filePath = filePath { dict["file_path"] = filePath }
            if let mimeType = mimeType { dict["mime_type"] = mimeType }
            guard let data = try? JSONSerialization.data(withJSONObject: dict),
                  let jsonStr = String(data: data, encoding: .utf8) else { return nil }
            return FFIString.consume(maho_core_start_download(ptr, jsonStr))
        } ?? nil
    }

    func completeDownload(id: String) {
        _ = withCore { ptr in
            FFIString.consume(maho_core_complete_download(ptr, id))
        }
    }

    func updateDownloadProgress(id: String, receivedBytes: UInt64) {
        withCore { ptr in
            maho_core_update_download_progress(ptr, id, receivedBytes)
        }
    }
}
