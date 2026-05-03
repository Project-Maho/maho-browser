import Foundation

extension MahoBridge {

    func getSyncStatus() -> SyncStatus? {
        withCore { ptr in
            FFIString.consumeJSON(maho_core_get_sync_status(ptr))
        } ?? nil
    }

    func getConnectedDevices() -> [ConnectedDevice] {
        (withCore { ptr in
            FFIString.consumeJSON(maho_core_get_connected_devices(ptr))
        } ?? nil) ?? []
    }

    func generateSyncKey() -> String? {
        withCore { ptr in
            FFIString.consume(maho_core_generate_sync_key(ptr))
        } ?? nil
    }

    func signIn(email: String, displayName: String?, password: String? = nil, accessToken: String? = nil, userId: String? = nil, deviceId: String? = nil) {
        sendEvent(.signIn(email: email, displayName: displayName, password: password, accessToken: accessToken, userId: userId, deviceId: deviceId))
    }

    func signOut() {
        sendEvent(.signOut)
    }

    func toggleSync() {
        sendEvent(.toggleSync)
    }

    func sendTab(tabId: TabId, deviceId: String) {
        withCore { ptr in
            tabId.withCString { cUrl in
                "".withCString { cTitle in
                    deviceId.withCString { cDeviceId in
                        maho_core_send_tab(ptr, cUrl, cTitle, cDeviceId)
                    }
                }
            }
        }
    }

    func sendTabToDevice(url: String, title: String, deviceId: String) {
        withCore { ptr in
            url.withCString { cUrl in
                title.withCString { cTitle in
                    deviceId.withCString { cDeviceId in
                        maho_core_send_tab(ptr, cUrl, cTitle, cDeviceId)
                    }
                }
            }
        }
    }

    func joinSync(recoveryPhrase: String) -> String? {
        withCore { ptr in
            recoveryPhrase.withCString { cPhrase in
                FFIString.consume(maho_core_join_sync(ptr, cPhrase))
            }
        } ?? nil
    }

    func removeSyncDevice(deviceId: String) {
        withCore { ptr in
            deviceId.withCString { cDeviceId in
                maho_core_remove_sync_device(ptr, cDeviceId)
            }
        }
    }

    func applyRemoteEntities(entitiesJson: String) -> String? {
        withCore { ptr in
            entitiesJson.withCString { cStr in
                FFIString.consume(maho_core_apply_remote_entities(ptr, cStr))
            }
        } ?? nil
    }

    func drainOutgoing() -> String? {
        withCore { ptr in
            FFIString.consume(maho_core_drain_outgoing(ptr))
        } ?? nil
    }

    func handleSyncUpdate(_ update: CoreUpdate) {
        switch update {
        case .syncStateChanged, .accountChanged, .accountSyncStateChanged:
            NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
        default:
            break
        }
    }
}
