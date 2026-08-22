// Copyright 2026 Maho Browser. All rights reserved.

package dev.maho.browser.security

import android.content.Context
import android.os.Build
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyPermanentlyInvalidatedException
import android.security.keystore.KeyProperties
import androidx.biometric.BiometricManager
import androidx.biometric.BiometricPrompt
import androidx.core.content.ContextCompat
import androidx.fragment.app.FragmentActivity
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.SecureRandom
import java.security.Signature
import java.security.spec.ECGenParameterSpec
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

enum class HardwareProviderKind(val value: String) {
    ANDROID_KEYSTORE("android_keystore"),
    ANDROID_STRONGBOX("android_strongbox"),
    SOFTWARE_PROCESS("software_process")
}

enum class ProtectionLevel(val rank: Int) : Comparable<ProtectionLevel> {
    UNPROTECTED(0),
    SOFTWARE_PROCESS(1),
    OS_SECRET_STORE(2),
    TEE(3),
    TPM20(4),
    SECURE_ENCLAVE(5),
    STRONGBOX(6)
}

enum class UserVerificationKind(val value: String) {
    NONE("none"),
    OS_USER_VERIFICATION("os_user_verification"),
    BIOMETRIC("biometric"),
    BIOMETRIC_STRONG("biometric_strong"),
    DEVICE_CREDENTIAL("device_credential"),
    FIDO2("fido2")
}

enum class DeviceKeyPurpose(val value: String) {
    DATABASE_ROOT("database_root"),
    VAULT_DEVICE_WRAP("vault_device_wrap"),
    INSTALLATION_IDENTITY("installation_identity"),
    HIGH_RISK_PRESENCE("high_risk_presence"),
    RELAY_SESSION("relay_session"),
    BYOK_SECRET("byok_secret"),
    RECOVERY("recovery")
}

enum class DeviceKeyAlgorithm(val value: String) {
    AES256_GCM("aes256_gcm"),
    P256_ECDSA("p256_ecdsa")
}

enum class HardwareRequirement {
    NONE,
    OS_SECRET_STORE_PREFERRED,
    HARDWARE_PREFERRED,
    HARDWARE_REQUIRED,
    STRONGBOX_PREFERRED,
    STRONGBOX_REQUIRED
}

enum class UserAuthRequirement {
    NONE,
    BIOMETRIC_ANY,
    BIOMETRIC_CURRENT_SET,
    DEVICE_CREDENTIAL_OR_BIOMETRIC,
    INTERACTIVE_PROMPT
}

data class DeviceKeyPolicy(
    val purpose: DeviceKeyPurpose,
    val algorithm: DeviceKeyAlgorithm,
    val hardware: HardwareRequirement,
    val userAuth: UserAuthRequirement,
    val invalidateOnBiometricChange: Boolean,
    val backgroundUseAllowed: Boolean
) {
    companion object {
        fun databaseRoot() = DeviceKeyPolicy(
            purpose = DeviceKeyPurpose.DATABASE_ROOT,
            algorithm = DeviceKeyAlgorithm.AES256_GCM,
            hardware = HardwareRequirement.OS_SECRET_STORE_PREFERRED,
            userAuth = UserAuthRequirement.NONE,
            invalidateOnBiometricChange = false,
            backgroundUseAllowed = true
        )

        fun installationIdentity() = DeviceKeyPolicy(
            purpose = DeviceKeyPurpose.INSTALLATION_IDENTITY,
            algorithm = DeviceKeyAlgorithm.P256_ECDSA,
            hardware = HardwareRequirement.HARDWARE_PREFERRED,
            userAuth = UserAuthRequirement.NONE,
            invalidateOnBiometricChange = false,
            backgroundUseAllowed = true
        )

        fun highRiskPresence() = DeviceKeyPolicy(
            purpose = DeviceKeyPurpose.HIGH_RISK_PRESENCE,
            algorithm = DeviceKeyAlgorithm.P256_ECDSA,
            hardware = HardwareRequirement.HARDWARE_PREFERRED,
            userAuth = UserAuthRequirement.BIOMETRIC_CURRENT_SET,
            invalidateOnBiometricChange = true,
            backgroundUseAllowed = false
        )
    }
}

