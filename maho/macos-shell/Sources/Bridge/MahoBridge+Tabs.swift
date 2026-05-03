import Foundation

// MARK: - Tab Operations

extension MahoBridge {

    func getTabViewModels() -> [TabViewModel] {
        guard let json = callCoreString({ maho_core_get_tab_view_models($0) }) else { return [] }
        return decode(json) ?? []
    }

    func getPinnedTabs(spaceId: SpaceId) -> [TabViewModel] {
        guard let spaceJson = encode(spaceId) else { return [] }
        return withFFIString(spaceJson) { cStr -> [TabViewModel] in
            guard let ptr = callCore({ maho_core_get_pinned_tabs($0, cStr) }) else { return [] }
            defer { maho_string_free(ptr) }
            return decode(String(cString: ptr)) ?? []
        }
    }

    func getSpaceTabs(spaceId: SpaceId) -> [TabViewModel] {
        guard let spaceJson = encode(spaceId) else { return [] }
        return withFFIString(spaceJson) { cStr -> [TabViewModel] in
            guard let ptr = callCore({ maho_core_get_space_tabs($0, cStr) }) else { return [] }
            defer { maho_string_free(ptr) }
            return decode(String(cString: ptr)) ?? []
        }
    }

    func getFavoriteTabs(spaceId: SpaceId) -> [TabViewModel] {
        guard let spaceJson = encode(spaceId) else { return [] }
        return withFFIString(spaceJson) { cStr -> [TabViewModel] in
            guard let ptr = callCore({ maho_core_get_favorite_tabs($0, cStr) }) else { return [] }
            defer { maho_string_free(ptr) }
            return decode(String(cString: ptr)) ?? []
        }
    }

    func getTodayTabs(spaceId: SpaceId) -> [TabViewModel] {
        guard let spaceJson = encode(spaceId) else { return [] }
        return withFFIString(spaceJson) { cStr -> [TabViewModel] in
            guard let ptr = callCore({ maho_core_get_today_tabs($0, cStr) }) else { return [] }
            defer { maho_string_free(ptr) }
            return decode(String(cString: ptr)) ?? []
        }
    }

    func getArchivedTabs(spaceId: SpaceId) -> [ArchivedTabViewModel] {
        guard let spaceJson = encode(spaceId) else { return [] }
        return withFFIString(spaceJson) { cStr -> [ArchivedTabViewModel] in
            guard let ptr = callCore({ maho_core_get_archived_tabs($0, cStr) }) else { return [] }
            defer { maho_string_free(ptr) }
            return decode(String(cString: ptr)) ?? []
        }
    }

    func setTabParent(tabId: String, newParentId: String?) {
        withFFIString(tabId) { tabCStr in
            if let parentId = newParentId {
                withFFIString(parentId) { parentCStr in
                    callCoreVoid { maho_core_set_tab_parent($0, tabCStr, parentCStr) }
                }
            } else {
                callCoreVoid { maho_core_set_tab_parent($0, tabCStr, nil) }
            }
        }
    }

    func resetPinnedTab(tabId: String) -> Bool {
        return withFFIString(tabId) { cStr -> Bool in
            guard let result = callCoreNonOptional({ maho_core_reset_pinned_tab($0, cStr) }) else { return false }
            return result
        }
    }

    func checkPinnedNavigation(tabId: String, url: String) -> [CoreUpdate] {
        return withFFIString(tabId) { tabCStr -> [CoreUpdate] in
            return withFFIString(url) { urlCStr -> [CoreUpdate] in
                guard let ptr = callCore({ maho_core_check_pinned_navigation($0, tabCStr, urlCStr) }) else { return [] }
                defer { maho_string_free(ptr) }
                let json = String(cString: ptr)
                if let single: CoreUpdate = decode(json) {
                    return [single]
                }
                return decode(json) ?? []
            }
        }
    }

    // MARK: - Tab Previews

    func updateTabPreview(tabId: String, data: [UInt8]) {
        withFFIString(tabId) { tabCStr in
            data.withUnsafeBufferPointer { buffer in
                guard let baseAddress = buffer.baseAddress else { return }
                callCoreVoid { maho_core_update_tab_preview($0, tabCStr, baseAddress, UInt(buffer.count)) }
            }
        }
    }

    func getTabPreview(tabId: String) -> [UInt8]? {
        return withFFIString(tabId) { tabCStr -> [UInt8]? in
            var outLen: UInt = 0
            guard let ptr = callCore({ maho_core_get_tab_preview($0, tabCStr, &outLen) }) else { return nil }
            guard outLen > 0 else {
                maho_core_free_preview_data(ptr, outLen)
                return nil
            }
            let data = Array(UnsafeBufferPointer(start: ptr, count: Int(outLen)))
            maho_core_free_preview_data(ptr, outLen)
            return data
        }
    }

    func takePendingPreviewCaptures() -> [String] {
        guard let json = callCoreString({ maho_core_take_pending_preview_captures($0) }) else { return [] }
        return decode(json) ?? []
    }

    func schedulePreviewCapture(tabId: String) -> Bool {
        return withFFIString(tabId) { cStr -> Bool in
            guard let result = callCoreNonOptional({ maho_core_schedule_preview_capture($0, cStr) }) else { return false }
            return result
        }
    }

    // MARK: - Usage Tracking

    func recordUsage(itemKey: String) {
        withFFIString(itemKey) { cStr in
            callCoreVoid { maho_core_record_usage($0, cStr) }
        }
    }

    func getTopUsed(limit: UInt) -> String? {
        return callCoreString { maho_core_get_top_used($0, limit) }
    }

    // MARK: - Form Data

    func saveFormData(tabId: String, formJson: String) {
        withFFIString(tabId) { tabCStr in
            withFFIString(formJson) { formCStr in
                callCoreVoid { maho_core_save_form_data($0, tabCStr, formCStr) }
            }
        }
    }

    // MARK: - Crash Save

    func setCrashSaveInterval(seconds: UInt64) {
        callCoreVoid { maho_core_set_crash_save_interval($0, seconds) }
    }

    // MARK: - Searchable Items

    func getSearchableItems() -> String? {
        return callCoreString { maho_core_get_searchable_items($0) }
    }

    // MARK: - Custom Title

    func setTabCustomTitle(tabId: String, title: String) {
        withFFIString(tabId) { tabCStr in
            let trimmed = title.trimmingCharacters(in: .whitespacesAndNewlines)
            if trimmed.isEmpty {
                callCoreVoid { maho_core_set_tab_custom_title($0, tabCStr, nil) }
            } else {
                withFFIString(trimmed) { titleCStr in
                    callCoreVoid { maho_core_set_tab_custom_title($0, tabCStr, titleCStr) }
                }
            }
        }
    }
}
