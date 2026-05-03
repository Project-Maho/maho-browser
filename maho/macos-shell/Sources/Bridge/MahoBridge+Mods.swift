import Foundation

extension MahoBridge {
    func getCssMods() -> [CssMod] {
        guard let json = callCoreString({ maho_core_get_css_mods($0) }) else { return [] }
        return decode(json) ?? []
    }

    func getCombinedModCss() -> String? {
        return callCoreString({ maho_core_get_combined_css($0) })
    }

    func installCssMod(name: String, css: String, description: String? = nil, author: String? = nil, version: String? = nil, homepage: String? = nil, sourceUrl: String? = nil) {
        let updates = sendEvent(.installCssMod(name: name, css: css, description: description, author: author, version: version, homepage: homepage, sourceUrl: sourceUrl))
        CoreUpdateDispatcher.shared.dispatch(updates)
    }

    func toggleCssMod(modId: String) {
        let updates = sendEvent(.toggleCssMod(modId: modId))
        CoreUpdateDispatcher.shared.dispatch(updates)
    }

    func uninstallCssMod(modId: String) {
        let updates = sendEvent(.uninstallCssMod(modId: modId))
        CoreUpdateDispatcher.shared.dispatch(updates)
    }

    func updateCssMod(modId: String, css: String) {
        let updates = sendEvent(.updateCssMod(modId: modId, css: css))
        CoreUpdateDispatcher.shared.dispatch(updates)
    }
}