data class HardwareSecurityCapabilities(
    val provider: HardwareProviderKind,
    val protectionLevel: ProtectionLevel,
    val supportsBackgroundUnwrap: Boolean,
    val supportsNonExportableSigning: Boolean,
    val supportsHardwareWrapping: Boolean,
    val supportsUserPresence: Boolean,
    val supportsBiometric: Boolean,
    val supportsBiometricStrong: Boolean,
    val supportsEnrollmentBoundKeys: Boolean,
    val supportsAttestation: Boolean,
    val userVerificationKinds: List<UserVerificationKind>
)

data class DeviceKeyHandle(
    val provider: HardwareProviderKind,
    val opaqueId: String,
    val purpose: DeviceKeyPurpose,
    val algorithm: DeviceKeyAlgorithm,
    val protectionLevel: ProtectionLevel
)

data class PlatformWrappedKey(
    val ciphertext: ByteArray,
    val tag: ByteArray?,
    val ivOrNonce: ByteArray?,
    val platformMetadata: ByteArray?
)

data class DeviceSignature(
    val algorithm: DeviceKeyAlgorithm,
    val signature: ByteArray,
    val publicKey: ByteArray?
)

sealed class HardwareSecurityError : Exception() {
    object Unavailable : HardwareSecurityError()
    object HardwareUnavailable : HardwareSecurityError()
    object SecretStoreUnavailable : HardwareSecurityError()
    object NotEnrolled : HardwareSecurityError()
    object SecureLockNotConfigured : HardwareSecurityError()
    object AuthenticationRequired : HardwareSecurityError()
    object AuthenticationFailed : HardwareSecurityError()
    object UserCanceled : HardwareSecurityError()
    object TemporaryLockout : HardwareSecurityError()
    object PermanentLockout : HardwareSecurityError()
    object AccessDenied : HardwareSecurityError()
    object KeyNotFound : HardwareSecurityError()
    object KeyInvalidated : HardwareSecurityError()
    object PolicyUnsatisfied : HardwareSecurityError()
    object UnsupportedAlgorithm : HardwareSecurityError()
    object UnsupportedOperation : HardwareSecurityError()
    object CorruptEnvelope : HardwareSecurityError()
    object VersionMismatch : HardwareSecurityError()
    object Timeout : HardwareSecurityError()
    data class BackendFailure(override val message: String) : HardwareSecurityError()
}

interface DeviceKeyStore {
    fun capabilities(): HardwareSecurityCapabilities
    fun getOrCreateKey(label: String, policy: DeviceKeyPolicy): DeviceKeyHandle
    fun wrapKeyMaterial(key: DeviceKeyHandle, plaintext: ByteArray, aad: ByteArray): PlatformWrappedKey
    fun unwrapKeyMaterial(key: DeviceKeyHandle, wrapped: PlatformWrappedKey, aad: ByteArray): ByteArray
    fun signChallenge(key: DeviceKeyHandle, challenge: ByteArray): DeviceSignature
    fun deleteKey(key: DeviceKeyHandle)
}

interface BiometricAuthenticator {
    fun checkAvailability(allowDeviceCredential: Boolean): Result<UserVerificationKind>
    fun authenticate(
        activity: FragmentActivity,
        reason: String,
        allowDeviceCredential: Boolean,
        cryptoObject: BiometricPrompt.CryptoObject?,
        callback: (Result<BiometricPrompt.CryptoObject?>) -> Unit
    )
}

interface HardwareSecurityProvider {
    val capabilities: HardwareSecurityCapabilities
    val keyStore: DeviceKeyStore
    val authenticator: BiometricAuthenticator?
}

// MARK: - Android Implementation

class AndroidDeviceKeyStore(private val context: Context) : DeviceKeyStore {
    companion object {
        private const val ANDROID_KEYSTORE = "AndroidKeyStore"
        private const val AES_TRANSFORMATION = "AES/GCM/NoPadding"
        private const val ALIAS_PREFIX = "dev.maho.browser.security"
    }

    private val keyStore: KeyStore = KeyStore.getInstance(ANDROID_KEYSTORE).apply {
        load(null)
    }

