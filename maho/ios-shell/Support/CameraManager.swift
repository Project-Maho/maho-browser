import AVFoundation
import PhotosUI
import UIKit
import os.log

enum CameraState: Equatable {
    case idle
    case preparing
    case capturing
    case captured(Data)
    case error(String)

    static func == (lhs: CameraState, rhs: CameraState) -> Bool {
        switch (lhs, rhs) {
        case (.idle, .idle), (.preparing, .preparing), (.capturing, .capturing):
            return true
        case (.captured(let a), .captured(let b)):
            return a == b
        case (.error(let a), .error(let b)):
            return a == b
        default:
            return false
        }
    }
}

@MainActor
@Observable
final class CameraManager: NSObject {
    var state: CameraState = .idle

    private(set) var session: AVCaptureSession?
    private var photoOutput: AVCapturePhotoOutput?
    private var photoContinuation: CheckedContinuation<Data, any Error>?

    private let log = Logger(subsystem: "dev.maho.browser", category: "Camera")

    // MARK: - Availability

    /// Returns true only when physical camera hardware is present (false on simulator).
    static var isCameraAvailable: Bool {
        AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .back) != nil
    }

    // MARK: - Permission

    func requestPermission() async -> Bool {
        let status = AVCaptureDevice.authorizationStatus(for: .video)
        switch status {
        case .authorized:
            return true
        case .notDetermined:
            let granted = await AVCaptureDevice.requestAccess(for: .video)
            if !granted {
                state = .error("Camera access denied")
                log.warning("Camera permission denied by user")
            }
            return granted
        case .denied, .restricted:
            state = .error("Camera access denied. Open Settings to allow.")
            log.warning("Camera permission denied/restricted")
            return false
        @unknown default:
            return false
        }
    }

    // MARK: - Session Setup

    func prepareSession() async {
        guard state == .idle else { return }
        state = .preparing

        let permitted = await requestPermission()
        guard permitted else { return }

        let captureSession = AVCaptureSession()
        captureSession.sessionPreset = .photo

        guard let device = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .back) else {
            state = .error("No camera available")
            log.error("No back camera device found")
            return
        }

        do {
            let input = try AVCaptureDeviceInput(device: device)
            guard captureSession.canAddInput(input) else {
                state = .error("Cannot configure camera")
                return
            }
            captureSession.addInput(input)
        } catch {
            state = .error("Camera setup failed: \(error.localizedDescription)")
            log.error("AVCaptureDeviceInput error: \(error.localizedDescription, privacy: .public)")
            return
        }

        let output = AVCapturePhotoOutput()
        guard captureSession.canAddOutput(output) else {
            state = .error("Cannot configure photo output")
            return
        }
        captureSession.addOutput(output)
        self.photoOutput = output
        self.session = captureSession

        // Start session on background thread
        Task.detached { [captureSession] in
            captureSession.startRunning()
        }

        state = .idle
        log.info("Camera session prepared")
    }

    // MARK: - Capture

    func capturePhoto() async throws -> Data {
        guard let output = photoOutput, let captureSession = session, captureSession.isRunning else {
            throw CameraError.sessionNotReady
        }

        state = .capturing

        let data = try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Data, any Error>) in
            self.photoContinuation = continuation
            let settings = AVCapturePhotoSettings(format: [AVVideoCodecKey: AVVideoCodecType.jpeg])
            output.capturePhoto(with: settings, delegate: self)
        }

        state = .captured(data)
        log.info("Photo captured: \(data.count) bytes")
        return data
    }

    // MARK: - Library Picker

    func pickFromLibrary() async throws -> Data {
        try await withCheckedThrowingContinuation { continuation in
            Task { @MainActor in
                let picker = LibraryPickerCoordinator(continuation: continuation)
                picker.present()
            }
        }
    }

    // MARK: - Teardown

    func tearDown() {
        Task.detached { [session] in
            session?.stopRunning()
        }
        session = nil
        photoOutput = nil
        photoContinuation = nil
        state = .idle
        log.info("Camera session torn down")
    }

    func reset() {
        state = .idle
    }
}

