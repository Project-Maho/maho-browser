import Foundation

protocol CoreUpdateObserver: AnyObject {
    func handleCoreUpdate(_ update: CoreUpdate)
}

final class CoreUpdateDispatcher {
    static let shared = CoreUpdateDispatcher()

    private var observers: [ObjectIdentifier: WeakObserver] = [:]

    private struct WeakObserver {
        weak var observer: CoreUpdateObserver?
    }

    private init() {}

    func register(_ observer: CoreUpdateObserver) {
        let id = ObjectIdentifier(observer)
        observers[id] = WeakObserver(observer: observer)
    }

    func unregister(_ observer: CoreUpdateObserver) {
        let id = ObjectIdentifier(observer)
        observers.removeValue(forKey: id)
    }

    func dispatch(_ updates: [CoreUpdate]) {
        observers = observers.filter { $0.value.observer != nil }
        for (_, weak) in observers {
            for update in updates {
                weak.observer?.handleCoreUpdate(update)
            }
        }
    }

    func sendAndDispatch(_ event: ShellEvent) {
        let updates = MahoBridge.shared.sendEvent(event)
        dispatch(updates)
    }
}
