import Foundation

struct LiveFolderItem: Equatable {
    let id: String
    let title: String
    let url: String
    let subtitle: String?
    let badgeText: String?
    let icon: String?
}

protocol LiveFolderProvider: AnyObject {
    var providerId: String { get }
    var displayName: String { get }
    var iconName: String { get }
    var refreshInterval: TimeInterval { get }
    func fetchItems(config: [String: String], completion: @escaping ([LiveFolderItem]) -> Void)
}
