import SwiftUI

struct NotificationSettingsView: View {
    @State private var enabled: Bool = true
    @State private var calendarNotifications: Bool = true
    @State private var updateNotifications: Bool = true
    @State private var soundEnabled: Bool = true
    @State private var showDismissAllConfirmation = false

    private let bridge = MahoBridge.shared

    var body: some View {
        Form {
            Section("Notifications") {
                Toggle("Enable Notifications", isOn: $enabled)
                    .onChange(of: enabled) { _, newValue in
                        bridge.updateNotificationSettings(
                            NotificationSettingsUpdate(enabled: newValue)
                        )
                    }
            }

            if enabled {
                Section("Categories") {
                    Toggle("Calendar Reminders", isOn: $calendarNotifications)
                        .onChange(of: calendarNotifications) { _, newValue in
                            bridge.updateNotificationSettings(
                                NotificationSettingsUpdate(calendarNotifications: newValue)
                            )
                        }

                    Toggle("Update Notifications", isOn: $updateNotifications)
                        .onChange(of: updateNotifications) { _, newValue in
                            bridge.updateNotificationSettings(
                                NotificationSettingsUpdate(updateNotifications: newValue)
                            )
                        }
                }

                Section("Sound") {
                    Toggle("Notification Sound", isOn: $soundEnabled)
                        .onChange(of: soundEnabled) { _, newValue in
                            bridge.updateNotificationSettings(
                                NotificationSettingsUpdate(soundEnabled: newValue)
                            )
                        }
                }
            }

            Section {
                Button("Dismiss All Notifications", role: .destructive) {
                    showDismissAllConfirmation = true
                }
                .confirmationDialog(
                    "Dismiss All Notifications?",
                    isPresented: $showDismissAllConfirmation,
                    titleVisibility: .visible
                ) {
                    Button("Dismiss All", role: .destructive) {
                        bridge.sendEvent(.dismissAllNotifications)
                    }
                    Button("Cancel", role: .cancel) {}
                }
            }
        }
        .navigationTitle("Notifications")
        .onAppear(perform: loadSettings)
    }

    private func loadSettings() {
        guard let vm = bridge.getSettings() else { return }
        for section in vm.sections {
            for item in section.items {
                switch item.key {
                case "notifications.enabled": enabled = (item.value.value as? Bool) ?? true
                case "notifications.calendarNotifications": calendarNotifications = (item.value.value as? Bool) ?? true
                case "notifications.updateNotifications": updateNotifications = (item.value.value as? Bool) ?? true
                case "notifications.soundEnabled": soundEnabled = (item.value.value as? Bool) ?? true
                default: break
                }
            }
        }
    }
}
