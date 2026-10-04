import SwiftUI
import LucideIcons
#if canImport(UIKit)
import UIKit
#endif

struct VisualSearchView: View {
    let onSend: (Data, String?) -> Void

    @Environment(\.dismiss) private var dismiss
    @FocusState private var isPromptFocused: Bool
    @State private var capturedImageData: Data?
    @State private var prompt = ""

    var body: some View {
        ZStack {
            ShellTheme.Palette.visualSearchBackground
                .ignoresSafeArea()

            if !CameraManager.isCameraAvailable {
                cameraUnavailableFallback
            } else if let capturedImageData {
                previewStage(for: capturedImageData)
            } else {
                CameraCaptureView(dismissOnCapture: false) { data in
                    withAnimation(.spring(response: 0.32, dampingFraction: 0.86)) {
                        capturedImageData = data
                    }

                    Task { @MainActor in
                        isPromptFocused = true
                    }
                }
            }
        }
        .accessibilityIdentifier("aiScreenVisualSearch")
    }

    private var cameraUnavailableFallback: some View {
        VStack(spacing: ShellTheme.Spacing.large) {
            HStack {
                Label {
                    Text("Visual Search")
                } icon: {
                    Image(uiImage: Lucide.image.withRenderingMode(.alwaysTemplate))
                }
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(ShellTheme.Palette.visualSearchMutedForeground)

                Spacer()

                Button {
                    dismiss()
                } label: {
                    Image(uiImage: Lucide.circleX.withRenderingMode(.alwaysTemplate))
                        .font(.title3)
                        .foregroundStyle(ShellTheme.Palette.visualSearchForeground)
                        .frame(width: ShellTheme.Size.touchTarget, height: ShellTheme.Size.touchTarget)
                        .background(Circle().fill(ShellTheme.Palette.visualSearchSurface))
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Close visual search")
            }

            Spacer()

            VStack(spacing: ShellTheme.Spacing.medium) {
                Image(uiImage: Lucide.cameraOff.withRenderingMode(.alwaysTemplate))
                    .font(.system(size: 48))
                    .foregroundStyle(ShellTheme.Palette.visualSearchMutedForeground)

                Text("Camera not available on this device")
                    .font(.body.weight(.medium))
                    .foregroundStyle(ShellTheme.Palette.visualSearchForeground)
                    .multilineTextAlignment(.center)

                Text("Visual Search requires a camera. Try on a physical device.")
                    .font(.footnote)
                    .foregroundStyle(ShellTheme.Palette.visualSearchMutedForeground)
                    .multilineTextAlignment(.center)
            }

            Spacer()
        }
        .padding(.horizontal, ShellTheme.Spacing.page)
        .padding(.top, ShellTheme.Spacing.large)
        .accessibilityIdentifier("visualSearchCameraUnavailable")
    }

    private func previewStage(for imageData: Data) -> some View {
        VStack(spacing: ShellTheme.Spacing.large) {
            HStack(spacing: ShellTheme.Spacing.medium) {
                Label {
                    Text("Visual Search")
                } icon: {
                    Image(uiImage: Lucide.image.withRenderingMode(.alwaysTemplate))
                }
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(ShellTheme.Palette.visualSearchMutedForeground)

                Spacer(minLength: 0)

                Button {
                    dismiss()
                } label: {
                    Image(uiImage: Lucide.circleX.withRenderingMode(.alwaysTemplate))
                        .font(.title3)
                        .foregroundStyle(ShellTheme.Palette.visualSearchForeground)
                        .frame(width: ShellTheme.Size.touchTarget, height: ShellTheme.Size.touchTarget)
                        .background(
                            Circle()
                                .fill(ShellTheme.Palette.visualSearchSurface)
                        )
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Close visual search")
            }

            Spacer(minLength: 0)

            previewImage(imageData)

            VStack(alignment: .leading, spacing: ShellTheme.Spacing.small) {
                Text("Send this capture to the page-aware chat with an optional prompt.")
                    .font(.footnote)
                    .foregroundStyle(ShellTheme.Palette.visualSearchMutedForeground)

                TextField("What should AI focus on?", text: $prompt, axis: .vertical)
                    .textFieldStyle(.plain)
                    .lineLimit(1...4)
                    .padding(.horizontal, ShellTheme.Spacing.large)
                    .padding(.vertical, ShellTheme.Spacing.medium)
                    .background(
                        RoundedRectangle(cornerRadius: ShellTheme.Radius.searchField, style: .continuous)
                            .fill(ShellTheme.Palette.visualSearchSurface)
                    )
                    .overlay(
                        RoundedRectangle(cornerRadius: ShellTheme.Radius.searchField, style: .continuous)
                            .strokeBorder(ShellTheme.Palette.visualSearchBorder, lineWidth: ShellTheme.Stroke.hairline)
                    )
                    .foregroundStyle(ShellTheme.Palette.visualSearchForeground)
                    .focused($isPromptFocused)
            }

            HStack(spacing: ShellTheme.Spacing.medium) {
                Button {
                    retake()
                } label: {
                    Label {
                        Text("Retake")
                    } icon: {
                        Image(uiImage: Lucide.camera.withRenderingMode(.alwaysTemplate))
                    }
                    .font(.subheadline.weight(.semibold))
                    .frame(maxWidth: .infinity)
                    .padding(.horizontal, ShellTheme.Spacing.large)
                    .padding(.vertical, ShellTheme.Spacing.medium)
                    .background(
                        RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous)
                            .fill(ShellTheme.Palette.visualSearchSurfaceStrong)
                    )
                    .overlay(
                        RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous)
                            .strokeBorder(ShellTheme.Palette.visualSearchBorder, lineWidth: ShellTheme.Stroke.hairline)
                    )
                }
                .buttonStyle(.plain)
                .foregroundStyle(ShellTheme.Palette.visualSearchForeground)

                Button {
                    send(imageData)
                } label: {
                    HStack(spacing: ShellTheme.Spacing.small) {
                        Image(uiImage: Lucide.sparkles.withRenderingMode(.alwaysTemplate))
                        Text("Send to AI")
                    }
                    .font(.subheadline.weight(.semibold))
                    .frame(maxWidth: .infinity)
                    .padding(.horizontal, ShellTheme.Spacing.large)
                    .padding(.vertical, ShellTheme.Spacing.medium)
                    .background(
                        RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous)
                            .fill(ShellTheme.Palette.summaryAccent)
                    )
                }
                .buttonStyle(.plain)
                .foregroundStyle(ShellTheme.Palette.visualSearchForeground)
            }
        }
        .frame(maxWidth: ShellTheme.Size.visualSearchPreviewMaxWidth)
        .padding(.horizontal, ShellTheme.Spacing.page)
        .padding(.top, ShellTheme.Spacing.large)
        .padding(.bottom, ShellTheme.Spacing.page)
    }

    @ViewBuilder
    private func previewImage(_ imageData: Data) -> some View {
#if canImport(UIKit)
        if let uiImage = UIImage(data: imageData) {
            Image(uiImage: uiImage)
                .resizable()
                .aspectRatio(contentMode: .fit)
                .frame(maxWidth: ShellTheme.Size.visualSearchPreviewMaxWidth)
                .clipShape(RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous))
                .overlay(
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                        .strokeBorder(ShellTheme.Palette.visualSearchBorder, lineWidth: ShellTheme.Stroke.hairline)
                )
        } else {
            previewPlaceholder
        }
