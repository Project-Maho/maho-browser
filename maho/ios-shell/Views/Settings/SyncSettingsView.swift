import SwiftUI
import LucideIcons
#if canImport(UIKit)
import UIKit
#endif

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
    @State private var activeProfileName: String?
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
    @State private var recoveryRequirement: SyncRecoveryRequirement?

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
            recoveryPhraseRequiredSection
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
        .onReceive(NotificationCenter.default.publisher(for: NSNotification.Name("MahoActiveProfileChanged"))) { _ in
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
                    if let activeProfileName, !activeProfileName.isEmpty {
                        Text("Active profile: \(activeProfileName)")
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

    /// Shown when the account holds its own keys (bootstrap `key_mode:
    /// client_held`) or the relay retired this device's room (410). Sync stays
    /// paused until the phrase is entered; no new seed is ever generated.
    @ViewBuilder
    private var recoveryPhraseRequiredSection: some View {
        if relaySession != nil, let recoveryRequirement {
            Section {
                Text(recoveryRequirement.message)
                    .font(.body)
                SecureField("24-word Recovery Phrase", text: $recoveryPhraseInput)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                Button("Join Sync with Recovery Phrase") {
                    joinWithRecoveryPhrase()
                }
                .disabled(recoveryPhraseInput.isEmpty)
            } header: {
                Text("Recovery Phrase Required")
            }
        }
    }

    @ViewBuilder
    private var syncToggleSection: some View {
        if relaySession != nil {
            Section {
                Toggle(
                    "Enable Polling Sync",
                    isOn: Binding(
                        get: { syncEnabled },
                        set: { setPollingSyncEnabled($0) }
                    )
                )

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

                Button("Poll Now") {
                    SyncManager.shared.syncNow()
                }
                .disabled(!syncEnabled)
            } header: {
                Text(activeProfileName.map { "Status · \($0)" } ?? "Status")
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
        return bridge.getTabViewModels().first(where: { $0.id == activeTabId && !$0.isPrivate })
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
                    joinWithRecoveryPhrase()
                }
                .disabled(recoveryPhraseInput.isEmpty)
            } header: {
                Text("Sync Key")
            }
        }
    }

    private func joinWithRecoveryPhrase() {
        let phrase = recoveryPhraseInput.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !phrase.isEmpty else { return }
        let result = bridge.joinSync(recoveryPhrase: phrase)
        if Self.joinSucceeded(result) {
            recoveryPhraseInput = ""
            joinResult = "Joined sync with your recovery phrase."
            // Lift the pause; the next cycle verifies the joined room against
            // the account's client-held room before pushing or pulling.
            SyncTransport.shared.recoveryPhraseJoined()
            if syncEnabled {
                SyncManager.shared.syncNow()
            }
        } else {
            joinResult = Self.joinErrorMessage(result) ?? "Failed to join sync."
        }
        showJoinResult = true
        refresh()
    }

    private static func joinSucceeded(_ result: String?) -> Bool {
        guard let data = result?.data(using: .utf8),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return false
        }
        return json["success"] as? Bool == true
    }

    private static func joinErrorMessage(_ result: String?) -> String? {
        guard let data = result?.data(using: .utf8),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return nil
        }
        return json["error"] as? String
    }

    private var isAuthActionDisabled: Bool {
        isSubmittingAuth || emailInput.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty || passwordInput.isEmpty
    }

    private func setPollingSyncEnabled(_ enabled: Bool) {
        guard relaySession != nil, enabled != syncEnabled else { return }
        syncEnabled = enabled
        do {
            try bridge.setSyncEnabled(enabled)
            if enabled {
                SyncManager.shared.startSync()
            } else {
                SyncManager.shared.stopSync()
            }
        } catch {
            syncEnabled = SyncManager.shared.isSyncEnabled
            authMessage = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
        }
        refresh()
    }

    private func configureView() {
        refresh()
        SyncManager.shared.configureForCurrentSession()
    }

    private func refresh() {
        relaySession = authStore.loadSession()
        recoveryRequirement = SyncTransport.shared.recoveryRequirement
        syncEnabled = SyncManager.shared.isSyncEnabled && relaySession != nil

        if let activeProfileId = bridge.getActiveProfileId() {
            activeProfileName = bridge.getProfiles().first(where: { $0.id == activeProfileId })?.name ?? activeProfileId
        } else {
            activeProfileName = nil
        }

        if let status = bridge.getSyncStatus() {
            syncStatus = status
        } else if relaySession == nil {
            syncStatus = .idle
        }
        // A cycle that fails before core sync is configured (expired session,
        // bootstrap error) cannot be recorded in core; surface it instead of
        // leaving the screen at "Ready to Poll".
        if relaySession != nil, case .idle = syncStatus,
           case .failed(let message)? = SyncTransport.shared.lastCycleOutcome {
            syncStatus = .error(message: message)
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
            try await prepareAuthenticatedSession(session)
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
                authStore: authStore,
                persistSession: false
            )
            let session = try await flow.signIn()
            try await prepareAuthenticatedSession(session)
            authMessage = "Logged in with Google."
            refresh()
        } catch {
            guard !GoogleSignInError.isCancellation(error) else { return }
            authMessage = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
        }
    }

    private func prepareAuthenticatedSession(_ session: RelayAuthSession) async throws {
        bridge.signIn(
            email: session.account.email,
            displayName: session.account.displayName,
            password: nil,
            accessToken: session.accessToken,
            userId: session.account.id,
            deviceId: session.device?.id,
        )
        do {
            if try await configureAccountSync(session) {
                try bridge.setSyncEnabled(true)
            }
            try authStore.saveSession(session)
            relaySession = session
            SyncManager.shared.relayAuthenticationSucceeded()
        } catch {
            try? SyncSeedStore.shared.clearSeed()
            bridge.signOut()
            throw error
        }
    }

    /// Returns `false` when the account holds its own keys and sync must wait
    /// for the recovery phrase (no seed is generated or uploaded).
    private func configureAccountSync(_ session: RelayAuthSession) async throws -> Bool {
        let bootstrap: RelaySyncBootstrap
        do {
            bootstrap = try await apiClient.getSyncBootstrap(session: session)
        } catch RelayAPIError.http(let statusCode, _) where statusCode == 404 {
            if (try? bridge.syncRoomID()) != nil {
                throw SyncCoreError.operationFailed(
                    "Enter your Recovery Phrase to migrate existing Sync."
                )
            }
            let generated = try bridge.generateSyncBootstrap()
            do {
                bootstrap = try await apiClient.putSyncBootstrap(generated, session: session)
            } catch RelayAPIError.http(let statusCode, _) where statusCode == 409 {
                bootstrap = try await apiClient.getSyncBootstrap(session: session)
            }
        }

        if bootstrap.isClientHeld {
            try? SyncSeedStore.shared.clearSeed()
            if let joinedRoom = try? bridge.syncRoomID(), joinedRoom == bootstrap.roomId {
                return true
            }
            SyncTransport.shared.requireRecoveryPhrase(.clientHeldBootstrap)
            return false
        }

        guard let seed = bootstrap.seed, !seed.isEmpty else {
            throw SyncTransportError.protocolViolation("escrow bootstrap is missing its seed")
        }
        try bridge.configureSyncBootstrap(
            serverURL: RelayAPIClient.currentBaseURLString(),
            seed: seed
        )
        // Sync restarts reuse this device-local seed instead of re-fetching the bootstrap.
        try SyncSeedStore.shared.saveSeed(seed, accountID: session.account.id)
        return true
    }

    private func signOut() async {
        guard let relaySession else { return }

        do {
            try await RelaySignOutFlow(apiClient: apiClient, authStore: authStore)
                .signOut(session: relaySession) {
                    try? bridge.setSyncEnabled(false)
                    SyncManager.shared.stopSync(persistPreference: true)
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
