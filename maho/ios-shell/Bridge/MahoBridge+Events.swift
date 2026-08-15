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

    /// Dispatches a batch of events sequentially and aggregates all CoreUpdates.
    func handleEvents(_ events: [ShellEvent]) -> [CoreUpdate] {
        guard !events.isEmpty else { return [] }
        var allUpdates: [CoreUpdate] = []
        for event in events {
            allUpdates.append(contentsOf: handleEvent(event))
        }
        return allUpdates
    }

    /// Dispatches a batch of events.
    func sendEvents(_ events: [ShellEvent]) {
        _ = handleEvents(events)
    }
}

/// Thread-safe throttler / coalescer for high-frequency native-to-web / web-to-native events.
final class EventThrottler: @unchecked Sendable {
    private let interval: TimeInterval
    private let queue: DispatchQueue
    private let lock = NSLock()
    private var pendingWorkItem: DispatchWorkItem?
    private var lastRunTime: Date = .distantPast

    init(interval: TimeInterval = 0.016, queue: DispatchQueue = .main) {
        self.interval = interval
        self.queue = queue
    }

    func throttle(action: @escaping () -> Void) {
        lock.lock()
        defer { lock.unlock() }

        pendingWorkItem?.cancel()
        let now = Date()
        let timeSinceLast = now.timeIntervalSince(lastRunTime)

        if timeSinceLast >= interval {
            lastRunTime = now
            let workItem = DispatchWorkItem(block: action)
            pendingWorkItem = workItem
            queue.async(execute: workItem)
        } else {
            let delay = interval - timeSinceLast
            let workItem = DispatchWorkItem { [weak self] in
                self?.lock.lock()
                self?.lastRunTime = Date()
                self?.lock.unlock()
                action()
            }
            pendingWorkItem = workItem
            queue.asyncAfter(deadline: .now() + delay, execute: workItem)
        }
    }

    func cancel() {
        lock.lock()
        defer { lock.unlock() }
        pendingWorkItem?.cancel()
        pendingWorkItem = nil
    }
}
