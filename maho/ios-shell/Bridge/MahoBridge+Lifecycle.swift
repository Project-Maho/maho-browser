import Foundation

extension MahoBridge {
    // FFI returns i32: 1 = success, 0 = error
    @discardableResult
    func saveState() -> Bool {
        guard let result = withCore({ maho_core_save_state($0) }) else {
            return false
        }
        return result == 1
    }

    // FFI returns i32: 1 = success, 0 = error
    @discardableResult
    func loadState() -> Bool {
        guard let result = withCore({ maho_core_load_state($0) }) else {
            return false
        }
        return result == 1
    }

    func tickDrain() -> [CoreUpdate] {
        (withCore { ptr in
            FFIString.consumeJSON(maho_core_tick(ptr))
        } ?? nil) ?? []
    }
}
