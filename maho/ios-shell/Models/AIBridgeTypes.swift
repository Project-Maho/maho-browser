import Foundation

// MARK: - AI Bridge Types (skeleton mode)

/// Space-level AI configuration for BYOK provider.
public struct SpaceAIConfig: Codable, Equatable {
    public let systemPrompt: String?
    public let tone: String?
    public let focusAreas: [String]
    public let preferredModel: String?
    public let memoryEnabled: Bool

    public init(
        systemPrompt: String? = nil,
        tone: String? = nil,
        focusAreas: [String] = [],
        preferredModel: String? = nil,
        memoryEnabled: Bool = true
    ) {
        self.systemPrompt = systemPrompt
        self.tone = tone
        self.focusAreas = focusAreas
        self.preferredModel = preferredModel
        self.memoryEnabled = memoryEnabled
    }
}

struct OfflineModelInfo: Codable {
    let status: String
    let size: Int
}
