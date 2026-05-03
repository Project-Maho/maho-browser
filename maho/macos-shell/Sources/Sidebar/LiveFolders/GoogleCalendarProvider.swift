import Foundation

final class GoogleCalendarProvider: LiveFolderProvider {
    let providerId = "google_calendar"
    let displayName = "Google Calendar"
    let iconName = "calendar"
    let refreshInterval: TimeInterval = 120

    func fetchItems(config: [String: String], completion: @escaping ([LiveFolderItem]) -> Void) {
        let items = [
            LiveFolderItem(
                id: "gcal_open",
                title: "Open Google Calendar",
                url: "https://calendar.google.com",
                subtitle: "View your events",
                badgeText: nil,
                icon: "calendar"
            )
        ]
        completion(items)
    }
}
