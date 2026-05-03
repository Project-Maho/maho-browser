import Foundation
import Speech
import AVFoundation
import os.log

enum VoiceSearchState: Equatable {
    case idle
    case listening
    case processing
    case result(String)
    case error(String)
}

@MainActor
@Observable
final class VoiceSearchManager {
    var state: VoiceSearchState = .idle

    private let log = Logger(subsystem: "dev.maho.browser", category: "VoiceSearch")
    private var audioEngine: AVAudioEngine?
    private var recognitionRequest: SFSpeechAudioBufferRecognitionRequest?
    private var recognitionTask: SFSpeechRecognitionTask?
    private var speechRecognizer: SFSpeechRecognizer?
    private var silenceTimer: Timer?
    private let silenceTimeout: TimeInterval = 5.0

    init() {
        speechRecognizer = SFSpeechRecognizer(locale: Locale.current)
    }

    var isAvailable: Bool {
        speechRecognizer?.isAvailable ?? false
    }

    func requestPermissions() async -> Bool {
        let speechStatus = await withCheckedContinuation { continuation in
            SFSpeechRecognizer.requestAuthorization { status in
                continuation.resume(returning: status)
            }
        }

        guard speechStatus == .authorized else {
            state = .error("Speech recognition not authorized")
            log.warning("Speech recognition authorization denied")
            return false
        }

        let audioStatus: Bool
        if #available(iOS 17.0, *) {
            audioStatus = await AVAudioApplication.requestRecordPermission()
        } else {
            audioStatus = await withCheckedContinuation { continuation in
                AVAudioSession.sharedInstance().requestRecordPermission { granted in
                    continuation.resume(returning: granted)
                }
            }
        }

        guard audioStatus else {
            state = .error("Microphone access not authorized")
            log.warning("Microphone permission denied")
            return false
        }

        return true
    }

    func startListening() {
        guard state == .idle || state != .listening else { return }

        Task {
            let permitted = await requestPermissions()
            guard permitted else { return }

            guard let recognizer = speechRecognizer, recognizer.isAvailable else {
                state = .error("Speech recognition unavailable")
                return
            }

            do {
                try startAudioSession(recognizer: recognizer)
            } catch {
                state = .error("Could not start audio: \(error.localizedDescription)")
                log.error("Audio start failed: \(error.localizedDescription, privacy: .public)")
            }
        }
    }

    func stopListening() {
        silenceTimer?.invalidate()
        silenceTimer = nil
        audioEngine?.stop()
        audioEngine?.inputNode.removeTap(onBus: 0)
        recognitionRequest?.endAudio()
        recognitionTask?.cancel()
        recognitionRequest = nil
        recognitionTask = nil

        if state == .listening {
            state = .idle
        }
    }

    private func startAudioSession(recognizer: SFSpeechRecognizer) throws {
        stopListening()

        let audioSession = AVAudioSession.sharedInstance()
        try audioSession.setCategory(.record, mode: .measurement, options: .duckOthers)
        try audioSession.setActive(true, options: .notifyOthersOnDeactivation)

        let engine = AVAudioEngine()
        self.audioEngine = engine

        let request = SFSpeechAudioBufferRecognitionRequest()
        request.shouldReportPartialResults = true
        request.requiresOnDeviceRecognition = true
        self.recognitionRequest = request

        state = .listening
        resetSilenceTimer()

        recognitionTask = recognizer.recognitionTask(with: request) { [weak self] result, error in
            Task { @MainActor in
                guard let self else { return }

                if let result {
                    let text = result.bestTranscription.formattedString
                    self.resetSilenceTimer()

                    if result.isFinal {
                        self.finishWithResult(text)
                    }
                }

                if let error {
                    let nsError = error as NSError
                    // Code 1 = recognition cancelled by user, not a real error
                    if nsError.domain == "kAFAssistantErrorDomain" && nsError.code == 1 {
                        return
                    }
                    self.log.error("Recognition error: \(error.localizedDescription, privacy: .public)")
                    if self.state == .listening {
                        self.state = .error("Recognition failed")
                    }
                    self.stopListening()
                }
            }
        }

        let inputNode = engine.inputNode
        let recordingFormat = inputNode.outputFormat(forBus: 0)
        inputNode.installTap(onBus: 0, bufferSize: 1024, format: recordingFormat) { buffer, _ in
            request.append(buffer)
        }

        engine.prepare()
        try engine.start()
        log.info("Voice search started")
    }

    private func finishWithResult(_ text: String) {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else {
            state = .idle
            stopListening()
            return
        }
        state = .result(trimmed)
        stopListening()
        log.info("Voice search result: \(trimmed, privacy: .public)")
    }

    private func resetSilenceTimer() {
        silenceTimer?.invalidate()
        silenceTimer = Timer.scheduledTimer(withTimeInterval: silenceTimeout, repeats: false) { [weak self] _ in
            Task { @MainActor in
                guard let self else { return }
                self.log.info("Silence timeout reached")
                self.recognitionRequest?.endAudio()
            }
        }
    }

    func reset() {
        stopListening()
        state = .idle
    }
}
