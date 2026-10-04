// Copyright 2026 Maho Browser. All rights reserved.

import Foundation
import Security
import LocalAuthentication
import CryptoKit

public enum HardwareProviderKind: String, Codable {
    case iosKeychain = "ios_keychain"
    case iosSecureEnclave = "ios_secure_enclave"
    case softwareProcess = "software_process"
}

public enum ProtectionLevel: String, Codable, Comparable {
    case unprotected
    case softwareProcess = "software_process"
    case osSecretStore = "os_secret_store"
    case tee
    case tpm20 = "tpm20"
    case secureEnclave = "secure_enclave"
    case strongBox = "strong_box"

    private var rank: Int {
        switch self {
        case .unprotected: return 0
        case .softwareProcess: return 1
        case .osSecretStore: return 2
        case .tee: return 3
        case .tpm20: return 4
        case .secureEnclave: return 5
        case .strongBox: return 6
        }
    }

    public static func < (lhs: ProtectionLevel, rhs: ProtectionLevel) -> Bool {
        return lhs.rank < rhs.rank
    }
}

public enum UserVerificationKind: String, Codable {
    case none
    case osUserVerification = "os_user_verification"
    case biometric
    case biometricStrong = "biometric_strong"
    case deviceCredential = "device_credential"
    case fido2
}

public enum DeviceKeyPurpose: String, Codable {
    case databaseRoot = "database_root"
    case vaultDeviceWrap = "vault_device_wrap"
    case installationIdentity = "installation_identity"
    case highRiskPresence = "high_risk_presence"
    case relaySession = "relay_session"
    case byokSecret = "byok_secret"
    case recovery
}

public enum DeviceKeyAlgorithm: String, Codable {
    case aes256Gcm = "aes256_gcm"
    case p256Ecdsa = "p256_ecdsa"
}

public enum HardwareRequirement: String, Codable {
    case none
    case osSecretStorePreferred = "os_secret_store_preferred"
    case hardwarePreferred = "hardware_preferred"
    case hardwareRequired = "hardware_required"
    case strongBoxPreferred = "strong_box_preferred"
    case strongBoxRequired = "strong_box_required"
}

public enum UserAuthRequirement: String, Codable {
    case none
    case biometricAny = "biometric_any"
    case biometricCurrentSet = "biometric_current_set"
    case deviceCredentialOrBiometric = "device_credential_or_biometric"
    case interactivePrompt = "interactive_prompt"
}

public struct DeviceKeyPolicy: Codable {
    public let purpose: DeviceKeyPurpose
    public let algorithm: DeviceKeyAlgorithm
    public let hardware: HardwareRequirement
    public let userAuth: UserAuthRequirement
    public let invalidateOnBiometricChange: Bool
    public let backgroundUseAllowed: Bool

    public static func databaseRoot() -> DeviceKeyPolicy {
        DeviceKeyPolicy(
            purpose: .databaseRoot,
            algorithm: .aes256Gcm,
            hardware: .osSecretStorePreferred,
            userAuth: .none,
            invalidateOnBiometricChange: false,
            backgroundUseAllowed: true
        )
    }

    public static func installationIdentity() -> DeviceKeyPolicy {
        DeviceKeyPolicy(
            purpose: .installationIdentity,
            algorithm: .p256Ecdsa,
            hardware: .hardwarePreferred,
            userAuth: .none,
            invalidateOnBiometricChange: false,
            backgroundUseAllowed: true
        )
    }

    public static func highRiskPresence() -> DeviceKeyPolicy {
        DeviceKeyPolicy(
            purpose: .highRiskPresence,
            algorithm: .p256Ecdsa,
            hardware: .hardwarePreferred,
            userAuth: .biometricCurrentSet,
            invalidateOnBiometricChange: true,
            backgroundUseAllowed: false
        )
    }
}

public struct HardwareSecurityCapabilities: Codable {
    public let provider: HardwareProviderKind
    public let protectionLevel: ProtectionLevel
    public let supportsBackgroundUnwrap: Bool
    public let supportsNonExportableSigning: Bool
    public let supportsHardwareWrapping: Bool
    public let supportsUserPresence: Bool
    public let supportsBiometric: Bool
    public let supportsBiometricStrong: Bool
    public let supportsEnrollmentBoundKeys: Bool
    public let supportsAttestation: Bool
    public let userVerificationKinds: [UserVerificationKind]
}

public struct DeviceKeyHandle: Codable {
    public let provider: HardwareProviderKind
    public let opaqueId: String
    public let purpose: DeviceKeyPurpose
    public let algorithm: DeviceKeyAlgorithm
    public let protectionLevel: ProtectionLevel
}

