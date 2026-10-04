import SwiftUI

@MainActor
final class AgentViewModel: ObservableObject {
    enum TaskState: Equatable {
        case idle
        case working
        case completed
        case failed
        case cancelled
    }

    @Published var goal = ""
    @Published var taskState: TaskState = .idle
    @Published var resultMessage: String?
    @Published var eventLog: [String] = []

    private let bridge: MahoBridge
    private var sessionPtr: OpaquePointer?
    private var activeSessionId: String?
    private var pollTask: Task<Void, Never>?

    init(bridge: MahoBridge = .shared) {
        self.bridge = bridge
    }

    var canSubmit: Bool {
        let trimmed = goal.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty {
            return false
        }
        switch taskState {
        case .idle, .completed, .failed, .cancelled:
            return true
        case .working:
            return false
        }
    }

    var canCancel: Bool {
        taskState == .working
    }

    func runAgent() {
        eventLog.removeAll()
        resultMessage = nil
        taskState = .working

        let sessionId = UUID().uuidString
        guard let ptr = bridge.agentCreateSession(sessionId: sessionId) else {
            taskState = .failed
            eventLog.append("[error] Failed to create local agent session")
            return
        }

        sessionPtr = ptr
        activeSessionId = sessionId

        let trimmedGoal = goal.trimmingCharacters(in: .whitespacesAndNewlines)
        guard bridge.agentSendMessage(ptr, message: trimmedGoal) else {
            sessionPtr = nil
            activeSessionId = nil
            bridge.agentFreeSession(ptr)
            taskState = .failed
            eventLog.append("[error] Failed to start agent task")
            return
        }

        let bridgeRef = bridge
        pollTask = Task { @MainActor [weak self] in
            defer {
                if let self, self.activeSessionId == sessionId {
                    self.sessionPtr = nil
                    self.activeSessionId = nil
                }
                bridgeRef.agentFreeSession(ptr)
            }

            while !Task.isCancelled,
                  let self,
                  self.activeSessionId == sessionId,
                  self.taskState == .working {
                if let eventJson = bridgeRef.agentPollEvent(ptr) {
                    self.processEvent(eventJson)
                }
                do {
                    try await Task.sleep(for: .milliseconds(100))
                } catch {
                    break
                }
            }
        }
    }

    func cancelAgent() {
        if let ptr = sessionPtr {
            _ = bridge.agentCancel(ptr)
        }
        taskState = .cancelled
    }

    private func processEvent(_ json: String) {
        guard let data = json.data(using: .utf8),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let type = object["type"] as? String else {
            return
        }

        switch type {
        case "token":
            if let token = object["data"] as? String {
                eventLog.append("[token] \(token)")
            }
        case "complete":
            if let payload = object["data"] as? [String: Any],
               let fullText = payload["full_text"] as? String {
                resultMessage = fullText
            }
            taskState = .completed
        case "error":
            let err = (object["data"] as? String) ?? "Unknown error"
            resultMessage = "Error: \(err)"
            taskState = .failed
        default:
            break
        }
    }
}

@MainActor
struct AgentView: View {
    @StateObject private var viewModel = AgentViewModel()

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
                TextField("Describe the task", text: $viewModel.goal, axis: .vertical)
                    .lineLimit(2...5)
                    .disabled(viewModel.taskState == .working)
            } header: {
                Text("Task")
            }

            Section {
                Button {
                    viewModel.runAgent()
                } label: {
                    HStack {
                        Label("Run Agent", systemImage: "play.fill")
                        if viewModel.taskState == .working {
                            Spacer()
                            ProgressView()
                        }
                    }
                }
                .disabled(!viewModel.canSubmit)

                if viewModel.canCancel {
                    Button(role: .destructive) {
                        viewModel.cancelAgent()
                    } label: {
                        Label("Cancel", systemImage: "xmark.circle")
                    }
                }
            }

            if let resultMessage = viewModel.resultMessage {
                Section {
                    Text(resultMessage)
                        .foregroundStyle(.primary)
                        .fixedSize(horizontal: false, vertical: true)
                } header: {
                    Text(viewModel.taskState == .failed ? "Failed" :
                         viewModel.taskState == .cancelled ? "Cancelled" : "Result")
                }
            }

            if !viewModel.eventLog.isEmpty {
                Section {
                    ForEach(viewModel.eventLog.suffix(20), id: \.self) { entry in
                        Text(entry)
                            .font(.caption.monospaced())
                            .foregroundStyle(.secondary)
                    }
                } header: {
                    Text("Event Log")
                }
            }
        }
        .navigationTitle("Web Agent")
        .navigationBarTitleDisplayMode(.inline)
        .accessibilityIdentifier("aiScreenAgent")
    }
}
