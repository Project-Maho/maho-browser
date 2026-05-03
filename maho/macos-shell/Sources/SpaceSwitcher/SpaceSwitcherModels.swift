import AppKit

struct SpaceSwitcherDisplayItem: Equatable {
    let id: String
    let space: SpaceViewModel?
    let name: String
    let color: SpaceColor
    let theme: SpaceTheme?
    let tabCount: Int
    let isActive: Bool
    let icon: String?
    let profileName: String?
    let profileId: ProfileId?
    let orderIndex: Int?

    var accessibilityLabel: String {
        var parts = ["Space: \(name)"]
        if isActive {
            parts.append("Active")
        }
        if let profileName, !profileName.isEmpty {
            parts.append("Profile \(profileName)")
        }
        if space != nil {
            parts.append("\(tabCount) tab\(tabCount == 1 ? "" : "s")")
        }
        return parts.joined(separator: ", ")
    }

    var toolTip: String {
        var parts = [name]
        if let profileName, !profileName.isEmpty {
            parts.append(profileName)
        }
        if space != nil {
            parts.append("\(tabCount) tab\(tabCount == 1 ? "" : "s")")
        }
        if isActive {
            parts.append("Active")
        }
        return parts.joined(separator: " • ")
    }
}

enum SpaceSwitcherDrag {
    static let pasteboardType = NSPasteboard.PasteboardType("com.maho.space-switcher.drag")
}
