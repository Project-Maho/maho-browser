import Foundation

// MARK: - Account, Profile & Sync Operations

extension MahoBridge {

    // MARK: - Profiles

    func createProfile(name: String) -> String? {
        return withFFIString(name) { cStr -> String? in
            return callCoreString { maho_core_create_profile($0, cStr) }
        }
    }

    func deleteProfile(profileId: String) -> Bool {
        return withFFIString(profileId) { cStr -> Bool in
            guard let result = callCoreNonOptional({ maho_core_delete_profile($0, cStr) }) else { return false }
            return result
        }
    }

    func listProfiles() -> String? {
        return callCoreString { maho_core_list_profiles($0) }
    }

    func switchProfile(profileId: String) -> Bool {
        return withFFIString(profileId) { cStr -> Bool in
            guard let result = callCoreNonOptional({ maho_core_switch_profile($0, cStr) }) else { return false }
            return result
        }
    }

    func getProfileDataStoreId(profileId: String) -> String? {
        return withFFIString(profileId) { cStr -> String? in
            return callCoreString { maho_core_get_profile_data_store_id($0, cStr) }
        }
    }

    func getActiveProfileId() -> String? {
        return callCoreString { maho_core_get_active_profile_id($0) }
    }

    // MARK: - Sync

    func startSync(serverUrl: String, syncKey: String) {
        withFFIString(serverUrl) { urlCStr in
            withFFIString(syncKey) { keyCStr in
                callCoreVoid { maho_core_start_sync($0, urlCStr, keyCStr) }
            }
        }
    }

    func stopSync() {
        callCoreVoid { maho_core_stop_sync($0) }
    }

    func getSyncStatus() -> String? {
        return callCoreString { maho_core_get_sync_status($0) }
    }

    func getConnectedDevices() -> String? {
        return callCoreString { maho_core_get_connected_devices($0) }
    }

    func sendTab(url: String, title: String, targetDeviceId: String) {
        withFFIString(url) { urlCStr in
            withFFIString(title) { titleCStr in
                withFFIString(targetDeviceId) { deviceCStr in
                    callCoreVoid { maho_core_send_tab($0, urlCStr, titleCStr, deviceCStr) }
                }
            }
        }
    }

    func generateSyncKey() -> String? {
        return callCoreString { maho_core_generate_sync_key($0) }
    }

    func joinSync(recoveryPhrase: String) -> String? {
        return withFFIString(recoveryPhrase) { cStr -> String? in
            return callCoreString { maho_core_join_sync($0, cStr) }
        }
    }

    func removeSyncDevice(deviceId: String) {
        withFFIString(deviceId) { cStr in
            callCoreVoid { maho_core_remove_sync_device($0, cStr) }
        }
    }

    func applyRemoteEntities(entitiesJson: String) -> String? {
        return withFFIString(entitiesJson) { cStr -> String? in
            return callCoreString { maho_core_apply_remote_entities($0, cStr) }
        }
    }

    func drainOutgoing() -> String? {
        return callCoreString { maho_core_drain_outgoing($0) }
    }

    func sendTabToDevice(deviceId: String, tabJson: String) -> String? {
        return withFFIString(deviceId) { deviceCStr -> String? in
            return withFFIString(tabJson) { tabCStr -> String? in
                return callCoreString { maho_core_send_tab_to_device($0, deviceCStr, tabCStr) }
            }
        }
    }

    // MARK: - Backup

    func createBackup(configJson: String) -> String? {
        return withFFIString(configJson) { cStr -> String? in
            return callCoreString { maho_core_create_backup($0, cStr) }
        }
    }

    func restoreBackup(backupB64: String, password: String) -> String? {
        return withFFIString(backupB64) { backupCStr -> String? in
            return withFFIString(password) { passwordCStr -> String? in
                return callCoreString { maho_core_restore_backup($0, backupCStr, passwordCStr) }
            }
        }
    }

    func getBackupHistory() -> String? {
        return callCoreString { maho_core_get_backup_history($0) }
    }
}