    private var strongBoxSupported: Boolean = false

    init {
        strongBoxSupported = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            context.packageManager.hasSystemFeature(
                android.content.pm.PackageManager.FEATURE_STRONGBOX_KEYSTORE
            )
        } else {
            false
        }
    }

    private fun aliasFor(purpose: DeviceKeyPurpose, label: String): String {
        return "$ALIAS_PREFIX.${purpose.value}.$label"
    }

    override fun capabilities(): HardwareSecurityCapabilities {
        return HardwareSecurityCapabilities(
            provider = if (strongBoxSupported) HardwareProviderKind.ANDROID_STRONGBOX else HardwareProviderKind.ANDROID_KEYSTORE,
            protectionLevel = if (strongBoxSupported) ProtectionLevel.STRONGBOX else ProtectionLevel.TEE,
            supportsBackgroundUnwrap = true,
            supportsNonExportableSigning = true,
            supportsHardwareWrapping = true,
            supportsUserPresence = true,
            supportsBiometric = true,
            supportsBiometricStrong = true,
            supportsEnrollmentBoundKeys = true,
            supportsAttestation = true,
            userVerificationKinds = listOf(
                UserVerificationKind.BIOMETRIC_STRONG,
                UserVerificationKind.BIOMETRIC,
                UserVerificationKind.DEVICE_CREDENTIAL
            )
        )
    }

    override fun getOrCreateKey(label: String, policy: DeviceKeyPolicy): DeviceKeyHandle {
        val alias = aliasFor(policy.purpose, label)

        if (!keyStore.containsAlias(alias)) {
            if (policy.algorithm == DeviceKeyAlgorithm.AES256_GCM) {
                generateAesKey(alias, policy)
            } else {
                generateEcKey(alias, policy)
            }
        }

        return DeviceKeyHandle(
            provider = if (strongBoxSupported) HardwareProviderKind.ANDROID_STRONGBOX else HardwareProviderKind.ANDROID_KEYSTORE,
            opaqueId = alias,
            purpose = policy.purpose,
            algorithm = policy.algorithm,
            protectionLevel = if (strongBoxSupported) ProtectionLevel.STRONGBOX else ProtectionLevel.TEE
        )
    }

    private fun generateAesKey(alias: String, policy: DeviceKeyPolicy) {
        val keyGen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE)
        val builder = KeyGenParameterSpec.Builder(
            alias,
            KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT
        )
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
            .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
            .setKeySize(256)
            .setRandomizedEncryptionRequired(true)

        if (policy.userAuth != UserAuthRequirement.NONE) {
            builder.setUserAuthenticationRequired(true)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                builder.setUserAuthenticationParameters(
                    0,
                    KeyProperties.AUTH_BIOMETRIC_STRONG
                )
            }
            if (policy.invalidateOnBiometricChange && Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                builder.setInvalidatedByBiometricEnrollment(true)
            }
        }

        if (strongBoxSupported && Build.VERSION.SDK_INT >= Build.VERSION_CODES.P &&
            (policy.hardware == HardwareRequirement.STRONGBOX_PREFERRED || policy.hardware == HardwareRequirement.STRONGBOX_REQUIRED)) {
            try {
                builder.setIsStrongBoxBacked(true)
                keyGen.init(builder.build())
                keyGen.generateKey()
                return
            } catch (e: Exception) {
                if (policy.hardware == HardwareRequirement.STRONGBOX_REQUIRED) {
                    throw HardwareSecurityError.PolicyUnsatisfied
                }
                // Fallback to TEE
                builder.setIsStrongBoxBacked(false)
            }
        }

        keyGen.init(builder.build())
        keyGen.generateKey()
    }

    private fun generateEcKey(alias: String, policy: DeviceKeyPolicy) {
        val kpg = KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_EC, ANDROID_KEYSTORE)
        val builder = KeyGenParameterSpec.Builder(
            alias,
            KeyProperties.PURPOSE_SIGN or KeyProperties.PURPOSE_VERIFY
        )
            .setAlgorithmParameterSpec(ECGenParameterSpec("secp256r1"))
            .setDigests(KeyProperties.DIGEST_SHA256)

        if (policy.userAuth != UserAuthRequirement.NONE) {
            builder.setUserAuthenticationRequired(true)
        }

        if (strongBoxSupported && Build.VERSION.SDK_INT >= Build.VERSION_CODES.P &&
            (policy.hardware == HardwareRequirement.STRONGBOX_PREFERRED || policy.hardware == HardwareRequirement.STRONGBOX_REQUIRED)) {
            try {
                builder.setIsStrongBoxBacked(true)
                kpg.initialize(builder.build())
                kpg.generateKeyPair()
                return
            } catch (e: Exception) {
                if (policy.hardware == HardwareRequirement.STRONGBOX_REQUIRED) {
                    throw HardwareSecurityError.PolicyUnsatisfied
                }
                builder.setIsStrongBoxBacked(false)
            }
        }

        kpg.initialize(builder.build())
        kpg.generateKeyPair()
    }

    override fun wrapKeyMaterial(
        key: DeviceKeyHandle,
        plaintext: ByteArray,
        aad: ByteArray
    ): PlatformWrappedKey {
        if (plaintext.isEmpty()) throw HardwareSecurityError.CorruptEnvelope

        val secretKey = keyStore.getKey(key.opaqueId, null) as? SecretKey
            ?: throw HardwareSecurityError.KeyNotFound

        try {
            val cipher = Cipher.getInstance(AES_TRANSFORMATION)
            cipher.init(Cipher.ENCRYPT_MODE, secretKey)
            cipher.updateAAD(aad)
            val ciphertext = cipher.doFinal(plaintext)
            val iv = cipher.iv

            return PlatformWrappedKey(
                ciphertext = ciphertext,
                tag = null, // Tag is appended in GCM ciphertext
                ivOrNonce = iv,
                platformMetadata = null
            )
        } catch (e: KeyPermanentlyInvalidatedException) {
            throw HardwareSecurityError.KeyInvalidated
        } catch (e: Exception) {
            throw HardwareSecurityError.BackendFailure(e.message ?: "encryption failed")
        }
    }

    override fun unwrapKeyMaterial(
        key: DeviceKeyHandle,
        wrapped: PlatformWrappedKey,
        aad: ByteArray
    ): ByteArray {
        val secretKey = keyStore.getKey(key.opaqueId, null) as? SecretKey
            ?: throw HardwareSecurityError.KeyNotFound

        val iv = wrapped.ivOrNonce ?: throw HardwareSecurityError.CorruptEnvelope

        try {
            val cipher = Cipher.getInstance(AES_TRANSFORMATION)
            val spec = GCMParameterSpec(128, iv)
            cipher.init(Cipher.DECRYPT_MODE, secretKey, spec)
            cipher.updateAAD(aad)
            return cipher.doFinal(wrapped.ciphertext)
        } catch (e: KeyPermanentlyInvalidatedException) {
            throw HardwareSecurityError.KeyInvalidated
        } catch (e: Exception) {
            throw HardwareSecurityError.CorruptEnvelope
        }
    }

    override fun signChallenge(key: DeviceKeyHandle, challenge: ByteArray): DeviceSignature {
        val privateKeyEntry = keyStore.getEntry(key.opaqueId, null) as? KeyStore.PrivateKeyEntry
            ?: throw HardwareSecurityError.KeyNotFound

        try {
            val signature = Signature.getInstance("SHA256withECDSA")
            signature.initSign(privateKeyEntry.privateKey)
            signature.update(challenge)
            val sigBytes = signature.sign()

            return DeviceSignature(
                algorithm = key.algorithm,
                signature = sigBytes,
                publicKey = privateKeyEntry.certificate.publicKey.encoded
            )
        } catch (e: KeyPermanentlyInvalidatedException) {
            throw HardwareSecurityError.KeyInvalidated
        } catch (e: Exception) {
            throw HardwareSecurityError.BackendFailure(e.message ?: "signing failed")
        }
    }

    override fun deleteKey(key: DeviceKeyHandle) {
        if (keyStore.containsAlias(key.opaqueId)) {
            keyStore.deleteEntry(key.opaqueId)
        }
    }
}

