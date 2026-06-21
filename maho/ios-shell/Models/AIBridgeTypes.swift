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

/// Result of tab clustering AI operation.
public struct TabClusterResult: Codable {
    public let tab_ids: [String]
    public let group_name: String

    public init(tab_ids: [String] = [], group_name: String = "") {
        self.tab_ids = tab_ids
        self.group_name = group_name
    }
}

struct OfflineModelInfo: Codable {
    let status: String
    let size: Int
}