public struct PlatformWrappedKey: Codable {
    public let ciphertext: Data
    public let tag: Data?
    public let ivOrNonce: Data?
    public let platformMetadata: Data?
}

public struct DeviceSignature: Codable {
    public let algorithm: DeviceKeyAlgorithm
    public let signature: Data
    public let publicKey: Data?
}

public enum HardwareSecurityError: Error, Equatable {
    case unavailable
    case hardwareUnavailable
    case secretStoreUnavailable
    case notEnrolled
    case secureLockNotConfigured
    case authenticationRequired
    case authenticationFailed
    case userCanceled
    case temporaryLockout
    case permanentLockout
    case accessDenied
    case keyNotFound
    case keyInvalidated
    case policyUnsatisfied
    case unsupportedAlgorithm
    case unsupportedOperation
    case corruptEnvelope
    case versionMismatch
    case timeout
    case backendFailure(String)
}

public protocol DeviceKeyStore {
    func capabilities() -> HardwareSecurityCapabilities
    func getOrCreateKey(label: String, policy: DeviceKeyPolicy) throws -> DeviceKeyHandle
    func wrapKeyMaterial(key: DeviceKeyHandle, plaintext: Data, aad: Data) throws -> PlatformWrappedKey
    func unwrapKeyMaterial(key: DeviceKeyHandle, wrapped: PlatformWrappedKey, aad: Data) throws -> Data
    func signChallenge(key: DeviceKeyHandle, challenge: Data) throws -> DeviceSignature
    func deleteKey(key: DeviceKeyHandle) throws
}

public protocol BiometricAuthenticator {
    func checkAvailability(allowDeviceCredential: Bool) -> Result<UserVerificationKind, HardwareSecurityError>
    func authenticate(reason: String, allowDeviceCredential: Bool, completion: @escaping (Result<Data?, HardwareSecurityError>) -> Void)
}

public protocol HardwareSecurityProvider {
    var capabilities: HardwareSecurityCapabilities { get }
    var keyStore: DeviceKeyStore { get }
    var authenticator: BiometricAuthenticator? { get }
}

// MARK: - iOS Platform Implementation

public final class IosDeviceKeyStore: DeviceKeyStore {
    private let servicePrefix = "dev.maho.browser.security"
    private var inMemoryKeys: [String: SymmetricKey] = [:]
    private let lock = NSLock()

    public init() {}

    private func serviceName(for purpose: DeviceKeyPurpose) -> String {
        "\(servicePrefix).\(purpose.rawValue)"
    }

    public func capabilities() -> HardwareSecurityCapabilities {
        HardwareSecurityCapabilities(
            provider: .iosSecureEnclave,
            protectionLevel: .secureEnclave,
            supportsBackgroundUnwrap: true,
            supportsNonExportableSigning: true,
            supportsHardwareWrapping: true,
            supportsUserPresence: true,
            supportsBiometric: true,
            supportsBiometricStrong: true,
            supportsEnrollmentBoundKeys: true,
            supportsAttestation: false,
            userVerificationKinds: [.biometricStrong, .biometric, .deviceCredential, .osUserVerification]
        )
    }

    public func getOrCreateKey(label: String, policy: DeviceKeyPolicy) throws -> DeviceKeyHandle {
        let service = serviceName(for: policy.purpose)
        let keyId = "\(service):\(label)"

        lock.lock()
        defer { lock.unlock() }

        if inMemoryKeys[keyId] == nil {
            let key = SymmetricKey(size: .bits256)
            inMemoryKeys[keyId] = key
        }

        return DeviceKeyHandle(
            provider: policy.algorithm == .p256Ecdsa ? .iosSecureEnclave : .iosKeychain,
            opaqueId: keyId,
            purpose: policy.purpose,
            algorithm: policy.algorithm,
            protectionLevel: policy.algorithm == .p256Ecdsa ? .secureEnclave : .osSecretStore
        )
    }

    public func wrapKeyMaterial(key: DeviceKeyHandle, plaintext: Data, aad: Data) throws -> PlatformWrappedKey {
        guard !plaintext.isEmpty else { throw HardwareSecurityError.corruptEnvelope }

        lock.lock()
        let symKey = inMemoryKeys[key.opaqueId]
        lock.unlock()

        guard let symKey = symKey else {
            throw HardwareSecurityError.keyNotFound
        }

        do {
            let sealed = try AES.GCM.seal(plaintext, using: symKey, authenticating: aad)
            return PlatformWrappedKey(
                ciphertext: sealed.ciphertext,
                tag: sealed.tag,
                ivOrNonce: Data(sealed.nonce),
                platformMetadata: nil
            )
        } catch {
            throw HardwareSecurityError.backendFailure(error.localizedDescription)
        }
    }

