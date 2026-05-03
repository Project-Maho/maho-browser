import Foundation

extension MahoBridge {

    func updateTabPreview(tabId: TabId, imageData: Data) {
        withCore { ptr in
            tabId.withCString { tabIdCStr in
                imageData.withUnsafeBytes { buffer in
                    guard let baseAddress = buffer.baseAddress else { return }
                    maho_core_update_tab_preview(
                        ptr,
                        tabIdCStr,
                        baseAddress.assumingMemoryBound(to: UInt8.self),
                        UInt(buffer.count)
                    )
                }
            }
        }
    }

    func getTabPreview(tabId: TabId) -> Data? {
        withCore { ptr -> Data? in
            tabId.withCString { tabIdCStr -> Data? in
                var length: UInt = 0
                guard let dataPtr = maho_core_get_tab_preview(ptr, tabIdCStr, &length) else {
                    return nil
                }
                let data = Data(bytes: dataPtr, count: Int(length))
                maho_core_free_preview_data(dataPtr, length)
                return data
            }
        } ?? nil
    }

    func schedulePreviewCapture(tabId: TabId) {
        withCore { ptr in
            tabId.withCString { tabIdCStr in
                _ = maho_core_schedule_preview_capture(ptr, tabIdCStr)
            }
        }
    }

    func takePendingPreviewCaptures() -> [TabId] {
        (withCore { ptr in
            FFIString.consumeJSON(maho_core_take_pending_preview_captures(ptr))
        } ?? nil) ?? []
    }
}
