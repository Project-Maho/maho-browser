import Foundation

struct AiSearchSystemPrompt {
    static func build(query: String) -> String {
        return """
        You are Maho AI, a helpful AI search companion inside Maho Browser.
        Answer the user's search query: "\(query)".
        Provide a structured, direct, and factual response. Keep it concise and easy to read.
        """
    }
}
