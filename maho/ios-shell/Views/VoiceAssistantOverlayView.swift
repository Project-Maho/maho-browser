import SwiftUI
import LucideIcons
#if canImport(UIKit)
import UIKit
#endif

struct VoiceAssistantOverlayView: View {
    let voiceManager: VoiceSearchManager
    let startsListeningOnAppear: Bool
    let onResult: (String) -> Void
    let onDismiss: () -> Void

    init(
        voiceManager: VoiceSearchManager,
        startsListeningOnAppear: Bool = true,
        onResult: @escaping (String) -> Void,
        onDismiss: @escaping () -> Void
    ) {
        self.voiceManager = voiceManager
        self.startsListeningOnAppear = startsListeningOnAppear
        self.onResult = onResult
        self.onDismiss = onDismiss
    }

    var body: some View {
        GeometryReader { geometry in
            ZStack {
                backgroundScrim
                    .contentShape(Rectangle())
                    .onTapGesture(perform: dismissOverlay)

                content(in: geometry)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .ignoresSafeArea()
        .accessibilityIdentifier("voiceAssistantOverlay")
        .onAppear(perform: handleAppear)
        .onChange(of: voiceManager.state) { _, newState in
            handleVoiceStateChange(newState)
        }
    }

    private var backgroundScrim: some View {
        ZStack {
            VoiceAssistantAuroraBackground()

            ShellTheme.Palette.voiceAssistantScrimDim
        }
        .ignoresSafeArea()
    }

    private func content(in geometry: GeometryProxy) -> some View {
        VStack(spacing: 0) {
            topBar(topInset: topSafeAreaInset)

            Spacer(minLength: ShellTheme.Spacing.hero)

            promptCluster

            Spacer(minLength: ShellTheme.Spacing.hero)

            VoiceAssistantWaveformPill(
                audioLevel: voiceManager.audioLevel,
                isListening: voiceManager.state == .listening
            )
            .padding(.bottom, geometry.safeAreaInsets.bottom + ShellTheme.Spacing.page)
        }
        .padding(.horizontal, ShellTheme.Spacing.page)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var topSafeAreaInset: CGFloat {
#if canImport(UIKit)
        let scenes = UIApplication.shared.connectedScenes.compactMap { scene in
            scene as? UIWindowScene
        }
        let foregroundScene = scenes.first { scene in
            scene.activationState == .foregroundActive
        } ?? scenes.first
        let foregroundWindow = foregroundScene?.windows.first { window in
            window.isKeyWindow
        } ?? foregroundScene?.windows.first
        return foregroundWindow?.safeAreaInsets.top ?? 0
#else
        return 0
#endif
    }

    private func topBar(topInset: CGFloat) -> some View {
        HStack {
            Spacer(minLength: 0)

            Button(action: dismissOverlay) {
                Image(lucide: Lucide.circleX)
                    .font(.title3.weight(.medium))
                    .foregroundStyle(ShellTheme.Palette.voiceAssistantFace)
                    .frame(width: ShellTheme.Size.touchTarget, height: ShellTheme.Size.touchTarget)
                    .background(
                        Circle()
                            .fill(ShellTheme.Palette.voiceAssistantControlFill)
                    )
                    .overlay(
                        Circle()
                            .strokeBorder(ShellTheme.Palette.voiceAssistantWaveformBorder, lineWidth: ShellTheme.Stroke.hairline)
                    )
            }
            .buttonStyle(.plain)
            .accessibilityIdentifier("voiceAssistantCloseButton")
            .accessibilityLabel("Close voice assistant")
        }
        .padding(.top, topInset + ShellTheme.Spacing.medium)
    }

    private var headline: some View {
        Text(headlineText)
            .font(.largeTitle.weight(.semibold))
            .multilineTextAlignment(.center)
            .lineLimit(3)
            .minimumScaleFactor(0.74)
            .foregroundStyle(headlineGradient)
            .frame(maxWidth: ShellTheme.Size.voiceAssistantHeadlineMaxWidth)
            .shadow(
                color: ShellTheme.Palette.voiceAssistantTextShadow,
                radius: ShellTheme.Spacing.xSmall,
                x: 0,
                y: ShellTheme.Spacing.badgeVertical
            )
            .id(headlineText)
            .transition(.opacity.combined(with: .scale(scale: 0.97)))
            .animation(.spring(response: 0.32, dampingFraction: 0.86), value: headlineText)
            .accessibilityIdentifier("voiceAssistantHeadline")
    }

    private var promptCluster: some View {
        VStack(spacing: 0) {
            headline

            if let errorMessage {
                errorPanel(message: errorMessage)
                    .padding(.top, ShellTheme.Spacing.large)
                    .transition(.opacity.combined(with: .scale(scale: 0.96)))
            } else {
                statusText
                    .padding(.top, ShellTheme.Spacing.medium)
                    .transition(.opacity)
            }
        }
        .padding(.horizontal, ShellTheme.Spacing.medium)
        .padding(.vertical, ShellTheme.Spacing.large)
        .background(promptLegibilityHalo)
    }

    private var promptLegibilityHalo: some View {
        Ellipse()
            .fill(
                RadialGradient(
                    colors: [
                        ShellTheme.Palette.voiceAssistantPromptHaloCore,
                        ShellTheme.Palette.voiceAssistantPromptHaloMid,
                        ShellTheme.Palette.voiceAssistantPromptHaloClear
                    ],
                    center: .center,
                    startRadius: 0,
                    endRadius: ShellTheme.Size.voiceAssistantPromptHaloRadius
                )
            )
            .frame(
                width: ShellTheme.Size.voiceAssistantPromptHaloWidth,
                height: ShellTheme.Size.voiceAssistantPromptHaloHeight
            )
            .blur(radius: ShellTheme.Spacing.medium)
            .allowsHitTesting(false)
            .accessibilityHidden(true)
    }

    private var statusText: some View {
        Text(statusMessage)
            .font(.footnote.weight(.medium))
            .foregroundStyle(ShellTheme.Palette.voiceAssistantSecondaryText)
            .multilineTextAlignment(.center)
            .shadow(
                color: ShellTheme.Palette.voiceAssistantTextShadow,
                radius: ShellTheme.Spacing.xSmall,
                x: 0,
                y: ShellTheme.Spacing.badgeVertical
            )
            .accessibilityIdentifier("voiceAssistantStatus")
    }

    private func errorPanel(message: String) -> some View {
        VStack(spacing: ShellTheme.Spacing.medium) {
            Text(message)
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(ShellTheme.Palette.voiceAssistantFace)
                .multilineTextAlignment(.center)

            Button(action: retryListening) {
                Text("Try again")
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(ShellTheme.Palette.voiceAssistantFace)
                    .padding(.horizontal, ShellTheme.Spacing.large)
                    .padding(.vertical, ShellTheme.Spacing.medium)
                    .background(
                        Capsule()
                            .fill(ShellTheme.Palette.voiceAssistantControlFill)
                    )
                    .overlay(
                        Capsule()
                            .strokeBorder(ShellTheme.Palette.voiceAssistantWaveformBorder, lineWidth: ShellTheme.Stroke.hairline)
                    )
            }
            .buttonStyle(.plain)
            .accessibilityIdentifier("voiceAssistantRetryButton")
        }
        .padding(ShellTheme.Spacing.large)
        .frame(maxWidth: ShellTheme.Size.voiceAssistantHeadlineMaxWidth)
        .background(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                .fill(ShellTheme.Palette.voiceAssistantErrorFill)
                .background(ShellTheme.Materials.voiceAssistantPill, in: RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous))
        )
        .overlay(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                .strokeBorder(ShellTheme.Palette.voiceAssistantErrorBorder, lineWidth: ShellTheme.Stroke.hairline)
        )
        .accessibilityIdentifier("voiceAssistantError")
    }

    private var headlineGradient: LinearGradient {
        LinearGradient(
            colors: [
                ShellTheme.Palette.voiceAssistantHeadlineStart,
                ShellTheme.Palette.voiceAssistantHeadlineEnd
            ],
            startPoint: .leading,
            endPoint: .trailing
        )
    }

    private var headlineText: String {
        switch voiceManager.state {
        case .listening:
            let transcript = voiceManager.transcript.trimmingCharacters(in: .whitespacesAndNewlines)
            return transcript.isEmpty ? "Hi! How can I help?" : transcript
        case .processing:
            return "One moment…"
        default:
            return "Hi! How can I help?"
        }
    }

    private var statusMessage: String {
        switch voiceManager.state {
        case .listening:
            return voiceManager.transcript.isEmpty ? "Listening…" : "Keep talking — I’m listening."
        case .processing:
            return "Turning your voice into a search."
        default:
            return "Tap anywhere outside to close."
        }
    }

    private var errorMessage: String? {
        guard case .error(let message) = voiceManager.state else { return nil }
        return message
    }

    private func handleAppear() {
        guard startsListeningOnAppear else { return }
        voiceManager.reset()
        voiceManager.startListening()
    }

    private func handleVoiceStateChange(_ newState: VoiceSearchState) {
        guard case .result(let text) = newState else { return }
        onResult(text)
    }

    private func retryListening() {
        voiceManager.reset()
        voiceManager.startListening()
    }

    private func dismissOverlay() {
        voiceManager.stopListening()
        onDismiss()
    }
}

private struct VoiceAssistantAuroraBackground: View {
    private static let cycleDuration: TimeInterval = 9.5
    private static let primaryBlobScale: CGFloat = 1.24
    private static let secondaryBlobScale: CGFloat = 1.12

    var body: some View {
        GeometryReader { geometry in
            TimelineView(.animation) { timeline in
                let animationPhase = phase(for: timeline.date)
                let maxSpan = max(geometry.size.width, geometry.size.height)

                ZStack {
                    LinearGradient(
                        colors: [
                            ShellTheme.Palette.voiceAssistantGlowPurple.opacity(1),
                            ShellTheme.Palette.voiceAssistantGlowBlue.opacity(0.96),
                            ShellTheme.Palette.voiceAssistantGlowMagenta.opacity(0.92),
                            ShellTheme.Palette.voiceAssistantGlowPurple.opacity(0.98)
                        ],
                        startPoint: driftingPoint(
                            x: 0.1,
                            y: 0.02,
                            radiusX: 0.42,
                            radiusY: 0.34,
                            phase: animationPhase,
                            offset: 0
                        ),
                        endPoint: driftingPoint(
                            x: 0.92,
                            y: 0.98,
                            radiusX: 0.36,
                            radiusY: 0.42,
                            phase: animationPhase,
                            offset: 0.5
                        )
                    )

                    RadialGradient(
                        colors: [
                            ShellTheme.Palette.voiceAssistantGlowBlue.opacity(0.86),
                            ShellTheme.Palette.voiceAssistantGlowBlue.opacity(0)
                        ],
                        center: driftingPoint(
                            x: 0.22,
                            y: 0.28,
                            radiusX: 0.42,
                            radiusY: 0.4,
                            phase: animationPhase,
                            offset: 0.16
                        ),
                        startRadius: 0,
                        endRadius: maxSpan * 0.72
                    )
                    .scaleEffect(Self.primaryBlobScale)

                    RadialGradient(
                        colors: [
                            ShellTheme.Palette.voiceAssistantGlowPurple.opacity(0.82),
                            ShellTheme.Palette.voiceAssistantGlowPurple.opacity(0)
                        ],
                        center: driftingPoint(
                            x: 0.72,
                            y: 0.22,
                            radiusX: 0.38,
                            radiusY: 0.34,
                            phase: animationPhase,
                            offset: 0.48
                        ),
                        startRadius: 0,
                        endRadius: maxSpan * 0.66
                    )
                    .scaleEffect(Self.secondaryBlobScale)

                    RadialGradient(
                        colors: [
                            ShellTheme.Palette.voiceAssistantGlowMagenta.opacity(0.76),
                            ShellTheme.Palette.voiceAssistantGlowMagenta.opacity(0)
                        ],
                        center: driftingPoint(
                            x: 0.58,
                            y: 0.76,
                            radiusX: 0.5,
                            radiusY: 0.32,
                            phase: animationPhase,
                            offset: 0.78
                        ),
                        startRadius: 0,
                        endRadius: maxSpan * 0.7
                    )
                    .scaleEffect(Self.primaryBlobScale)
                }
                .blur(radius: ShellTheme.Spacing.hero)
            }
        }
        .allowsHitTesting(false)
        .accessibilityHidden(true)
    }

    private func phase(for date: Date) -> Double {
        date.timeIntervalSinceReferenceDate
            .truncatingRemainder(dividingBy: Self.cycleDuration) / Self.cycleDuration
    }

    private func driftingPoint(
        x: Double,
        y: Double,
        radiusX: Double,
        radiusY: Double,
        phase: Double,
        offset: Double
    ) -> UnitPoint {
        let angle = (phase + offset) * Double.pi * 2
        let driftX = x + cos(angle) * radiusX
        let driftY = y + sin(angle) * radiusY
        return UnitPoint(x: CGFloat(driftX), y: CGFloat(driftY))
    }
}

private struct VoiceAssistantWaveformPill: View {
    let audioLevel: Float
    let isListening: Bool

    private static let barWeights: [CGFloat] = [
        1.0, 0.92, 0.98, 0.82, 0.72, 0.78, 0.62,
        0.56, 0.48, 0.52, 0.38, 0.32, 0.28, 0.24
    ]
    private static let ambientCycleDuration: TimeInterval = 1.8
    private static let ambientPhaseStride: Double = 0.62
    private static let ambientLevelFloor: CGFloat = 0.07
    private static let ambientLevelRange: CGFloat = 0.18

    var body: some View {
        Group {
            if isListening {
                TimelineView(.animation) { timeline in
                    waveformItems(ambientPhase: ambientPhase(for: timeline.date))
                }
            } else {
                waveformItems(ambientPhase: nil)
            }
        }
        .frame(maxWidth: ShellTheme.Size.voiceAssistantWaveformMaxWidth)
        .frame(height: ShellTheme.Size.voiceAssistantWaveformHeight)
        .padding(.horizontal, ShellTheme.Spacing.large)
        .background(
            Capsule()
                .fill(ShellTheme.Palette.voiceAssistantWaveformFill)
                .background(ShellTheme.Materials.voiceAssistantPill, in: Capsule())
        )
        .overlay(
            Capsule()
                .strokeBorder(ShellTheme.Palette.voiceAssistantWaveformBorder, lineWidth: ShellTheme.Stroke.hairline)
        )
        .animation(.spring(response: 0.22, dampingFraction: 0.74), value: audioLevel)
        .animation(.spring(response: 0.24, dampingFraction: 0.86), value: isListening)
        .accessibilityIdentifier("voiceAssistantWaveform")
    }

    private func waveformItems(ambientPhase: Double?) -> some View {
        HStack(alignment: .center, spacing: ShellTheme.Spacing.xSmall) {
            ForEach(Self.barWeights.indices, id: \.self) { index in
                waveformItem(at: index, ambientPhase: ambientPhase)
            }
        }
    }

    @ViewBuilder
    private func waveformItem(at index: Int, ambientPhase: Double?) -> some View {
        if let ambientPhase {
            Capsule()
                .fill(ShellTheme.Palette.voiceAssistantWaveformActive)
                .frame(
                    width: ShellTheme.Size.voiceAssistantWaveformBarWidth,
                    height: barHeight(at: index, ambientPhase: ambientPhase)
                )
        } else {
            Circle()
                .fill(ShellTheme.Palette.voiceAssistantWaveformIdle)
                .frame(width: ShellTheme.Size.voiceAssistantWaveformDot, height: ShellTheme.Size.voiceAssistantWaveformDot)
        }
    }

    private func ambientPhase(for date: Date) -> Double {
        date.timeIntervalSinceReferenceDate
            .truncatingRemainder(dividingBy: Self.ambientCycleDuration) / Self.ambientCycleDuration * Double.pi * 2
    }

    private func barHeight(at index: Int, ambientPhase: Double) -> CGFloat {
        let clampedLevel = CGFloat(min(max(audioLevel, 0), 1))
        let minHeight = ShellTheme.Size.voiceAssistantWaveformMinBarHeight
        let maxHeight = ShellTheme.Size.voiceAssistantWaveformMaxBarHeight
        let dynamicRange = maxHeight - minHeight
        let weight = Self.barWeights[index]
        let audioReactiveHeight = dynamicRange * max(clampedLevel, 0.08) * weight
        let ambientHeight = dynamicRange * ambientLevel(at: index, phase: ambientPhase) * weight
        return min(maxHeight, minHeight + audioReactiveHeight + ambientHeight)
    }

    private func ambientLevel(at index: Int, phase: Double) -> CGFloat {
        let shiftedPhase = phase + Double(index) * Self.ambientPhaseStride
        let wave = (sin(shiftedPhase) + 1) / 2
        return Self.ambientLevelFloor + CGFloat(wave) * Self.ambientLevelRange
    }
}
