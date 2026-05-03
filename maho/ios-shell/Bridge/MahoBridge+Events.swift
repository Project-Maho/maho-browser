import Foundation

extension MahoBridge {
    func handleEvent(_ event: ShellEvent) -> [CoreUpdate] {
        guard let jsonString = try? event.mahoJSONString() else {
            return []
        }

        return (withCore { ptr in
            jsonString.withCString { cStr in
                FFIString.consumeJSON(maho_core_handle_event(ptr, cStr))
            }
        } ?? nil) ?? []
    }

    func sendEvent(_ event: ShellEvent) {
        _ = handleEvent(event)
    }
}
