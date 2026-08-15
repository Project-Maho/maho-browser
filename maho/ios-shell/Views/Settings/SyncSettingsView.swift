import SwiftUI
import LucideIcons
#if canImport(UIKit)
import UIKit
#endif

private struct SyncBootstrapConfiguration: Decodable {
    let success: Bool
}

@MainActor
struct SyncSettingsView: View {
    private enum AuthMode: String, CaseIterable, Identifiable {
        case login
        case signup

        var id: String { rawValue }

        var buttonTitle: String {
            switch self {
            case .login: return "Log In"
            case .signup: return "Sign Up"
            }
        }
    }

    private let bridge = MahoBridge.shared
    private let authStore: RelayAuthStoreProtocol
    private let apiClient: RelayAPIClientProtocol
    private let googleCoordinatorProvider: () throws -> any GoogleSignInCoordinating

    @State private var syncStatus: SyncStatus = .idle
    @State private var account: AccountInfo?
    @State private var relaySession: RelayAuthSession?
    @State private var syncEnabled = false
    @State private var devices: [ConnectedDevice] = []
    @State private var authMode: AuthMode = .login
    @State private var emailInput = ""
    @State private var passwordInput = ""
    @State private var displayNameInput = ""
    @State private var authMessage: String?
    @State private var recoveryPhraseInput = ""
    @State private var generatedSyncKey: String?
    @State private var joinResult: String?
    @State private var showSyncKeyAlert = false
    @State private var showJoinResult = false
    @State private var isSubmittingAuth = false
    @State private var pendingDeviceDeletion: IndexSet?
    @State private var showRemoveDeviceConfirmation = false

    init(
        authStore: RelayAuthStoreProtocol = RelayAuthStore.shared,
        apiClient: RelayAPIClientProtocol = RelayAPIClient(),
        googleCoordinatorProvider: (() throws -> any GoogleSignInCoordinating)? = nil
    ) {
        self.authStore = authStore
        self.apiClient = apiClient
        self.googleCoordinatorProvider = googleCoordinatorProvider ?? {
            guard let configuration = GoogleSignInConfiguration.fromBundle() else {
                throw GoogleSignInError.missingClientID
            }
            return GoogleSignInCoordinator(configuration: configuration)
        }
    }

