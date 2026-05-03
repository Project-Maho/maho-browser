import Foundation
#if os(iOS)
import UIKit
#endif

final class MahoBridge {
    private var corePtr: UnsafeMutableRawPointer?

    init() {
        corePtr = maho_core_new()
    }

    deinit {
        if let ptr = corePtr {
            maho_core_free(ptr)
        }
    }

    func getStateJson() -> String? {
        guard let ptr = corePtr else { return nil }
        guard let cString = maho_core_get_tab_view_models(ptr) else { return nil }
        let string = String(cString: cString)
        maho_string_free(cString)
        return string
    }

    func tick() -> String? {
        guard let ptr = corePtr else { return nil }
        guard let cString = maho_core_tick(ptr) else { return nil }
        let string = String(cString: cString)
        maho_string_free(cString)
        return string
    }

    func getArchivedTabs(spaceId: String) -> String? {
        guard let ptr = corePtr else { return nil }
        guard let jsonData = try? JSONSerialization.data(withJSONObject: spaceId),
              let jsonString = String(data: jsonData, encoding: .utf8) else { return nil }
        guard let cString = jsonString.withCString({ cStr in
            maho_core_get_archived_tabs(ptr, cStr)
        }) else { return nil }
        let string = String(cString: cString)
        maho_string_free(cString)
        return string
    }

    func sendEvent(name: String, payload: [String: Any] = [:]) {
        guard let ptr = corePtr else { return }
        var dict = payload
        dict["type"] = name
        guard let jsonData = try? JSONSerialization.data(withJSONObject: dict),
              let jsonString = String(data: jsonData, encoding: .utf8) else { return }
        jsonString.withCString { cStr in
            if let result = maho_core_handle_event(ptr, cStr) {
                maho_string_free(result)
            }
        }
    }
}

// C FFI declarations expected from the Rust static library
@_silgen_name("maho_core_new")
private func maho_core_new() -> UnsafeMutableRawPointer?

@_silgen_name("maho_core_free")
private func maho_core_free(_ ptr: UnsafeMutableRawPointer)

@_silgen_name("maho_core_handle_event")
private func maho_core_handle_event(_ ptr: UnsafeMutableRawPointer, _ eventJson: UnsafePointer<CChar>) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_core_get_tab_view_models")
private func maho_core_get_tab_view_models(_ ptr: UnsafeMutableRawPointer) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_core_tick")
private func maho_core_tick(_ ptr: UnsafeMutableRawPointer) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_core_get_archived_tabs")
private func maho_core_get_archived_tabs(_ ptr: UnsafeMutableRawPointer, _ spaceIdJson: UnsafePointer<CChar>) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_string_free")
private func maho_string_free(_ s: UnsafeMutablePointer<CChar>)
