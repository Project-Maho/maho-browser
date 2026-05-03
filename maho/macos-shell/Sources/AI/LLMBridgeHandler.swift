import Foundation

final class LLMBridgeHandler: CoreUpdateObserver {
    static let shared = LLMBridgeHandler()

    private init() {}

    func start() {
        CoreUpdateDispatcher.shared.register(self)
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        guard case .requestLlmCompletion(let requestId, _, let prompt, _) = update else { return }

        Task {
            do {
                let messages = [LLMMessage(role: .user, content: prompt)]
                let result = try await LLMService.shared.completeChat(messages: messages)
                let event = ShellEvent.llmResult(requestId: requestId, result: result)
                DispatchQueue.main.async {
                    CoreUpdateDispatcher.shared.sendAndDispatch(event)
                }
            } catch {
                let event = ShellEvent.llmError(requestId: requestId, error: error.localizedDescription)
                DispatchQueue.main.async {
                    CoreUpdateDispatcher.shared.sendAndDispatch(event)
                }
            }
        }
    }
}