#else
        previewPlaceholder
#endif
    }

    private var previewPlaceholder: some View {
        RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
            .fill(ShellTheme.Palette.visualSearchSurface)
            .frame(maxWidth: .infinity)
            .aspectRatio(4.0 / 3.0, contentMode: .fit)
            .overlay {
                VStack(spacing: ShellTheme.Spacing.small) {
                    Image(uiImage: Lucide.image.withRenderingMode(.alwaysTemplate))
                        .font(.title2)
                        .foregroundStyle(ShellTheme.Palette.visualSearchMutedForeground)

                    Text("Preview unavailable")
                        .font(.footnote.weight(.semibold))
                        .foregroundStyle(ShellTheme.Palette.visualSearchMutedForeground)
                }
            }
            .overlay(
                RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                    .strokeBorder(ShellTheme.Palette.visualSearchBorder, lineWidth: ShellTheme.Stroke.hairline)
            )
    }

    private func retake() {
        withAnimation(.spring(response: 0.3, dampingFraction: 0.86)) {
            capturedImageData = nil
        }
        prompt = ""
    }

    private func send(_ imageData: Data) {
        onSend(imageData, normalizedPrompt)
        dismiss()
    }

    private var normalizedPrompt: String? {
        let trimmed = prompt.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? nil : trimmed
    }
}
