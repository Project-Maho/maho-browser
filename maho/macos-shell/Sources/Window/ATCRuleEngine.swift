import Foundation

struct ATCLinkRule: Codable {
    let id: String
    var domainPattern: String
    var targetSpaceId: String?
    var targetProfileId: String?
    var openInNewTab: Bool
    var enabled: Bool

    init(domainPattern: String, targetSpaceId: String? = nil, targetProfileId: String? = nil, openInNewTab: Bool = true, enabled: Bool = true) {
        self.id = UUID().uuidString
        self.domainPattern = domainPattern
        self.targetSpaceId = targetSpaceId
        self.targetProfileId = targetProfileId
        self.openInNewTab = openInNewTab
        self.enabled = enabled
    }
}

final class ATCRuleEngine {
    static let shared = ATCRuleEngine()
    private(set) var rules: [ATCLinkRule] = []
    private let storageKey = "maho.atc.link.rules"

    private init() { loadRules() }

    func matchingRule(for url: URL) -> ATCLinkRule? {
        guard let host = url.host?.lowercased() else { return nil }
        return rules.first(where: { rule in
            guard rule.enabled else { return false }
            return matchDomain(pattern: rule.domainPattern.lowercased(), host: host)
        })
    }

    private func matchDomain(pattern: String, host: String) -> Bool {
        if pattern.hasPrefix("*.") {
            let suffix = String(pattern.dropFirst(2))
            return host == suffix || host.hasSuffix("." + suffix)
        }
        return host == pattern
    }

    func addRule(_ rule: ATCLinkRule) { rules.append(rule); saveRules() }
    func removeRule(at index: Int) { guard index >= 0, index < rules.count else { return }; rules.remove(at: index); saveRules() }
    func updateRule(at index: Int, with rule: ATCLinkRule) { guard index >= 0, index < rules.count else { return }; rules[index] = rule; saveRules() }

    private func loadRules() {
        guard let data = UserDefaults.standard.data(forKey: storageKey),
              let decoded = try? JSONDecoder().decode([ATCLinkRule].self, from: data) else { return }
        rules = decoded
    }

    private func saveRules() {
        if let data = try? JSONEncoder().encode(rules) {
            UserDefaults.standard.set(data, forKey: storageKey)
        }
    }
}
