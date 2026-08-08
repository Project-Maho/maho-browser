import Foundation

extension ShellEvent {
    func encodeAccountLibrary(to container: inout KeyedEncodingContainer<DynamicCodingKey>) throws -> Bool {
        switch self {
        case .toggleExtension(let extensionId):
            try container.encode(extensionId, forKey: DynamicCodingKey(stringValue: "extension_id")!)
        case .removeExtension(let extensionId):
            try container.encode(extensionId, forKey: DynamicCodingKey(stringValue: "extension_id")!)
        case .signIn(let email, let displayName, let password, let accessToken, let userId, let deviceId):
            try container.encode(email, forKey: DynamicCodingKey(stringValue: "email")!)
            try container.encodeIfPresent(displayName, forKey: DynamicCodingKey(stringValue: "display_name")!)
            try container.encodeIfPresent(password, forKey: DynamicCodingKey(stringValue: "password")!)
            try container.encodeIfPresent(accessToken, forKey: DynamicCodingKey(stringValue: "access_token")!)
            try container.encodeIfPresent(userId, forKey: DynamicCodingKey(stringValue: "user_id")!)
            try container.encodeIfPresent(deviceId, forKey: DynamicCodingKey(stringValue: "device_id")!)
        case .signOut: break
        case .toggleSync: break
        case .createTrafficRule(let rule):
            try container.encode(rule, forKey: DynamicCodingKey(stringValue: "rule")!)
        case .deleteTrafficRule(let ruleId):
            try container.encode(ruleId, forKey: DynamicCodingKey(stringValue: "rule_id")!)
        case .updateTrafficRule(let rule):
            try container.encode(rule, forKey: DynamicCodingKey(stringValue: "rule")!)
        case .setDefaultLinkBehavior(let behavior):
            try container.encode(behavior, forKey: DynamicCodingKey(stringValue: "behavior")!)
        case .updateSpaceConfig(let changes):
            try container.encode(changes, forKey: DynamicCodingKey(stringValue: "changes")!)
        case .createProfile(let name):
            try container.encode(name, forKey: DynamicCodingKey(stringValue: "name")!)
        case .deleteProfile(let profileId):
            try container.encode(profileId, forKey: DynamicCodingKey(stringValue: "profile_id")!)
        case .updateProfile(let profileId, let name, let avatarColor, let downloadPath, let archiveTimeoutHours):
            try container.encode(profileId, forKey: DynamicCodingKey(stringValue: "profile_id")!)
            try container.encodeIfPresent(name, forKey: DynamicCodingKey(stringValue: "name")!)
            try container.encodeIfPresent(avatarColor, forKey: DynamicCodingKey(stringValue: "avatar_color")!)
            try container.encodeIfPresent(downloadPath, forKey: DynamicCodingKey(stringValue: "download_path")!)
            try container.encodePatchable(archiveTimeoutHours, forKey: DynamicCodingKey(stringValue: "archive_timeout_hours")!)
        case .switchProfile(let profileId):
            try container.encode(profileId, forKey: DynamicCodingKey(stringValue: "profile_id")!)
        case .searchPasswords(let query):
            try container.encode(query, forKey: DynamicCodingKey(stringValue: "query")!)
        case .deletePassword(let passwordId):
            try container.encode(passwordId, forKey: DynamicCodingKey(stringValue: "password_id")!)
        case .addPassword(let domain, let username):
            try container.encode(domain, forKey: DynamicCodingKey(stringValue: "domain")!)
            try container.encode(username, forKey: DynamicCodingKey(stringValue: "username")!)
        case .addAutofillAddress(let address):
            try container.encode(address, forKey: DynamicCodingKey(stringValue: "address")!)
        case .deleteAutofillAddress(let id):
            try container.encode(id, forKey: DynamicCodingKey(stringValue: "id")!)
        case .addAutofillPayment(let payment):
            try container.encode(payment, forKey: DynamicCodingKey(stringValue: "payment")!)
        case .deleteAutofillPayment(let id):
            try container.encode(id, forKey: DynamicCodingKey(stringValue: "id")!)
        case .resetSettings: break
        case .addToReadingList(let url, let title):
            try container.encode(url, forKey: DynamicCodingKey(stringValue: "url")!)
            try container.encode(title, forKey: DynamicCodingKey(stringValue: "title")!)
        case .removeFromReadingList(let itemId):
            try container.encode(itemId, forKey: DynamicCodingKey(stringValue: "item_id")!)
        case .markReadingListItemRead(let itemId):
            try container.encode(itemId, forKey: DynamicCodingKey(stringValue: "item_id")!)
        case .markReadingListItemUnread(let itemId):
            try container.encode(itemId, forKey: DynamicCodingKey(stringValue: "item_id")!)
        default:
            return false
        }
        return true
    }

