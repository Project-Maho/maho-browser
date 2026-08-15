package dev.maho.browser.sync

import android.app.Activity
import android.os.Bundle
import androidx.credentials.ClearCredentialStateRequest
import androidx.credentials.CredentialManager
import androidx.credentials.CustomCredential
import androidx.credentials.GetCredentialRequest
import androidx.credentials.exceptions.GetCredentialCancellationException
import com.google.android.libraries.identity.googleid.GetGoogleIdOption
import com.google.android.libraries.identity.googleid.GoogleIdTokenCredential
import java.security.SecureRandom
import java.util.Base64
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

sealed interface GoogleCredentialResult {
    data class IdToken(val token: String, val email: String) : GoogleCredentialResult
    data object Cancelled : GoogleCredentialResult
    data class Failure(val message: String) : GoogleCredentialResult
}

object GoogleCredentialParser {
    fun parse(type: String, data: Bundle): GoogleCredentialResult {
        if (type != GoogleIdTokenCredential.TYPE_GOOGLE_ID_TOKEN_CREDENTIAL) {
            return GoogleCredentialResult.Failure("Unsupported Google credential type")
        }
        return try {
            val credential = GoogleIdTokenCredential.createFrom(data)
            if (credential.idToken.isBlank()) {
                GoogleCredentialResult.Failure("Malformed Google credential")
            } else {
                GoogleCredentialResult.IdToken(credential.idToken, credential.id)
            }
        } catch (_: Exception) {
            GoogleCredentialResult.Failure("Malformed Google credential")
        }
    }
}

fun interface GoogleCredentialSource {
    suspend fun getCredential(serverClientId: String, nonce: String): GoogleCredentialResult
}

class CredentialManagerGoogleSource(
    private val activity: Activity,
    private val credentialManager: CredentialManager = CredentialManager.create(activity),
) : GoogleCredentialSource {
    override suspend fun getCredential(serverClientId: String, nonce: String): GoogleCredentialResult {
        val option = GetGoogleIdOption.Builder()
            .setServerClientId(serverClientId)
            .setNonce(nonce)
            .setFilterByAuthorizedAccounts(false)
            .setAutoSelectEnabled(false)
            .build()
        val request = GetCredentialRequest.Builder()
            .addCredentialOption(option)
            .build()
        return try {
            val credential = credentialManager.getCredential(activity, request).credential
            if (credential is CustomCredential) {
                GoogleCredentialParser.parse(credential.type, credential.data)
            } else {
                GoogleCredentialResult.Failure("Unsupported Google credential type")
            }
        } catch (_: GetCredentialCancellationException) {
            GoogleCredentialResult.Cancelled
        } catch (e: Exception) {
            GoogleCredentialResult.Failure(e.message ?: "Google sign-in failed")
        }
    }

    suspend fun clearCredentialState() {
        credentialManager.clearCredentialState(ClearCredentialStateRequest())
    }
}

fun interface GoogleRelayExchange {
    suspend fun exchange(serverUrl: String, idToken: String, nonce: String, fallbackEmail: String): RelaySession
}

fun interface GoogleSessionCommit {
    fun commit(session: RelaySession)
}

sealed interface GoogleSignInOutcome {
    data class SignedIn(val session: RelaySession) : GoogleSignInOutcome
    data object Cancelled : GoogleSignInOutcome
    data class Failure(val message: String) : GoogleSignInOutcome
}

class GoogleSignInCoordinator(
    private val credentials: GoogleCredentialSource,
    private val exchange: GoogleRelayExchange,
    private val commit: GoogleSessionCommit,
    private val nonceFactory: () -> String = ::secureNonce,
    private val exchangeDispatcher: CoroutineDispatcher = Dispatchers.IO,
) {
    suspend fun signIn(serverUrl: String, serverClientId: String): GoogleSignInOutcome {
        if (serverClientId.isBlank()) {
            return GoogleSignInOutcome.Failure("Google sign-in is not configured")
        }
        val nonce = nonceFactory()
        return when (val credential = credentials.getCredential(serverClientId, nonce)) {
            GoogleCredentialResult.Cancelled -> GoogleSignInOutcome.Cancelled
            is GoogleCredentialResult.Failure -> GoogleSignInOutcome.Failure(credential.message)
            is GoogleCredentialResult.IdToken -> try {
                val session = withContext(exchangeDispatcher) {
                    exchange.exchange(serverUrl, credential.token, nonce, credential.email)
                }
                commit.commit(session)
                GoogleSignInOutcome.SignedIn(session)
            } catch (e: Exception) {
                GoogleSignInOutcome.Failure(e.message ?: "Unable to sign in with Google")
            }
        }
    }
}

private val nonceRandom = SecureRandom()

private fun secureNonce(): String {
    val bytes = ByteArray(32)
    nonceRandom.nextBytes(bytes)
    return Base64.getUrlEncoder().withoutPadding().encodeToString(bytes)
}
