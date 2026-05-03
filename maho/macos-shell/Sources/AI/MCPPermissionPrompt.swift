import AppKit

final class MCPPermissionPrompt {
    static func requestPermission(
        toolName: String,
        params: [String: Any],
        completion: @escaping (Bool) -> Void
    ) {
        DispatchQueue.main.async {
            let alert = NSAlert()
            alert.messageText = "External AI Tool Request"
            alert.informativeText = "An external AI tool wants to: \(toolName)\n\nParameters: \(formatParams(params))\n\nAllow this action?"
            alert.alertStyle = .warning
            alert.addButton(withTitle: "Allow")
            alert.addButton(withTitle: "Deny")
            let response = alert.runModal()
            completion(response == .alertFirstButtonReturn)
        }
    }

    private static func formatParams(_ params: [String: Any]) -> String {
        return params.map { "\($0.key): \($0.value)" }.joined(separator: ", ")
    }
}