    static func decodeAccountLibrary(kind: String, from container: KeyedDecodingContainer<DynamicCodingKey>) throws -> ShellEvent? {
        switch kind {
        // Extensions management
        case "toggle_extension":
            return .toggleExtension(extensionId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "extension_id")!))
        case "remove_extension":
            return .removeExtension(extensionId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "extension_id")!))

        // Account & Sync
        case "sign_in":
            return .signIn(
                email: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "email")!),
                displayName: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "display_name")!),
                password: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "password")!),
                accessToken: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "access_token")!),
                userId: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "user_id")!),
                deviceId: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "device_id")!))
        case "sign_out":
            return .signOut
        case "toggle_sync":
            return .toggleSync

        // Air traffic control
        case "create_traffic_rule":
            return .createTrafficRule(rule: try container.decode(TrafficRule.self, forKey: DynamicCodingKey(stringValue: "rule")!))
        case "delete_traffic_rule":
            return .deleteTrafficRule(ruleId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "rule_id")!))
        case "update_traffic_rule":
            return .updateTrafficRule(rule: try container.decode(TrafficRule.self, forKey: DynamicCodingKey(stringValue: "rule")!))
        case "set_default_link_behavior":
            return .setDefaultLinkBehavior(behavior: try container.decode(DefaultLinkBehavior.self, forKey: DynamicCodingKey(stringValue: "behavior")!))

        // Space settings
        case "update_space_config":
            return .updateSpaceConfig(
                changes: try container.decode(SpaceConfigUpdate.self, forKey: DynamicCodingKey(stringValue: "changes")!))

        // Profile management
        case "create_profile":
            return .createProfile(name: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "name")!))
        case "delete_profile":
            return .deleteProfile(profileId: try container.decode(ProfileId.self, forKey: DynamicCodingKey(stringValue: "profile_id")!))
        case "update_profile":
            return .updateProfile(
                profileId: try container.decode(ProfileId.self, forKey: DynamicCodingKey(stringValue: "profile_id")!),
                name: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "name")!),
                avatarColor: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "avatar_color")!),
                downloadPath: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "download_path")!),
                archiveTimeoutHours: try container.decodePatchable(Patchable<Double>.self, forKey: DynamicCodingKey(stringValue: "archive_timeout_hours")!))
        case "switch_profile":
            return .switchProfile(profileId: try container.decode(ProfileId.self, forKey: DynamicCodingKey(stringValue: "profile_id")!))

        // Passwords
        case "search_passwords":
            return .searchPasswords(query: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "query")!))
        case "delete_password":
            return .deletePassword(passwordId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "password_id")!))
        case "add_password":
            return .addPassword(
                domain: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "domain")!),
                username: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "username")!))

        // Autofill
        case "add_autofill_address":
            return .addAutofillAddress(address: try container.decode(AutofillAddress.self, forKey: DynamicCodingKey(stringValue: "address")!))
        case "delete_autofill_address":
            return .deleteAutofillAddress(id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!))
        case "add_autofill_payment":
            return .addAutofillPayment(payment: try container.decode(AutofillPayment.self, forKey: DynamicCodingKey(stringValue: "payment")!))
        case "delete_autofill_payment":
            return .deleteAutofillPayment(id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!))

        case "reset_settings":
            return .resetSettings

        // Reading List
        case "add_to_reading_list":
            return .addToReadingList(
                url: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "url")!),
                title: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "title")!))
        case "remove_from_reading_list":
            return .removeFromReadingList(itemId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "item_id")!))
        case "mark_reading_list_item_read":
            return .markReadingListItemRead(itemId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "item_id")!))
        case "mark_reading_list_item_unread":
            return .markReadingListItemUnread(itemId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "item_id")!))

        default:
            return nil
        }
    }
}