class AndroidBiometricAuthenticator(private val context: Context) : BiometricAuthenticator {
    override fun checkAvailability(allowDeviceCredential: Boolean): Result<UserVerificationKind> {
        val biometricManager = BiometricManager.from(context)
        val authenticators = if (allowDeviceCredential) {
            BiometricManager.Authenticators.BIOMETRIC_STRONG or BiometricManager.Authenticators.DEVICE_CREDENTIAL
        } else {
            BiometricManager.Authenticators.BIOMETRIC_STRONG
        }

        return when (biometricManager.canAuthenticate(authenticators)) {
            BiometricManager.BIOMETRIC_SUCCESS -> Result.success(UserVerificationKind.BIOMETRIC_STRONG)
            BiometricManager.BIOMETRIC_ERROR_NONE_ENROLLED -> Result.failure(HardwareSecurityError.NotEnrolled)
            BiometricManager.BIOMETRIC_ERROR_NO_HARDWARE -> Result.failure(HardwareSecurityError.HardwareUnavailable)
            BiometricManager.BIOMETRIC_ERROR_HW_UNAVAILABLE -> Result.failure(HardwareSecurityError.TemporaryLockout)
            BiometricManager.BIOMETRIC_ERROR_SECURITY_UPDATE_REQUIRED -> Result.failure(HardwareSecurityError.Unavailable)
            else -> Result.failure(HardwareSecurityError.Unavailable)
        }
    }

