import SwiftUI

struct FeedbackView: View {
    private static let feedbackAddress = "feedback@maho.dev"

    @State private var email: String = ""
    @State private var message: String = ""
    @State private var showMailUnavailable = false
    @State private var unsentFeedback = ""
    @Environment(\.dismiss) private var dismiss

    private var canSend: Bool {
        !message.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    var body: some View {
        Form {
            Section {
                TextField("Email (optional)", text: $email)
                    .keyboardType(.emailAddress)
                    .textContentType(.emailAddress)
                    .textInputAutocapitalization(.never)
            } header: {
                Text("Contact Info")
            }

            Section {
                TextEditor(text: $message)
                    .frame(minHeight: 120)
            } header: {
                Text("Message")
            }

            Section {
                Button {
                    sendFeedback()
                } label: {
                    HStack {
                        Spacer()
                        Text("Send")
                            .font(.headline)
                        Spacer()
                    }
                }
                .disabled(!canSend)
            }
        }
        .navigationTitle("Feedback")
        .alert("Couldn't Open Mail", isPresented: $showMailUnavailable) {
            Button("Copy Feedback") {
                UIPasteboard.general.string = unsentFeedback
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("No mail app is set up on this device. Copy your feedback and send it to \(Self.feedbackAddress).")
        }
    }

    private func sendFeedback() {
        let trimmedMessage = message.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedMessage.isEmpty else { return }

        var body = "Feedback from Maho iOS\n\n"
        if !email.isEmpty {
            body += "Email: \(email)\n\n"
        }
        body += "Message:\n\(trimmedMessage)"

        var components = URLComponents()
        components.scheme = "mailto"
        components.path = Self.feedbackAddress
        components.queryItems = [
            URLQueryItem(name: "subject", value: "Maho Feedback"),
            URLQueryItem(name: "body", value: body)
        ]

        guard let url = components.url else {
            presentMailUnavailable(body)
            return
        }

        UIApplication.shared.open(url) { success in
            if success {
                message = ""
                dismiss()
            } else {
                presentMailUnavailable(body)
            }
        }
    }

    private func presentMailUnavailable(_ body: String) {
        unsentFeedback = "To: \(Self.feedbackAddress)\n\n\(body)"
        showMailUnavailable = true
    }
}
