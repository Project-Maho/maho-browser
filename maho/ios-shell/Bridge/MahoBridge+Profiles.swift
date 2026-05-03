import Foundation

extension MahoBridge {

    func getProfiles() -> [ProfileConfig] {
        (withCore { ptr in
            FFIString.consumeJSON(maho_core_list_profiles(ptr))
        } ?? nil) ?? []
    }

    func getActiveProfileId() -> ProfileId? {
        withCore { ptr in
            FFIString.consume(maho_core_get_active_profile_id(ptr))
        } ?? nil
    }

    func createProfile(config name: String) {
        sendEvent(.createProfile(name: name))
    }

    func updateProfile(id: ProfileId, name: String?, avatarColor: String?, downloadPath: String?) {
        sendEvent(.updateProfile(
            profileId: id,
            name: name,
            avatarColor: avatarColor,
            downloadPath: downloadPath,
            archiveTimeoutHours: .absent
        ))
    }

    func deleteProfile(id: ProfileId) {
        sendEvent(.deleteProfile(profileId: id))
    }

    @discardableResult
    func switchProfile(id: ProfileId) -> Bool {
        (withCore { ptr in
            id.withCString { cId in
                maho_core_switch_profile(ptr, cId)
            }
        }) ?? false
    }

    func handleProfileUpdate(_ update: CoreUpdate) {
        switch update {
        case .profileCreated:
            break
        case .profileUpdated:
            break
        case .profileDeleted:
            break
        case .activeProfileChanged:
            break
        default:
            break
        }
    }
}