    override fun authenticate(
        activity: FragmentActivity,
        reason: String,
        allowDeviceCredential: Boolean,
        cryptoObject: BiometricPrompt.CryptoObject?,
        callback: (Result<BiometricPrompt.CryptoObject?>) -> Unit
    ) {
        val executor = ContextCompat.getMainExecutor(activity)
        val prompt = BiometricPrompt(
            activity,
            executor,
            object : BiometricPrompt.AuthenticationCallback() {
                override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                    callback(Result.success(result.cryptoObject))
                }

                override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                    val error = when (errorCode) {
                        BiometricPrompt.ERROR_USER_CANCELED,
                        BiometricPrompt.ERROR_NEGATIVE_BUTTON,
                        BiometricPrompt.ERROR_CANCELED -> HardwareSecurityError.UserCanceled
                        BiometricPrompt.ERROR_LOCKOUT -> HardwareSecurityError.TemporaryLockout
                        BiometricPrompt.ERROR_LOCKOUT_PERMANENT -> HardwareSecurityError.PermanentLockout
                        BiometricPrompt.ERROR_NO_BIOMETRICS -> HardwareSecurityError.NotEnrolled
                        BiometricPrompt.ERROR_NO_DEVICE_CREDENTIAL -> HardwareSecurityError.SecureLockNotConfigured
                        else -> HardwareSecurityError.AuthenticationFailed
                    }
                    callback(Result.failure(error))
                }

                override fun onAuthenticationFailed() {
                    // Intermediate failed attempt, user may retry
                }
            }
        )

        val promptInfoBuilder = BiometricPrompt.PromptInfo.Builder()
            .setTitle("Maho Security Verification")
            .setSubtitle(reason)

        if (allowDeviceCredential) {
            promptInfoBuilder.setAllowedAuthenticators(
                BiometricManager.Authenticators.BIOMETRIC_STRONG or BiometricManager.Authenticators.DEVICE_CREDENTIAL
            )
        } else {
            promptInfoBuilder.setAllowedAuthenticators(BiometricManager.Authenticators.BIOMETRIC_STRONG)
            promptInfoBuilder.setNegativeButtonText("Cancel")
        }

        if (cryptoObject != null) {
            prompt.authenticate(promptInfoBuilder.build(), cryptoObject)
        } else {
            prompt.authenticate(promptInfoBuilder.build())
        }
    }
}

class AndroidHardwareSecurityProvider(
    context: Context,
    override val keyStore: DeviceKeyStore = AndroidDeviceKeyStore(context),
    override val authenticator: BiometricAuthenticator = AndroidBiometricAuthenticator(context)
) : HardwareSecurityProvider {
    override val capabilities: HardwareSecurityCapabilities
        get() = keyStore.capabilities()
}