// MARK: - AVCapturePhotoCaptureDelegate

extension CameraManager: AVCapturePhotoCaptureDelegate {
    nonisolated func photoOutput(
        _ output: AVCapturePhotoOutput,
        didFinishProcessingPhoto photo: AVCapturePhoto,
        error: (any Error)?
    ) {
        Task { @MainActor in
            if let error {
                photoContinuation?.resume(throwing: error)
                photoContinuation = nil
                state = .error("Capture failed: \(error.localizedDescription)")
                return
            }

            guard let data = photo.fileDataRepresentation() else {
                photoContinuation?.resume(throwing: CameraError.noImageData)
                photoContinuation = nil
                state = .error("No image data")
                return
            }

            // Compress to reasonable JPEG size (max 1MB)
            let compressed = compressJPEG(data, maxBytes: 1_048_576)
            photoContinuation?.resume(returning: compressed)
            photoContinuation = nil
        }
    }

    private nonisolated func compressJPEG(_ data: Data, maxBytes: Int) -> Data {
        guard let image = UIImage(data: data) else { return data }
        var quality: CGFloat = 0.8
        var result = image.jpegData(compressionQuality: quality) ?? data

        while result.count > maxBytes && quality > 0.1 {
            quality -= 0.1
            result = image.jpegData(compressionQuality: quality) ?? result
        }

        return result
    }
}

// MARK: - Errors

enum CameraError: LocalizedError {
    case sessionNotReady
    case noImageData
    case permissionDenied
    case pickerCancelled

    var errorDescription: String? {
        switch self {
        case .sessionNotReady: "Camera session is not ready"
        case .noImageData: "Failed to capture image data"
        case .permissionDenied: "Camera access was denied"
        case .pickerCancelled: "Photo selection was cancelled"
        }
    }
}

// MARK: - Library Picker Coordinator

@MainActor
final class LibraryPickerCoordinator: NSObject, PHPickerViewControllerDelegate {
    private var continuation: CheckedContinuation<Data, any Error>?

    init(continuation: CheckedContinuation<Data, any Error>) {
        self.continuation = continuation
        super.init()
    }

    func present() {
        var config = PHPickerConfiguration()
        config.selectionLimit = 1
        config.filter = .images

        let picker = PHPickerViewController(configuration: config)
        picker.delegate = self

        guard let scene = UIApplication.shared.connectedScenes.first as? UIWindowScene,
              let rootVC = scene.windows.first?.rootViewController else {
            continuation?.resume(throwing: CameraError.sessionNotReady)
            continuation = nil
            return
        }

        // Find the topmost presented view controller
        var topVC = rootVC
        while let presented = topVC.presentedViewController {
            topVC = presented
        }

        topVC.present(picker, animated: true)
    }

    nonisolated func picker(_ picker: PHPickerViewController, didFinishPicking results: [PHPickerResult]) {
        Task { @MainActor in
            picker.dismiss(animated: true)

            guard let result = results.first else {
                continuation?.resume(throwing: CameraError.pickerCancelled)
                continuation = nil
                return
            }

            let itemProvider = result.itemProvider
            guard itemProvider.canLoadObject(ofClass: UIImage.self) else {
                continuation?.resume(throwing: CameraError.noImageData)
                continuation = nil
                return
            }

            do {
                let image = try await loadImage(from: itemProvider)
                guard let data = image.jpegData(compressionQuality: 0.8) else {
                    continuation?.resume(throwing: CameraError.noImageData)
                    continuation = nil
                    return
                }
                continuation?.resume(returning: data)
            } catch {
                continuation?.resume(throwing: error)
            }
            continuation = nil
        }
    }

    private func loadImage(from provider: NSItemProvider) async throws -> UIImage {
        try await withCheckedThrowingContinuation { continuation in
            provider.loadObject(ofClass: UIImage.self) { object, error in
                if let error {
                    continuation.resume(throwing: error)
                    return
                }
                guard let image = object as? UIImage else {
                    continuation.resume(throwing: CameraError.noImageData)
                    return
                }
                continuation.resume(returning: image)
            }
        }
    }
}
