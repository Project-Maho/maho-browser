import Darwin
import Foundation

enum SyncCoreError: LocalizedError, Equatable {
    case unavailable(String)
    case malformedStatus(String)
    case operationFailed(String)

    var errorDescription: String? {
        switch self {
        case .unavailable(let operation):
            return "The installed Maho core does not support \(operation)."
        case .malformedStatus(let detail):
            return "Core returned malformed sync status: \(detail)"
        case .operationFailed(let operation):
            return "Core sync operation failed: \(operation)."
        }
    }
}

enum SyncStateKind: String, Codable, Equatable {
    case idle
    case connecting
    case syncing
    case synced
    case error
    case offline
}

struct SyncStateResponse: Codable, Equatable {
    let kind: SyncStateKind
    let lastSuccessAt: Int64?
    let pendingOutboxCount: Int
    let lastError: String?

    var displayStatus: SyncStatus {
        switch kind {
        case .idle, .connecting:
            return .idle
        case .syncing:
            return .syncing(progress: 0)
        case .synced:
            let value = lastSuccessAt.map(String.init) ?? ""
            return .synced(lastSyncAt: value)
        case .error:
            return .error(message: lastError ?? "Sync failed.")
        case .offline:
            return .offline
        }
    }
}

struct SyncEnvelopeV2: Codable, Equatable {
    let protocolVersion: Int
    let deliveryId: String
    let payload: String
    let relaySeq: Int?
}

struct RelayAckV2: Codable, Equatable {
    let deliveryId: String
    let seq: Int
}

protocol SyncCoreClient: AnyObject {
    func signIn(
        email: String,
        displayName: String?,
        accessToken: String?,
        userId: String?,
        deviceId: String?
    )
    func signOut()
    func setSyncEnabled(_ enabled: Bool) throws
    func generateSyncBootstrap() throws -> RelaySyncBootstrap
    func configureSyncBootstrap(serverURL: String, seed: String) throws
    func syncState() throws -> SyncStateResponse
    func syncRoomID() throws -> String
    func leaseOutgoingEnvelopes() throws -> [SyncEnvelopeV2]
    func acknowledge(_ ack: RelayAckV2) throws
    func receiveCursor(roomID: String) throws -> Int
    func apply(_ envelope: SyncEnvelopeV2, roomID: String) throws
    func applySyncSnapshot(_ json: String) throws -> Int
}

private struct SyncBootstrapConfiguration: Decodable {
    let success: Bool
}

private enum DynamicSyncFFI {
    typealias GetString = @convention(c) (OpaquePointer?) -> UnsafeMutablePointer<CChar>?
    typealias ConfigureBootstrap = @convention(c) (
        OpaquePointer?,
        UnsafePointer<CChar>?,
        UnsafePointer<CChar>?
    ) -> UnsafeMutablePointer<CChar>?
    typealias HandleJSON = @convention(c) (OpaquePointer?, UnsafePointer<CChar>?) -> Int32
    typealias GetCursor = @convention(c) (OpaquePointer?, UnsafePointer<CChar>?) -> UInt64

    static func symbol<T>(_ name: String, as type: T.Type) -> T? {
        guard let pointer = dlsym(UnsafeMutableRawPointer(bitPattern: -2), name) else { return nil }
        return unsafeBitCast(pointer, to: type)
    }
}

extension MahoBridge: SyncCoreClient {
    func signIn(
        email: String,
        displayName: String?,
        accessToken: String?,
        userId: String?,
        deviceId: String?
    ) {
        signIn(
            email: email,
            displayName: displayName,
            password: nil,
            accessToken: accessToken,
            userId: userId,
            deviceId: deviceId
        )
    }

    func setSyncEnabled(_ enabled: Bool) throws {
        guard let json = withCore({ ptr in FFIString.consume(maho_core_get_account_state(ptr)) }) ?? nil else {
            throw SyncCoreError.operationFailed("read account Sync state")
        }
        guard let data = json.data(using: .utf8),
              let account = try? RelayCoding.decoder.decode(AccountInfo.self, from: data)
        else {
            throw SyncCoreError.operationFailed("read signed-in account Sync state")
        }
        guard account.syncEnabled != enabled else { return }
        toggleSync()
    }

    func generateSyncBootstrap() throws -> RelaySyncBootstrap {
        guard let json = generateSyncBootstrapJSON(),
              let data = json.data(using: .utf8),
              let bootstrap = try? RelayCoding.decoder.decode(RelaySyncBootstrap.self, from: data)
        else {
            throw SyncCoreError.operationFailed("generate account Sync bootstrap")
        }
        return bootstrap
    }

    func configureSyncBootstrap(serverURL: String, seed: String) throws {
        guard let json = configureSyncBootstrapJSON(serverURL: serverURL, seed: seed),
              let data = json.data(using: .utf8),
              let response = try? RelayCoding.decoder.decode(SyncBootstrapConfiguration.self, from: data),
              response.success
        else {
            throw SyncCoreError.operationFailed("configure account Sync bootstrap")
        }
    }

    func syncState() throws -> SyncStateResponse {
        guard let json = withCore({ ptr in FFIString.consume(maho_core_get_sync_status(ptr)) }) ?? nil else {
            throw SyncCoreError.operationFailed("read status")
        }
        do {
            return try RelayCoding.decoder.decode(SyncStateResponse.self, from: Data(json.utf8))
        } catch {
            throw SyncCoreError.malformedStatus(error.localizedDescription)
        }
    }

