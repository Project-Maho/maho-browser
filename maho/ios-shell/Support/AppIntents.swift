import AppIntents

@available(iOS 16.0, *)
struct OpenMahoIntent: AppIntent {
    static var title: LocalizedStringResource = "Open Maho"
    static var description = IntentDescription("Opens Maho and focuses the search bar.")

    static var openAppWhenRun: Bool = true

    @MainActor
    func perform() async throws -> some IntentResult {
        DeepLinkHandler.shared.onNewTab?()
        return .result()
    }
}
