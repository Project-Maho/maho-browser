import SwiftUI
import AVFoundation
import LucideIcons

struct PrivacyDashboardView: View {
    @AppStorage("send_conversation_history") private var sendConversationHistory: Bool = true
    @AppStorage("allow_telemetry") private var allowTelemetry: Bool = false
    
    @State private var isClearingConversations = false
    @State private var statusMessage: String? = nil
    
    @State private var cameraAccessGranted = false
    @State private var microphoneAccessGranted = false
    
    private let bridge = MahoBridge.shared
    
    var body: some View {
        Form {
            Section("AI Privacy") {
                Toggle(isOn: $sendConversationHistory) {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Send conversation history to providers")
                        Text("Keep provider prompts context-aware across the current chat session.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
                
                Toggle(isOn: $allowTelemetry) {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Allow telemetry")
                        Text("Share aggregate AI feature health signals from this device.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
            
            Section("Data") {
                Button(role: isClearingConversations ? nil : .destructive) {
                    clearAllConversations()
                } label: {
                    HStack {
                        Text("Clear all conversations")
                        if isClearingConversations {
                            Spacer()
                            ProgressView()
                        }
                    }
                }
                .disabled(isClearingConversations)
                
                Button("Clear cached AI responses") {
                    statusMessage = "AI response cache clearing is not available in this build yet."
                }
            }
            
            if let status = statusMessage {
                Section("Status") {
                    Text(status)
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
            }
            
            Section("Permissions") {
                HStack {
                    Image(lucide: Lucide.camera)
                        .foregroundStyle(cameraAccessGranted ? ShellTheme.Palette.accent : .secondary)
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Camera access")
                        Text(cameraAccessGranted ? "Granted on this device" : "Not granted")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
                
                HStack {
                    Image(lucide: Lucide.mic)
                        .foregroundStyle(microphoneAccessGranted ? ShellTheme.Palette.accent : .secondary)
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Microphone access")
                        Text(microphoneAccessGranted ? "Granted on this device" : "Not granted")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
        }
        .navigationTitle("AI Privacy Dashboard")
        .navigationBarTitleDisplayMode(.inline)
        .onAppear(perform: checkPermissions)
    }
    
    private func clearAllConversations() {
        isClearingConversations = true
        statusMessage = nil
        
        DispatchQueue.global(qos: .userInitiated).async {
            let conversations = bridge.listConversations(limit: 1000)
            var allCleared = true
            
            for session in conversations {
                if !bridge.deleteConversation(sessionId: session.id) {
                    allCleared = false
                }
            }
            
            DispatchQueue.main.async {
                isClearingConversations = false
                if !allCleared {
                    statusMessage = "Unable to clear every saved conversation right now."
                } else if conversations.isEmpty {
                    statusMessage = "No saved conversations to clear."
                } else {
                    statusMessage = "Cleared \(conversations.count) saved conversation(s)."
                }
            }
        }
    }
    
    private func checkPermissions() {
        cameraAccessGranted = AVCaptureDevice.authorizationStatus(for: .video) == .authorized
        microphoneAccessGranted = AVCaptureDevice.authorizationStatus(for: .audio) == .authorized
    }
}
