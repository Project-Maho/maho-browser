import SwiftUI
import LucideIcons

struct VoiceSearchButton: View {
    @Bindable var manager: VoiceSearchManager
    let onResult: (String) -> Void

    @State private var pulseScale: CGFloat = 1.0

    var body: some View {
        Button {
            handleTap()
        } label: {
            ZStack {
                if manager.state == .listening {
                    Circle()
                        .fill(Color.blue.opacity(0.15))
                        .scaleEffect(pulseScale)
                        .frame(width: 44, height: 44)
                }

                Image(lucide: voiceIcon)
                    .font(.title3)
                    .foregroundStyle(iconColor)
                    .frame(width: 32, height: 32)
            }
        }
        .buttonStyle(.plain)
        .accessibilityLabel(accessibilityText)
        .onChange(of: manager.state) { _, newState in
            handleStateChange(newState)
        }
    }

    private var voiceIcon: UIImage {
        switch manager.state {
        case .listening:
            Lucide.mic
        case .processing:
            Lucide.ellipsis
        case .error:
            Lucide.micOff
        default:
            Lucide.mic
        }
    }

    private var iconColor: Color {
        switch manager.state {
        case .listening:
            .blue
        case .error:
            .red
        default:
            .secondary
        }
    }

    private var accessibilityText: String {
        switch manager.state {
        case .listening:
            "Stop voice search"
        case .processing:
            "Processing speech"
        default:
            "Voice search"
        }
    }

    private func handleTap() {
        switch manager.state {
        case .listening:
            manager.stopListening()
        case .idle, .error, .result:
            manager.reset()
            manager.startListening()
        case .processing:
            break
        }
    }

    private func handleStateChange(_ newState: VoiceSearchState) {
        switch newState {
        case .listening:
            withAnimation(.easeInOut(duration: 0.8).repeatForever(autoreverses: true)) {
                pulseScale = 1.5
            }
        case .result(let text):
            pulseScale = 1.0
            onResult(text)
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                manager.reset()
            }
        default:
            withAnimation(.easeOut(duration: 0.2)) {
                pulseScale = 1.0
            }
        }
    }
}