    func getSyncStatus() -> SyncStatus? {
        switch Result(catching: syncState) {
        case .success(let state):
            return state.displayStatus
        case .failure(let error):
            return .error(message: error.localizedDescription)
        }
    }

    func syncRoomID() throws -> String {
        guard let function = DynamicSyncFFI.symbol("maho_core_get_sync_room_id", as: DynamicSyncFFI.GetString.self) else {
            throw SyncCoreError.unavailable("recovery-derived room IDs")
        }
        guard let value = withCore({ ptr in FFIString.consume(function(ptr)) }) ?? nil,
              value.range(of: #"^[0-9a-f]{32}$"#, options: .regularExpression) != nil else {
            throw SyncCoreError.operationFailed("read recovery-derived room ID")
        }
        return value
    }

    func leaseOutgoingEnvelopes() throws -> [SyncEnvelopeV2] {
        guard let function = DynamicSyncFFI.symbol("maho_core_drain_outgoing_envelopes", as: DynamicSyncFFI.GetString.self) else {
            throw SyncCoreError.unavailable("V2 outbox leasing")
        }
        guard let json = withCore({ ptr in FFIString.consume(function(ptr)) }) ?? nil else {
            throw SyncCoreError.operationFailed("lease V2 outbox")
        }
        return try RelayCoding.decoder.decode([SyncEnvelopeV2].self, from: Data(json.utf8))
    }

    func acknowledge(_ ack: RelayAckV2) throws {
        guard let function = DynamicSyncFFI.symbol("maho_core_accept_sync_ack", as: DynamicSyncFFI.HandleJSON.self) else {
            throw SyncCoreError.unavailable("delivery ACK persistence")
        }
        let json = String(decoding: try RelayCoding.encoder.encode(ack), as: UTF8.self)
        let result = withCore { ptr in json.withCString { function(ptr, $0) } } ?? -1
        guard result == 0 else { throw SyncCoreError.operationFailed("persist delivery ACK") }
    }

    func receiveCursor(roomID: String) throws -> Int {
        guard let function = DynamicSyncFFI.symbol("maho_core_get_sync_receive_cursor", as: DynamicSyncFFI.GetCursor.self) else {
            throw SyncCoreError.unavailable("durable receive cursors")
        }
        let cursor = withCore { ptr in roomID.withCString { function(ptr, $0) } } ?? 0
        guard cursor <= UInt64(Int.max) else { throw SyncCoreError.operationFailed("read receive cursor") }
        return Int(cursor)
    }

    func apply(_ envelope: SyncEnvelopeV2, roomID: String) throws {
        guard let function = DynamicSyncFFI.symbol("maho_core_handle_incoming_sync_envelope", as: DynamicSyncFFI.HandleJSON.self) else {
            throw SyncCoreError.unavailable("V2 envelope application")
        }
        let json = String(decoding: try RelayCoding.encoder.encode(envelope), as: UTF8.self)
        let result = withCore { ptr in json.withCString { function(ptr, $0) } } ?? -1
        guard result == 0 else { throw SyncCoreError.operationFailed("apply V2 envelope and cursor") }
    }

    func applySyncSnapshot(_ json: String) throws -> Int {
        guard let function = DynamicSyncFFI.symbol("maho_core_apply_sync_snapshot", as: DynamicSyncFFI.HandleJSON.self) else {
            throw SyncCoreError.unavailable("V2 sync snapshot application")
        }
        let result = withCore { ptr in json.withCString { function(ptr, $0) } } ?? -1
        guard result >= 0 else { throw SyncCoreError.operationFailed("apply sync snapshot") }
        return Int(result)
    }

    func exportSyncSnapshot() throws -> String {
        guard let function = DynamicSyncFFI.symbol("maho_core_export_sync_snapshot", as: DynamicSyncFFI.GetString.self) else {
            throw SyncCoreError.unavailable("V2 sync snapshot export")
        }
        guard let json = withCore({ ptr in FFIString.consume(function(ptr)) }) ?? nil else {
            throw SyncCoreError.operationFailed("export sync snapshot")
        }
        return json
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

    private func generateSyncBootstrapJSON() -> String? {
        withCore { ptr in
            guard let function = DynamicSyncFFI.symbol(
                "maho_core_generate_sync_bootstrap",
                as: DynamicSyncFFI.GetString.self
            ) else {
                return nil
            }
            return FFIString.consume(function(ptr))
        } ?? nil
    }

    private func configureSyncBootstrapJSON(serverURL: String, seed: String) -> String? {
        withCore { ptr in
            guard let function = DynamicSyncFFI.symbol(
                "maho_core_configure_sync_bootstrap",
                as: DynamicSyncFFI.ConfigureBootstrap.self
            ) else {
                return nil
            }
            return serverURL.withCString { url in
                seed.withCString { bootstrapSeed in
                    FFIString.consume(function(ptr, url, bootstrapSeed))
                }
            }
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
            let serverUrl = RelayAPIClient.currentBaseURLString()
            return serverUrl.withCString { cUrl in
                recoveryPhrase.withCString { cPhrase in
                    FFIString.consume(maho_core_join_sync(ptr, cUrl, cPhrase))
                }
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

    func handleSyncUpdate(_ update: CoreUpdate) {
        switch update {
        case .syncStateChanged, .accountChanged, .accountSyncStateChanged:
            NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
        default:
            break
        }
    }
}
