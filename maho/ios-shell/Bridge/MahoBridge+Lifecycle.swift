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
        lock.lock()
        let target = pendingCorePtr ?? corePtr
        guard let ptr = target else {
            lock.unlock()
            return false
        }
#if DEBUG
        // Observation fires with the CURRENT publication state: a deferred
        // (private) core reports unpublished here by design.
        memoryThreadHydrationEntered?(memoryThreadCorePublished)
#endif
        let result = maho_core_load_state(ptr)
        // U06i: publish the privately-owned core only after successful load.
        if result == 1, let pending = pendingCorePtr, corePtr == nil {
            corePtr = pending
            pendingCorePtr = nil
        }
        lock.unlock()
        return result == 1
    }

    func tickDrain() -> [CoreUpdate] {
        (withCore { ptr in
            FFIString.consumeJSON(maho_core_tick(ptr))
        } ?? nil) ?? []
    }
}
