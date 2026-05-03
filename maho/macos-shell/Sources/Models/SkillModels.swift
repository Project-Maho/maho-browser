import Foundation

struct SkillInfo: Codable, Equatable {
    let id: String
    let name: String
    let slashCommand: String
    let description: String
    let icon: String

    private enum CodingKeys: String, CodingKey {
        case id, name, slashCommand, description, icon
    }
}

final class SkillsStore {
    static let shared = SkillsStore()

    private(set) var skills: [SkillInfo] = SkillsStore.defaultSkills()

    func updateSkills(_ skills: [SkillInfo]) {
        self.skills = skills
    }

    func filter(query: String) -> [SkillInfo] {
        let q = query.lowercased()
        if q.isEmpty || q == "/" { return skills }
        let searchTerm = q.hasPrefix("/") ? String(q.dropFirst()) : q
        return skills.filter {
            $0.name.lowercased().contains(searchTerm) ||
            $0.slashCommand.lowercased().contains(searchTerm)
        }
    }

    private static func defaultSkills() -> [SkillInfo] {
        [
            SkillInfo(id: "summarize", name: "Summarize", slashCommand: "/summarize",
                      description: "Summarize the current page", icon: "doc.text"),
            SkillInfo(id: "translate", name: "Translate", slashCommand: "/translate",
                      description: "Translate text to another language", icon: "globe"),
            SkillInfo(id: "explain", name: "Explain", slashCommand: "/explain",
                      description: "Explain this page in simple terms", icon: "lightbulb"),
            SkillInfo(id: "rewrite", name: "Rewrite", slashCommand: "/rewrite",
                      description: "Rewrite text with improved clarity", icon: "pencil.line"),
            SkillInfo(id: "fact-check", name: "Fact Check", slashCommand: "/fact-check",
                      description: "Analyze claims for accuracy", icon: "checkmark.shield"),
            SkillInfo(id: "code-review", name: "Code Review", slashCommand: "/code-review",
                      description: "Review code for bugs and improvements", icon: "chevron.left.forwardslash.chevron.right"),
        ]
    }
}