    var body: some View {
        Form {
            accountSection
            syncToggleSection
            syncStatusSection
            devicesSection
            syncKeySection
        }
        .navigationTitle("Sync")
        .onAppear(perform: configureView)
        .onReceive(NotificationCenter.default.publisher(for: .syncStatusChanged)) { _ in
            refresh()
        }
        .alert("Sync Key", isPresented: $showSyncKeyAlert) {
            Button("Copy") {
                if let key = generatedSyncKey {
                    UIPasteboard.general.string = key
                }
            }
            Button("OK", role: .cancel) {}
        } message: {
            Text(generatedSyncKey ?? "")
        }
        .alert("Join Sync", isPresented: $showJoinResult) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(joinResult ?? "Failed to join sync")
        }
        .confirmationDialog(
            "Remove Device?",
            isPresented: $showRemoveDeviceConfirmation,
            titleVisibility: .visible
        ) {
            Button("Remove Device", role: .destructive) {
                removePendingDevices()
            }
            Button("Cancel", role: .cancel) {
                pendingDeviceDeletion = nil
            }
        } message: {
            Text("This disconnects the selected device from sync.")
        }
    }

    @ViewBuilder
    private var accountSection: some View {
        Section {
            if let relaySession {
                VStack(alignment: .leading, spacing: ShellTheme.Spacing.small) {
                    Text(relaySession.account.email)
                        .font(.body)
                    if let displayName = relaySession.account.displayName, !displayName.isEmpty {
                        Text(displayName)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    if let account,
                       let coreDisplayName = account.displayName,
                       coreDisplayName != relaySession.account.displayName {
                        Text(coreDisplayName)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }

                Button("Log Out", role: .destructive) {
                    Task {
                        await signOut()
                    }
                }
            } else {
                Picker("Mode", selection: $authMode) {
                    ForEach(AuthMode.allCases) { mode in
                        Text(mode == .login ? "Log In" : "Sign Up").tag(mode)
                    }
                }
                .pickerStyle(.segmented)

                TextField("Email", text: $emailInput)
                    .textContentType(.emailAddress)
                    .keyboardType(.emailAddress)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()

                SecureField("Password", text: $passwordInput)
                    .textContentType(authMode == .login ? .password : .newPassword)

                if authMode == .signup {
                    TextField("Display Name (Optional)", text: $displayNameInput)
                        .textContentType(.name)
                }

                HStack(spacing: ShellTheme.Spacing.medium) {
                    if authMode == .signup {
                        Button("Sign Up") {
                            Task {
                                await submitAuth(mode: .signup)
                            }
                        }
                        .disabled(isAuthActionDisabled)
                    }

                    Button("Log In") {
                        Task {
                            await submitAuth(mode: .login)
                        }
                    }
                    .disabled(isAuthActionDisabled)
                }

                Button("Continue with Google") {
                    Task {
                        await signInWithGoogle()
                    }
                }
                .disabled(isSubmittingAuth)
                .accessibilityIdentifier("continueWithGoogleButton")

                if let authMessage, !authMessage.isEmpty {
                    Text(authMessage)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        } header: {
            Text("Relay Account")
        }
    }

    @ViewBuilder
    private var syncToggleSection: some View {
        if relaySession != nil {
            Section {
                Toggle("Enable Polling Sync", isOn: $syncEnabled)
                    .onChange(of: syncEnabled) { _, newValue in
                        if newValue {
                            bridge.toggleSync()
                            SyncManager.shared.startSync()
                        } else {
                            SyncManager.shared.stopSync()
                            bridge.toggleSync()
                        }
                    }

                Text("When enabled, Maho periodically pushes local changes and pulls relay updates while the app is active.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            } header: {
                Text("Polling")
            }
        }
    }

    @ViewBuilder
    private var syncStatusSection: some View {
        if relaySession != nil {
            Section {
                HStack(spacing: ShellTheme.Spacing.medium) {
                    statusIcon
                    VStack(alignment: .leading, spacing: ShellTheme.Spacing.xxSmall) {
                        Text(statusLabel)
                            .font(.body)
                        if let detail = statusDetail {
                            Text(detail)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                }

                if relaySession != nil {
                    Button("Poll Now") {
                        SyncManager.shared.syncNow()
                    }
                    .disabled(!syncEnabled)
                }
            } header: {
                Text("Status")
            }
        }
    }

    private var statusIcon: some View {
        Group {
            switch syncStatus {
            case .idle:
                Image(lucide: Lucide.circleMinus)
                    .foregroundStyle(.secondary)
            case .syncing:
                Image(lucide: Lucide.refreshCw)
                    .foregroundStyle(ShellTheme.Palette.accent)
            case .synced:
                Image(lucide: Lucide.circleCheck)
                    .foregroundStyle(ShellTheme.Palette.success)
            case .error:
                Image(lucide: Lucide.triangleAlert)
                    .foregroundStyle(ShellTheme.Palette.error)
            case .offline:
                Image(lucide: Lucide.wifiOff)
                    .foregroundStyle(ShellTheme.Palette.warning)
            }
        }
    }

    private var statusLabel: String {
        switch syncStatus {
        case .idle: return relaySession == nil ? "Signed Out" : "Ready to Poll"
        case .syncing: return "Polling Relay"
        case .synced: return "Relay Up to Date"
        case .error: return "Polling Error"
        case .offline: return "Offline"
        }
    }

    private var statusDetail: String? {
        switch syncStatus {
        case .syncing(let progress):
            return progress > 0 ? "\(Int(progress * 100))% complete" : "Pushing local changes and checking relay updates."
        case .synced(let lastSyncAt):
            return "Last poll finished: \(lastSyncAt)"
        case .error(let message):
            return message
        case .idle:
            if relaySession != nil {
                return syncEnabled ? "Polling resumes automatically while the app is active." : "Enable polling sync to start periodic relay updates."
            }
            return "Sign up or log in with your relay email and password to enable polling sync."
        case .offline:
            return "Relay polling is paused until connectivity returns."
        }
    }

    @ViewBuilder
    private var devicesSection: some View {
        if !devices.isEmpty {
            Section {
                ForEach(devices) { device in
                    HStack {
                        Image(lucide: deviceIcon(device.deviceType))
                            .foregroundStyle(.secondary)
                        VStack(alignment: .leading) {
                            Text(device.name)
                                .font(.body)
                            if let lastSeen = device.lastSeen {
                                Text("Last seen: \(lastSeen)")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }
                        Spacer()
                        Button {
                            sendActiveTab(to: device)
                        } label: {
                            Image(lucide: Lucide.send)
                                .frame(minWidth: ShellTheme.Size.touchTarget, minHeight: ShellTheme.Size.touchTarget)
                                .contentShape(Rectangle())
                        }
                        .buttonStyle(.borderless)
                        .disabled(activeTabForSend == nil)
                        .accessibilityLabel("Send active tab to \(device.name)")
                    }
                }
                .onDelete { offsets in
                    pendingDeviceDeletion = offsets
                    showRemoveDeviceConfirmation = true
                }
            } header: {
                Text("Connected Devices")
            }
        }
    }

    private func deviceIcon(_ type: String) -> UIImage {
        switch type.lowercased() {
        case "desktop", "mac", "pc": return Lucide.monitor
        case "phone", "iphone", "android": return Lucide.smartphone
        case "tablet", "ipad": return Lucide.tablet
        default: return Lucide.laptop
        }
    }

    private var activeTabForSend: TabViewModel? {
        guard let activeTabId = bridge.getActiveTabId() else { return nil }
        return bridge.getTabViewModels().first(where: { $0.id == activeTabId })
    }

    private func sendActiveTab(to device: ConnectedDevice) {
        guard let activeTab = activeTabForSend else { return }
        bridge.sendTabToDevice(url: activeTab.url, title: activeTab.title, deviceId: device.id)
    }

    private func removePendingDevices() {
        guard let offsets = pendingDeviceDeletion else { return }
        for index in offsets {
            bridge.removeSyncDevice(deviceId: devices[index].id)
        }
        devices.remove(atOffsets: offsets)
        pendingDeviceDeletion = nil
    }

    @ViewBuilder
    private var syncKeySection: some View {
        if relaySession != nil {
            Section {
                Button("Generate Sync Key") {
                    generatedSyncKey = bridge.generateSyncKey()
                    showSyncKeyAlert = generatedSyncKey != nil
                }
                TextField("Recovery Phrase", text: $recoveryPhraseInput)
                    .textInputAutocapitalization(.never)
                Button("Join with Recovery Phrase") {
                    guard !recoveryPhraseInput.isEmpty else { return }
                    joinResult = bridge.joinSync(recoveryPhrase: recoveryPhraseInput)
                    showJoinResult = true
                    if joinResult != nil {
                        recoveryPhraseInput = ""
                        refresh()
                    }
                }
                .disabled(recoveryPhraseInput.isEmpty)
            } header: {
                Text("Sync Key")
            }
        }
    }

    private var isAuthActionDisabled: Bool {
        isSubmittingAuth || emailInput.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty || passwordInput.isEmpty
    }

    private func configureView() {
        refresh()
        SyncManager.shared.configureForCurrentSession()
    }

    private func refresh() {
        relaySession = authStore.loadSession()
        syncEnabled = SyncManager.shared.isSyncEnabled && relaySession != nil

        if let status = bridge.getSyncStatus() {
            syncStatus = status
        } else if relaySession == nil {
            syncStatus = .idle
        }

        devices = bridge.getConnectedDevices()
        account = relaySession.map {
            AccountInfo(
                email: $0.account.email,
                displayName: $0.account.displayName,
                avatarUrl: nil,
                syncEnabled: syncEnabled
            )
        }

        if relaySession == nil {
            syncEnabled = false
        }
    }

    private func submitAuth(mode: RelayAuthMode) async {
        isSubmittingAuth = true
        authMessage = nil
        defer { isSubmittingAuth = false }

        let email = emailInput.trimmingCharacters(in: .whitespacesAndNewlines)
        let password = passwordInput
        let displayName = displayNameInput.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !email.isEmpty, !password.isEmpty else { return }

        do {
            let session = try await apiClient.authenticate(
                mode: mode,
                email: email,
                password: password,
                displayName: displayName.isEmpty ? nil : displayName
            )
            try authStore.saveSession(session)
            try await configureAccountSync(session)
            applyAuthenticatedSession(session)
            authMessage = mode == .login ? "Logged in to relay." : "Relay account created."
            passwordInput = ""
            displayNameInput = ""
            refresh()
        } catch {
            authMessage = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
        }
    }

    private func signInWithGoogle() async {
        isSubmittingAuth = true
        authMessage = nil
        defer { isSubmittingAuth = false }

        do {
            let flow = GoogleRelaySignInFlow(
                coordinatorProvider: googleCoordinatorProvider,
                apiClient: apiClient,
                authStore: authStore
            )
            let session = try await flow.signIn()
            try await configureAccountSync(session)
            applyAuthenticatedSession(session)
            authMessage = "Logged in with Google."
            refresh()
        } catch {
            guard !GoogleSignInError.isCancellation(error) else { return }
            authMessage = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
        }
    }

    private func applyAuthenticatedSession(_ session: RelayAuthSession) {
        relaySession = session
        bridge.signIn(
            email: session.account.email,
            displayName: session.account.displayName,
            password: nil,
            accessToken: session.accessToken,
            userId: session.account.id,
            deviceId: session.device?.id,
        )
        SyncManager.shared.relayAuthenticationSucceeded()
    }

    private func configureAccountSync(_ session: RelayAuthSession) async throws {
        let bootstrap: RelaySyncBootstrap
        do {
            bootstrap = try await apiClient.getSyncBootstrap(session: session)
        } catch RelayAPIError.http(let statusCode, _) where statusCode == 404 {
            if (try? bridge.syncRoomID()) != nil {
                throw SyncCoreError.operationFailed(
                    "Enter your Recovery Phrase to migrate existing Sync."
                )
            }
            guard let generated = bridge.generateSyncBootstrap(),
                  let data = generated.data(using: .utf8),
                  let created = try? JSONDecoder().decode(RelaySyncBootstrap.self, from: data)
            else {
                throw SyncCoreError.operationFailed("generate account Sync bootstrap")
            }
            do {
                bootstrap = try await apiClient.putSyncBootstrap(created, session: session)
            } catch RelayAPIError.http(let statusCode, _) where statusCode == 409 {
                bootstrap = try await apiClient.getSyncBootstrap(session: session)
            }
        }

        guard let result = bridge.configureSyncBootstrap(
            serverURL: RelayAPIClient.currentBaseURLString(),
            seed: bootstrap.seed
        ),
        let data = result.data(using: .utf8),
        let response = try? JSONDecoder().decode(SyncBootstrapConfiguration.self, from: data),
        response.success
        else {
            throw SyncCoreError.operationFailed("configure account Sync bootstrap")
        }
    }

    private func signOut() async {
        guard let relaySession else { return }

        do {
            try await RelaySignOutFlow(apiClient: apiClient, authStore: authStore)
                .signOut(session: relaySession) {
                    SyncManager.shared.relayAuthenticationEnded()
                    bridge.signOut()
                    SyncTransport.shared.clearRelaySession()
                    self.relaySession = nil
                    syncEnabled = false
                    authMessage = "Logged out of relay."
                    refresh()
                }
        } catch {
            authMessage = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
        }
    }
}
