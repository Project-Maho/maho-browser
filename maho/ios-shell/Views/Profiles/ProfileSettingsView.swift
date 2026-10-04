import SwiftUI
import LucideIcons

struct ProfileSettingsView: View {
    @State private var profiles: [ProfileConfig] = []
    @State private var activeProfileId: ProfileId = ""
    @State private var isLoading = false
    @State private var showCreateSheet = false
    @State private var showDeleteConfirmation = false
    @State private var profileToDelete: ProfileConfig?
    @State private var editingProfile: ProfileConfig?

    var body: some View {
        NavigationStack {
            Group {
                if isLoading {
                    ProgressView()
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else if profiles.isEmpty {
                    emptyState
                } else {
                    profileList
                }
            }
            .navigationTitle("Profiles")
            .toolbar {
                ToolbarItemGroup(placement: .navigationBarTrailing) {
                    Button {
                        showCreateSheet = true
                    } label: {
                        Image(lucide: Lucide.plus)
                    }
                }
            }
            .sheet(isPresented: $showCreateSheet) {
                ProfileFormSheet(mode: .create) { name, avatarColor, downloadPath in
                    createProfile(name: name, avatarColor: avatarColor, downloadPath: downloadPath)
                    showCreateSheet = false
                }
            }
            .sheet(item: $editingProfile) { profile in
                ProfileFormSheet(mode: .edit(profile)) { name, avatarColor, downloadPath in
                    updateProfile(profile: profile, name: name, avatarColor: avatarColor, downloadPath: downloadPath)
                    editingProfile = nil
                }
            }
            .confirmationDialog(
                "Delete Profile?",
                isPresented: $showDeleteConfirmation,
                titleVisibility: .visible
            ) {
                Button("Delete", role: .destructive) {
                    if let profile = profileToDelete {
                        deleteProfile(profile)
                    }
                }
                Button("Cancel", role: .cancel) {
                    profileToDelete = nil
                }
            } message: {
                Text("This will permanently delete this profile and all its data.")
            }
            .onAppear(perform: loadProfiles)
        }
    }

    // MARK: - Subviews

    private var profileList: some View {
        List {
            ForEach(profiles, id: \.id) { profile in
                ProfileRow(
                    profile: profile,
                    isActive: profile.id == activeProfileId
                )
                .contentShape(Rectangle())
                .onTapGesture {
                    switchProfile(profile)
                }
                .swipeActions(edge: .trailing) {
                    Button(role: .destructive) {
                        profileToDelete = profile
                        showDeleteConfirmation = true
                    } label: {
                        Label { Text("Delete") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("trash")) }
                    }
                }
                .swipeActions(edge: .leading) {
                    Button {
                        editingProfile = profile
                    } label: {
                        Label { Text("Edit") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("pencil")) }
                    }
                    .tint(.blue)
                }
            }
        }
        .listStyle(.plain)
    }

    private var emptyState: some View {
        VStack(spacing: 12) {
            Image(lucide: Lucide.circleUser)
                .font(.system(size: 48))
                .foregroundStyle(.secondary)
            Text("No Profiles")
                .font(.title3)
                .fontWeight(.medium)
            Text("Create a profile to separate browsing contexts.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // MARK: - Actions

    private func loadProfiles() {
        isLoading = true
        profiles = MahoBridge.shared.getProfiles()
        activeProfileId = MahoBridge.shared.getActiveProfileId() ?? ""
        isLoading = false
    }

    private func createProfile(name: String, avatarColor: String, downloadPath: String) {
        MahoBridge.shared.sendEvent(.createProfile(name: name))
        loadProfiles()
    }

    private func updateProfile(profile: ProfileConfig, name: String, avatarColor: String, downloadPath: String) {
        MahoBridge.shared.sendEvent(.updateProfile(
            profileId: profile.id,
            name: name,
            avatarColor: avatarColor,
            downloadPath: downloadPath,
            archiveTimeoutHours: .absent
        ))
        loadProfiles()
    }

    private func deleteProfile(_ profile: ProfileConfig) {
        MahoBridge.shared.sendEvent(.deleteProfile(profileId: profile.id))
        profiles.removeAll { $0.id == profile.id }
        profileToDelete = nil
    }

    private func switchProfile(_ profile: ProfileConfig) {
        MahoBridge.shared.switchProfile(id: profile.id)
        activeProfileId = profile.id
    }
}

// MARK: - ProfileRow

private struct ProfileRow: View {
    let profile: ProfileConfig
    let isActive: Bool

    var body: some View {
        HStack(spacing: 12) {
            Circle()
                .fill(Color(hex: profile.avatarColor))
                .frame(width: 36, height: 36)
                .overlay {
                    Text(String(profile.name.prefix(1)).uppercased())
                        .font(.headline)
                        .foregroundStyle(.white)
                }

            VStack(alignment: .leading, spacing: 2) {
                Text(profile.name)
                    .font(.body)
                Text(profile.downloadPath)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }

            Spacer()

            if isActive {
                Image(lucide: Lucide.circleCheck)
                    .foregroundStyle(.blue)
            }
        }
        .padding(.vertical, 4)
    }
}

// MARK: - ProfileFormSheet

private struct ProfileFormSheet: View {
    enum Mode {
        case create
        case edit(ProfileConfig)
    }

    let mode: Mode
    let onSave: (String, String, String) -> Void

    @Environment(\.dismiss) private var dismiss
    @State private var name: String = ""
    @State private var avatarColor: String = "#4A90D9"
    @State private var downloadPath: String = ""

    private var title: String {
        switch mode {
        case .create: return "New Profile"
        case .edit: return "Edit Profile"
        }
    }

    var body: some View {
        NavigationStack {
            Form {
                Section("Profile Name") {
                    TextField("Name", text: $name)
                }
                Section("Avatar Color") {
                    TextField("Hex color", text: $avatarColor)
                }
                Section("Download Path") {
                    TextField("Path", text: $downloadPath)
                }
            }
            .navigationTitle(title)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItemGroup(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
                ToolbarItemGroup(placement: .confirmationAction) {
                    Button("Save") {
                        onSave(name, avatarColor, downloadPath)
                    }
                    .disabled(name.trimmingCharacters(in: .whitespaces).isEmpty)
                }
            }
            .onAppear {
                if case .edit(let profile) = mode {
                    name = profile.name
                    avatarColor = profile.avatarColor
                    downloadPath = profile.downloadPath
                }
            }
        }
    }
}

// MARK: - ProfileConfig Identifiable conformance

extension ProfileConfig: @retroactive Identifiable {}

// MARK: - Color hex helper

private extension Color {
    init(hex: String) {
        let hex = hex.trimmingCharacters(in: CharacterSet(charactersIn: "#"))
        let scanner = Scanner(string: hex)
        var rgbValue: UInt64 = 0
        scanner.scanHexInt64(&rgbValue)
        let r = Double((rgbValue & 0xFF0000) >> 16) / 255.0
        let g = Double((rgbValue & 0x00FF00) >> 8) / 255.0
        let b = Double(rgbValue & 0x0000FF) / 255.0
        self.init(red: r, green: g, blue: b)
    }
}
