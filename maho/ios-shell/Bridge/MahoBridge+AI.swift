import Foundation

// MARK: - FFI declarations

@_silgen_name("maho_core_byok_set_key")
private func ffiByokSetKey(
    _ ptr: OpaquePointer?,
    _ provider: UnsafePointer<CChar>?,
    _ key: UnsafePointer<CChar>?
) -> Bool

@_silgen_name("maho_core_byok_get_key")
private func ffiByokGetKey(
    _ ptr: OpaquePointer?,
    _ provider: UnsafePointer<CChar>?
) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_core_byok_delete_key")
private func ffiByokDeleteKey(
    _ ptr: OpaquePointer?,
    _ provider: UnsafePointer<CChar>?
) -> Bool

@_silgen_name("maho_core_byok_get_providers")
private func ffiByokGetProviders(
    _ ptr: OpaquePointer?
) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_core_byok_validate_key")
private func ffiByokValidateKey(
    _ ptr: OpaquePointer?,
    _ provider: UnsafePointer<CChar>?,
    _ key: UnsafePointer<CChar>?
) -> Bool

@_silgen_name("maho_core_get_space_ai_config")
private func ffiGetSpaceAIConfig(
    _ ptr: OpaquePointer?,
    _ spaceId: UnsafePointer<CChar>?
) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_core_set_space_ai_config")
private func ffiSetSpaceAIConfig(
    _ ptr: OpaquePointer?,
    _ spaceId: UnsafePointer<CChar>?,
    _ configJson: UnsafePointer<CChar>?
) -> Bool

@_silgen_name("maho_core_managed_proxy_url")
private func ffiManagedProxyUrl() -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_core_free_string")
private func ffiFreeString(_ s: UnsafeMutablePointer<CChar>?)





// MARK: - Bridge extension

extension MahoBridge {

    // MARK: BYOK

    func byokGetProviders() -> [String] {
        withCore { ptr in
            FFIString.consumeJSON(ffiByokGetProviders(ptr), as: [String].self) ?? []
        } ?? []
    }

    func byokGetKey(provider: String) -> String? {
        return BYOKKeychain.get(provider: provider)
    }

    func byokSetKey(provider: String, key: String) -> Bool {
        return BYOKKeychain.set(provider: provider, key: key)
    }

    func byokDeleteKey(provider: String) -> Bool {
        return BYOKKeychain.delete(provider: provider)
    }

    func byokValidateKey(provider: String, key: String) -> Bool {
        withCore { ptr in
            FFIString.withCString(provider) { providerPointer in
                FFIString.withCString(key) { keyPointer in
                    ffiByokValidateKey(ptr, providerPointer, keyPointer)
                }
            }
        } ?? false
    }

    // MARK: Space AI Config

    func getSpaceAIConfig(spaceId: String) -> SpaceAIConfig? {
        withCore { ptr in
            FFIString.withCString(spaceId) { spaceIdPointer in
                FFIString.consumeJSON(ffiGetSpaceAIConfig(ptr, spaceIdPointer), as: SpaceAIConfig.self)
            }
        } ?? nil
    }

    func setSpaceAIConfig(spaceId: String, config: SpaceAIConfig) -> Bool {
        guard let jsonData = try? JSONEncoder().encode(config),
              let jsonString = String(data: jsonData, encoding: .utf8) else {
            return false
        }
        return withCore { ptr in
            FFIString.withCString(spaceId) { spaceIdPointer in
                FFIString.withCString(jsonString) { configPointer in
                    ffiSetSpaceAIConfig(ptr, spaceIdPointer, configPointer)
                }
            }
        } ?? false
    }

    // MARK: Managed AI (free credits via relay session)

    func managedProxyURL() -> String? {
        guard let raw = ffiManagedProxyUrl() else { return nil }
        defer { ffiFreeString(raw) }
        let url = String(cString: raw)
        return url.isEmpty ? nil : url
    }

    // MARK: Offline Model (stubs)

    func offlineModelInfo() -> OfflineModelInfo? {
        return OfflineModelInfo(status: "unavailable", size: 0)
    }

    func offlineModelIsDownloaded() -> Bool {
        return false
    }

    func offlineModelDelete() -> Bool {
        return true
    }
}