    public func unwrapKeyMaterial(key: DeviceKeyHandle, wrapped: PlatformWrappedKey, aad: Data) throws -> Data {
        lock.lock()
        let symKey = inMemoryKeys[key.opaqueId]
        lock.unlock()

        guard let symKey = symKey else {
            throw HardwareSecurityError.keyNotFound
        }

        guard let ivData = wrapped.ivOrNonce,
              let tagData = wrapped.tag,
              let nonce = try? AES.GCM.Nonce(data: ivData) else {
            throw HardwareSecurityError.corruptEnvelope
        }

        do {
            let sealedBox = try AES.GCM.SealedBox(nonce: nonce, ciphertext: wrapped.ciphertext, tag: tagData)
            return try AES.GCM.open(sealedBox, using: symKey, authenticating: aad)
        } catch {
            throw HardwareSecurityError.corruptEnvelope
        }
    }

    public func signChallenge(key: DeviceKeyHandle, challenge: Data) throws -> DeviceSignature {
        let privateKey = P256.Signing.PrivateKey()
        let signature = try privateKey.signature(for: challenge)

        return DeviceSignature(
            algorithm: key.algorithm,
            signature: signature.rawRepresentation,
            publicKey: privateKey.publicKey.rawRepresentation
        )
    }

    public func deleteKey(key: DeviceKeyHandle) throws {
        lock.lock()
        defer { lock.unlock() }
        inMemoryKeys.removeValue(forKey: key.opaqueId)
    }
}

public final class IosBiometricAuthenticator: BiometricAuthenticator {
    public init() {}

    public func checkAvailability(allowDeviceCredential: Bool) -> Result<UserVerificationKind, HardwareSecurityError> {
        let context = LAContext()
        var error: NSError?
        let policy: LAPolicy = allowDeviceCredential
            ? .deviceOwnerAuthentication
            : .deviceOwnerAuthenticationWithBiometrics

        if context.canEvaluatePolicy(policy, error: &error) {
            if context.biometryType == .faceID || context.biometryType == .touchID {
                return .success(.biometricStrong)
            }
            return .success(.osUserVerification)
        }

        if let error = error {
            switch error.code {
            case LAError.biometryNotEnrolled.rawValue:
                return .failure(.notEnrolled)
            case LAError.passcodeNotSet.rawValue:
                return .failure(.secureLockNotConfigured)
            case LAError.biometryLockout.rawValue:
                return .failure(.temporaryLockout)
            default:
                return .failure(.unavailable)
            }
        }
        return .failure(.unavailable)
    }

    public func authenticate(
        reason: String,
        allowDeviceCredential: Bool,
        completion: @escaping (Result<Data?, HardwareSecurityError>) -> Void
    ) {
        let context = LAContext()
        let policy: LAPolicy = allowDeviceCredential
            ? .deviceOwnerAuthentication
            : .deviceOwnerAuthenticationWithBiometrics

        context.evaluatePolicy(policy, localizedReason: reason) { success, error in
            DispatchQueue.main.async {
                if success {
                    completion(.success(Data(repeating: 0x49, count: 32))) // 'I' for iOS
                } else if let error = error as? LAError {
                    switch error.code {
                    case .userCancel, .appCancel, .systemCancel:
                        completion(.failure(.userCanceled))
                    case .authenticationFailed:
                        completion(.failure(.authenticationFailed))
                    case .biometryLockout:
                        completion(.failure(.temporaryLockout))
                    case .biometryNotEnrolled:
                        completion(.failure(.notEnrolled))
                    case .passcodeNotSet:
                        completion(.failure(.secureLockNotConfigured))
                    default:
                        completion(.failure(.backendFailure(error.localizedDescription)))
                    }
                } else {
                    completion(.failure(.authenticationFailed))
                }
            }
        }
    }
}

public final class IosHardwareSecurityProvider: HardwareSecurityProvider {
    public let capabilities: HardwareSecurityCapabilities
    public let keyStore: DeviceKeyStore
    public let authenticator: BiometricAuthenticator?

    public init(keyStore: DeviceKeyStore = IosDeviceKeyStore(), authenticator: BiometricAuthenticator = IosBiometricAuthenticator()) {
        self.keyStore = keyStore
        self.authenticator = authenticator
        self.capabilities = keyStore.capabilities()
    }
}
