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
    nonisolated static let sttLocaleDefaultsKey = "voice.sttLocale"

    private nonisolated static let sttLocaleDidChangeNotification = Notification.Name("dev.maho.browser.voice.sttLocaleDidChange")

    var state: VoiceSearchState = .idle
    var audioLevel: Float = 0
    var transcript: String = ""

    private let log = Logger(subsystem: "dev.maho.browser", category: "VoiceSearch")
    private var audioEngine: AVAudioEngine?
    private var recognitionRequest: SFSpeechAudioBufferRecognitionRequest?
    private var recognitionTask: SFSpeechRecognitionTask?
    private var speechRecognizer: SFSpeechRecognizer?
    private var activeLocaleIdentifier: String?
    private nonisolated(unsafe) var localeChangeObserver: NSObjectProtocol?
    private var silenceTimer: Timer?
    private let silenceTimeout: TimeInterval = 5.0

    init() {
        activeLocaleIdentifier = Self.normalizedLocaleIdentifier(
            UserDefaults.standard.string(forKey: Self.sttLocaleDefaultsKey)
        )
        speechRecognizer = Self.makeSpeechRecognizer(for: activeLocaleIdentifier)
        observeLocaleChanges()
    }

    deinit {
        if let localeChangeObserver {
            NotificationCenter.default.removeObserver(localeChangeObserver)
        }
    }

    var isAvailable: Bool {
        speechRecognizer?.isAvailable ?? false
    }

    func applyLocale(_ identifier: String?) {
        let normalizedIdentifier = Self.normalizedLocaleIdentifier(identifier)
        guard normalizedIdentifier != activeLocaleIdentifier else { return }

        reset()
        activeLocaleIdentifier = normalizedIdentifier
        speechRecognizer = Self.makeSpeechRecognizer(for: normalizedIdentifier)
    }

    private nonisolated static func normalizedLocaleIdentifier(_ identifier: String?) -> String? {
        guard let trimmed = identifier?.trimmingCharacters(in: .whitespacesAndNewlines),
              !trimmed.isEmpty else {
            return nil
        }

        return trimmed
    }

    private static func makeSpeechRecognizer(for identifier: String?) -> SFSpeechRecognizer? {
        let locale = identifier.map { Locale(identifier: $0) } ?? Locale.current
        return SFSpeechRecognizer(locale: locale)
    }

    private func observeLocaleChanges() {
        localeChangeObserver = NotificationCenter.default.addObserver(
            forName: Self.sttLocaleDidChangeNotification,
            object: nil,
            queue: .main
        ) { notification in
            let identifier = notification.userInfo?[Self.sttLocaleDefaultsKey] as? String

            Task { @MainActor [weak self] in
                self?.applyLocale(identifier)
            }
        }
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
        audioLevel = 0

        if state == .listening {
            state = .idle
        }
    }

    private func startAudioSession(recognizer: SFSpeechRecognizer) throws {
        stopListening()
        transcript = ""
        audioLevel = 0

        let audioSession = AVAudioSession.sharedInstance()
        try audioSession.setCategory(.record, mode: .measurement, options: .duckOthers)
        try audioSession.setActive(true, options: .notifyOthersOnDeactivation)

        let engine = AVAudioEngine()
        self.audioEngine = engine

        let request = SFSpeechAudioBufferRecognitionRequest()
        request.shouldReportPartialResults = true
        // On-device only when supported; else server fallback. Forcing it true on
        // the simulator/unsupported locales fails with "Failed to initialize recognizer".
        request.requiresOnDeviceRecognition = recognizer.supportsOnDeviceRecognition
        self.recognitionRequest = request

        state = .listening
        resetSilenceTimer()

        recognitionTask = recognizer.recognitionTask(with: request) { [weak self] result, error in
            Task { @MainActor in
                guard let self else { return }

                if let result {
                    let text = result.bestTranscription.formattedString
                    self.transcript = text
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
        inputNode.installTap(onBus: 0, bufferSize: 1024, format: recordingFormat) { [weak self] buffer, _ in
            request.append(buffer)
            let rmsLevel = Self.normalizedRMSLevel(from: buffer)

            Task { @MainActor [weak self] in
                guard let self, self.state == .listening else { return }
                let smoothingWeight: Float = 0.22
                self.audioLevel = (self.audioLevel * (1 - smoothingWeight)) + (rmsLevel * smoothingWeight)
            }
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
        transcript = ""
        state = .idle
    }

    nonisolated private static func normalizedRMSLevel(from buffer: AVAudioPCMBuffer) -> Float {
        guard let channelData = buffer.floatChannelData?[0] else { return 0 }
        let frameLength = Int(buffer.frameLength)
        guard frameLength > 0 else { return 0 }

        var sumSquares: Float = 0
        for frame in 0..<frameLength {
            let sample = channelData[frame]
            sumSquares += sample * sample
        }

        let rms = sqrtf(sumSquares / Float(frameLength))
        return min(max(rms * 18, 0), 1)
    }
}
