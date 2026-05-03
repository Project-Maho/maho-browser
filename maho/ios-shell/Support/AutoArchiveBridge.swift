import Foundation

enum AutoArchiveBridge {
    static func send() {
        let payload = "{\"kind\":\"auto_archive\"}"
        _ = MahoBridge.shared.withCore { ptr in
            payload.withCString { cString in
                FFIString.consumeJSON(AutoArchiveFFI.maho_core_handle_event(ptr, cString)) as [CoreUpdate]?
            }
        }
    }

    private enum AutoArchiveFFI {
        @_silgen_name("maho_core_handle_event")
        static func maho_core_handle_event(_ ptr: OpaquePointer, _ eventJson: UnsafePointer<CChar>) -> UnsafeMutablePointer<CChar>?
    }
}
