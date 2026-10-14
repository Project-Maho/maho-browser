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

    /// Result of the off-main create+send handshake. The pointer is only ever
    /// handed back to the bridge, which serializes access through its lease
    /// registry.
    private struct StartResult: @unchecked Sendable {
        let ptr: OpaquePointer?
        let failure: String?
    }

    static let maxLogEntries = 500

    func appendLog(_ entry: String) {
        eventLog.append(entry)
        if eventLog.count > Self.maxLogEntries {
            eventLog.removeFirst(eventLog.count - Self.maxLogEntries)
        }
    }

    func runAgent() {
        eventLog.removeAll()
        resultMessage = nil
        taskState = .working

        let sessionId = UUID().uuidString
        let trimmedGoal = goal.trimmingCharacters(in: .whitespacesAndNewlines)
        activeSessionId = sessionId

        let bridgeRef = bridge
        pollTask = Task { @MainActor [weak self] in
            // The native lease handshake fails when issued on Main, so create
            // and send run off the main actor and only UI state hops back.
            let started = await Task.detached(priority: .userInitiated) { () -> StartResult in
                guard let ptr = bridgeRef.agentCreateSession(sessionId: sessionId) else {
                    return StartResult(ptr: nil, failure: "Failed to create local agent session")
                }
                guard bridgeRef.agentSendMessage(ptr, message: trimmedGoal) else {
                    bridgeRef.agentFreeSession(ptr)
                    return StartResult(ptr: nil, failure: "Failed to start agent task")
                }
                return StartResult(ptr: ptr, failure: nil)
            }.value

            guard let ptr = self?.adoptStartedSession(started, sessionId: sessionId) else {
                // Owner gone: release a session that never got adopted.
                if self == nil, let orphan = started.ptr { bridgeRef.agentFreeSession(orphan) }
                return
            }

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
                // Drain everything queued so a token burst is not rate-limited
                // to one event per tick.
                while self.taskState == .working, let eventJson = bridgeRef.agentPollEvent(ptr) {
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

    private func adoptStartedSession(_ started: StartResult, sessionId: String) -> OpaquePointer? {
        guard activeSessionId == sessionId, taskState == .working else {
            // Cancelled or superseded while the session was starting.
            if let ptr = started.ptr { bridge.agentFreeSession(ptr) }
            return nil
        }
        guard let ptr = started.ptr else {
            activeSessionId = nil
            taskState = .failed
            appendLog("[error] \(started.failure ?? "Failed to start agent task")")
            return nil
        }
        sessionPtr = ptr
        return ptr
    }

    func cancelAgent() {
        if let ptr = sessionPtr {
            let bridgeRef = bridge
            let box = StartResult(ptr: ptr, failure: nil)
            Task.detached(priority: .userInitiated) {
                if let ptr = box.ptr { _ = bridgeRef.agentCancel(ptr) }
            }
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
                appendLog("[token] \(token)")
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
                    ForEach(Array(viewModel.eventLog.suffix(20).enumerated()), id: \.offset) { _, entry in
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
