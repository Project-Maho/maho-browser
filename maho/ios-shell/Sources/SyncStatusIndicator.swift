import SwiftUI
import LucideIcons

struct SyncStatusIndicator: View {
    let statusJson: String

    private var isSynced: Bool {
        guard let data = statusJson.data(using: .utf8),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return false
        }
        if let synced = json["synced"] as? Bool {
            return synced
        }
        return false
    }

    var body: some View {
        Image(lucide: isSynced ? Lucide.circleCheck : Lucide.refreshCw)
            .foregroundColor(isSynced ? .green : .orange)
            .accessibilityLabel(isSynced ? "Relay synced" : "Relay sync in progress")
    }
}
