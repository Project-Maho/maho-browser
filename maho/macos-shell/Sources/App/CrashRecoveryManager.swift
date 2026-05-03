import AppKit

final class CrashRecoveryManager {
    static let shared = CrashRecoveryManager()

    private let saveIntervalSeconds: UInt64 = 30
    private var saveTimer: Timer?

    private init() {}

    func startPeriodicSave() {
        MahoBridge.shared.setCrashSaveInterval(seconds: saveIntervalSeconds)
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            self.saveTimer = Timer.scheduledTimer(withTimeInterval: TimeInterval(self.saveIntervalSeconds), repeats: true) { _ in
                _ = MahoBridge.shared.saveState()
            }
        }
    }

    func stopAndSave() {
        saveTimer?.invalidate()
        saveTimer = nil
        _ = MahoBridge.shared.saveState()
    }
}
