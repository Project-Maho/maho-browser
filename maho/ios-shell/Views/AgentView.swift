import SwiftUI

@MainActor
struct AgentView: View {
    @State private var goal = ""
    @State private var startURL = ""
    @State private var resultMessage: String?
    @State private var isRunning = false
    @State private var showNotImplementedAlert = false

    var body: some View {
        Form {
            Section {
                Text("Enter a web task for the agent to perform autonomously.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            } header: {
                Text("Web Agent")
            }

            Section {
                TextField("Describe the task", text: $goal, axis: .vertical)
                    .lineLimit(2...5)

                TextField("Start URL (optional)", text: $startURL)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
            } header: {
                Text("Task")
            }

            Section {
                Button {
                    runAgent()
                } label: {
                    HStack {
                        Label("Run Agent", systemImage: "play.fill")
                        if isRunning {
                            Spacer()
                            ProgressView()
                        }
                    }
                }
                .disabled(trimmedGoal.isEmpty || isRunning)
            }

            if let resultMessage {
                Section {
                    Text(resultMessage)
                        .foregroundStyle(.primary)
                        .fixedSize(horizontal: false, vertical: true)
                } header: {
                    Text("Result")
                }
            }
        }
        .navigationTitle("Web Agent")
        .navigationBarTitleDisplayMode(.inline)
        .accessibilityIdentifier("aiScreenAgent")
        .alert("Not Yet Available", isPresented: $showNotImplementedAlert) {
            Button("OK", role: .cancel) {}
        } message: {
            Text("Web Agent is coming soon to iOS. Currently in beta on Android only.")
        }
    }

    private var trimmedGoal: String {
        goal.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private func runAgent() {
        isRunning = true
        resultMessage = nil

        Task {
            try? await Task.sleep(for: .milliseconds(600))
            isRunning = false
            showNotImplementedAlert = true
            resultMessage = "Web Agent is not yet available on iOS. This feature is currently in beta on Android only."
        }
    }
}
