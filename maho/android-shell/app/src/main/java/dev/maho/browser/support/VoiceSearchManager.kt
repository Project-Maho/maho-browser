package dev.maho.browser.support

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.speech.RecognitionListener
import android.speech.RecognizerIntent
import android.speech.SpeechRecognizer
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

enum class VoiceSearchState {
    Idle,
    Listening,
    Processing,
    Error,
}

class VoiceSearchManager(private val context: Context) {
    var state by mutableStateOf(VoiceSearchState.Idle)
        private set

    var resultText by mutableStateOf("")
        private set

    var finalResultText by mutableStateOf("")
        private set

    var errorMessage by mutableStateOf("")
        private set

    var audioLevel by mutableStateOf(0f)
        private set

    private var speechRecognizer: SpeechRecognizer? = null

    val isAvailable: Boolean
        get() = SpeechRecognizer.isRecognitionAvailable(context)

    fun startListening() {
        if (state == VoiceSearchState.Listening || state == VoiceSearchState.Processing) return

        if (!isAvailable) {
            state = VoiceSearchState.Error
            errorMessage = "Speech recognition not available"
            resetAudioLevel()
            return
        }

        cleanup()
        resultText = ""
        finalResultText = ""
        errorMessage = ""
        resetAudioLevel()

        val recognizer = SpeechRecognizer.createSpeechRecognizer(context)
        speechRecognizer = recognizer

        recognizer.setRecognitionListener(object : RecognitionListener {
            override fun onReadyForSpeech(params: Bundle?) {
                state = VoiceSearchState.Listening
            }

            override fun onBeginningOfSpeech() {}

            override fun onRmsChanged(rmsdB: Float) {
                updateAudioLevel(rmsdB)
            }

            override fun onBufferReceived(buffer: ByteArray?) {}

            override fun onEndOfSpeech() {
                resetAudioLevel()
                state = VoiceSearchState.Processing
            }

            override fun onError(error: Int) {
                resetAudioLevel()
                state = VoiceSearchState.Error
                errorMessage = mapErrorCode(error)
                cleanup()
            }

            override fun onResults(results: Bundle?) {
                val matches = results
                    ?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)
                val text = matches?.firstOrNull()?.trim().orEmpty()

                if (text.isNotEmpty()) {
                    resultText = text
                    finalResultText = text
                }
                state = VoiceSearchState.Idle
                cleanup()
            }

            override fun onPartialResults(partialResults: Bundle?) {
                val matches = partialResults
                    ?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)
                val text = matches?.firstOrNull()?.trim().orEmpty()
                if (text.isNotEmpty()) {
                    resultText = text
                }
            }

            override fun onEvent(eventType: Int, params: Bundle?) {}
        })

        val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
            putExtra(
                RecognizerIntent.EXTRA_LANGUAGE_MODEL,
                RecognizerIntent.LANGUAGE_MODEL_FREE_FORM,
            )
            val languageTag = VoiceRecognitionLanguageSettings.resolveLanguageTag(context)
            putExtra(RecognizerIntent.EXTRA_LANGUAGE, languageTag)
            putExtra(RecognizerIntent.EXTRA_LANGUAGE_PREFERENCE, languageTag)
            putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true)
            putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_COMPLETE_SILENCE_LENGTH_MILLIS, 5000L)
            putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_POSSIBLY_COMPLETE_SILENCE_LENGTH_MILLIS, 5000L)
        }

        recognizer.startListening(intent)
    }

    fun stopListening() {
        speechRecognizer?.stopListening()
        resetAudioLevel()
        if (state == VoiceSearchState.Listening) {
            state = VoiceSearchState.Processing
        }
    }

    fun reset() {
        cleanup()
        state = VoiceSearchState.Idle
        resultText = ""
        finalResultText = ""
        errorMessage = ""
        resetAudioLevel()
    }

    private fun cleanup() {
        speechRecognizer?.destroy()
        speechRecognizer = null
        resetAudioLevel()
    }

    private fun updateAudioLevel(rmsdB: Float) {
        if (rmsdB.isNaN() || rmsdB.isInfinite()) return
        val normalized = ((rmsdB - MinRmsDb) / RmsDbRange).coerceIn(0f, 1f)
        audioLevel = (audioLevel + (normalized - audioLevel) * AudioLevelSmoothing).coerceIn(0f, 1f)
    }

    private fun resetAudioLevel() {
        audioLevel = 0f
    }

    private fun mapErrorCode(error: Int): String = when (error) {
        SpeechRecognizer.ERROR_AUDIO -> "Audio recording error"
        SpeechRecognizer.ERROR_CLIENT -> "Client error"
        SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS -> "Microphone permission required"
        SpeechRecognizer.ERROR_NETWORK -> "Network error"
        SpeechRecognizer.ERROR_NETWORK_TIMEOUT -> "Network timeout"
        SpeechRecognizer.ERROR_NO_MATCH -> "No speech detected"
        SpeechRecognizer.ERROR_RECOGNIZER_BUSY -> "Recognizer busy"
        SpeechRecognizer.ERROR_SERVER -> "Server error"
        SpeechRecognizer.ERROR_SPEECH_TIMEOUT -> "No speech detected"
        else -> "Recognition failed"
    }

    private companion object {
        const val MinRmsDb = -2f
        const val RmsDbRange = 12f
        const val AudioLevelSmoothing = 0.35f
    }
}
