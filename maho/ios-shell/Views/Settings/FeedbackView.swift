import SwiftUI

struct FeedbackView: View {
    @State private var email: String = ""
    @State private var message: String = ""
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
    }

    private func sendFeedback() {
        let trimmedMessage = message.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedMessage.isEmpty else { return }

        var body = "Feedback from Maho iOS\n\n"
        if !email.isEmpty {
            body += "Email: \(email)\n\n"
        }
        body += "Message:\n\(trimmedMessage)"

        guard let encodedBody = body.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed),
              let url = URL(string: "mailto:feedback@maho.dev?subject=Maho%20Feedback&body=\(encodedBody)") else {
            return
        }

        UIApplication.shared.open(url)
    }
}
