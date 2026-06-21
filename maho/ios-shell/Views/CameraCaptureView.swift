import SwiftUI
import AVFoundation
import LucideIcons

struct CameraCaptureView: View {
    let onCapture: (Data) -> Void
    let dismissOnCapture: Bool

    @State private var camera = CameraManager()
    @Environment(\.dismiss) private var dismiss

    init(
        dismissOnCapture: Bool = true,
        onCapture: @escaping (Data) -> Void
    ) {
        self.onCapture = onCapture
        self.dismissOnCapture = dismissOnCapture
    }

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            if !CameraManager.isCameraAvailable {
                cameraUnavailableView
            } else {
                switch camera.state {
                case .error(let message):
                    errorView(message)
                default:
                    cameraContent
                }
            }
        }
        .task {
            guard CameraManager.isCameraAvailable else { return }
            await camera.prepareSession()
        }
        .accessibilityIdentifier("aiScreenCameraCapture")
        .onDisappear {
            camera.tearDown()
        }
    }

    private var cameraUnavailableView: some View {
        VStack(spacing: 20) {
            Image(uiImage: Lucide.cameraOff.withRenderingMode(.alwaysTemplate))
                .font(.system(size: 48))
                .foregroundStyle(.white.opacity(0.6))

            Text("Camera not available on this device")
                .font(.body)
                .foregroundStyle(.white.opacity(0.8))
                .multilineTextAlignment(.center)
                .padding(.horizontal, 32)

            Button("Dismiss") {
                dismiss()
            }
            .buttonStyle(.borderedProminent)
        }
        .accessibilityIdentifier("cameraUnavailableFallback")
    }

    // MARK: - Camera Content

    @ViewBuilder
    private var cameraContent: some View {
        VStack(spacing: 0) {
            // Preview
            if let session = camera.session {
                CameraPreviewView(session: session)
                    .ignoresSafeArea(edges: .top)
            } else {
                Spacer()
                ProgressView()
                    .tint(.white)
                Spacer()
            }

            // Controls
            controlBar
        }
    }

    // MARK: - Control Bar

    private var controlBar: some View {
        HStack(spacing: 0) {
            // Cancel
            Button {
                dismiss()
            } label: {
                Image(uiImage: Lucide.x.withRenderingMode(.alwaysTemplate))
                    .font(.title2)
                    .foregroundStyle(.white)
                    .frame(width: 56, height: 56)
            }
            .accessibilityLabel("Cancel")

            Spacer()

            // Capture
            Button {
                performCapture()
            } label: {
                ZStack {
                    Circle()
                        .stroke(.white, lineWidth: 4)
                        .frame(width: 72, height: 72)
                    Circle()
                        .fill(.white)
                        .frame(width: 60, height: 60)
                        .scaleEffect(camera.state == .capturing ? 0.85 : 1.0)
                        .animation(.easeInOut(duration: 0.1), value: camera.state == .capturing)
                }
            }
            .disabled(camera.state == .capturing || camera.state == .preparing)
            .accessibilityLabel("Take photo")

            Spacer()

            // Library picker
            Button {
                performLibraryPick()
            } label: {
                Image(uiImage: Lucide.image.withRenderingMode(.alwaysTemplate))
                    .font(.title2)
                    .foregroundStyle(.white)
                    .frame(width: 56, height: 56)
            }
            .accessibilityLabel("Choose from library")
        }
        .padding(.horizontal, 24)
        .padding(.vertical, 16)
        .background(.black.opacity(0.9))
    }

    // MARK: - Error View

    private func errorView(_ message: String) -> some View {
        VStack(spacing: 20) {
            Image(uiImage: Lucide.cameraOff.withRenderingMode(.alwaysTemplate))
                .font(.system(size: 48))
                .foregroundStyle(.white.opacity(0.6))

            Text(message)
                .font(.body)
                .foregroundStyle(.white.opacity(0.8))
                .multilineTextAlignment(.center)
                .padding(.horizontal, 32)

            if message.contains("Settings") {
                Button("Open Settings") {
                    openSettings()
                }
                .buttonStyle(.borderedProminent)
            }

            Button("Dismiss") {
                dismiss()
            }
            .foregroundStyle(.white.opacity(0.7))
            .padding(.top, 8)
        }
    }

    // MARK: - Actions

    private func performCapture() {
        Task {
            do {
                let data = try await camera.capturePhoto()
                onCapture(data)
                if dismissOnCapture {
                    dismiss()
                }
            } catch {
                camera.state = .error(error.localizedDescription)
            }
        }
    }

    private func performLibraryPick() {
        Task {
            do {
                let data = try await camera.pickFromLibrary()
                camera.state = .captured(data)
                onCapture(data)
                if dismissOnCapture {
                    dismiss()
                }
            } catch CameraError.pickerCancelled {
                // User cancelled — do nothing
            } catch {
                camera.state = .error(error.localizedDescription)
            }
        }
    }

    private func openSettings() {
        guard let url = URL(string: UIApplication.openSettingsURLString) else { return }
        UIApplication.shared.open(url)
    }
}

// MARK: - Camera Preview (UIViewRepresentable)

struct CameraPreviewView: UIViewRepresentable {
    let session: AVCaptureSession

    func makeUIView(context: Context) -> CameraPreviewUIView {
        let view = CameraPreviewUIView()
        view.previewLayer.session = session
        view.previewLayer.videoGravity = .resizeAspectFill
        return view
    }

    func updateUIView(_ uiView: CameraPreviewUIView, context: Context) {
        uiView.previewLayer.session = session
    }
}

final class CameraPreviewUIView: UIView {
    override class var layerClass: AnyClass {
        AVCaptureVideoPreviewLayer.self
    }

    var previewLayer: AVCaptureVideoPreviewLayer {
        layer as! AVCaptureVideoPreviewLayer
    }
}
