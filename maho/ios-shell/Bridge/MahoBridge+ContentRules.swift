import CryptoKit
import Foundation
import WebKit
import os.log

extension Notification.Name {
    /// Posted when the Content Blocker setting changes so live web views
    /// re-apply (or drop) their rule lists without waiting for a navigation.
    static let mahoContentBlockerChanged = Notification.Name("dev.maho.browser.contentBlockerChanged")
    /// Posted when a filter-list refresh pass ends, whatever its result, so the
    /// settings footer can show download failures and stop a retry spinner.
    static let mahoFilterListRefreshFinished = Notification.Name("dev.maho.browser.filterListRefreshFinished")
}

extension MahoBridge {

    func compileContentRules(
        json: String,
        identifier: String = "dev.maho.contentblocker",
        completion: @escaping (WKContentRuleList?) -> Void
    ) {
        WKContentRuleListStore.default().compileContentRuleList(
            forIdentifier: identifier,
            encodedContentRuleList: json
        ) { ruleList, error in
            if let error {
                let log = Logger(subsystem: "dev.maho.browser", category: "ContentRules")
                log.error("Failed to compile content rules: \(error.localizedDescription, privacy: .public)")
                completion(nil)
                return
            }
            completion(ruleList)
        }
    }

    func applyContentRules(_ ruleList: WKContentRuleList, to webView: WKWebView) {
        webView.configuration.userContentController.add(ruleList)
    }

    func removeAllContentRules(from webView: WKWebView) {
        webView.configuration.userContentController.removeAllContentRuleLists()
    }

    func lookupContentRules(
        identifier: String = "dev.maho.contentblocker",
        completion: @escaping (WKContentRuleList?) -> Void
    ) {
        WKContentRuleListStore.default().lookUpContentRuleList(
            forIdentifier: identifier
        ) { ruleList, error in
            if let error {
                let log = Logger(subsystem: "dev.maho.browser", category: "ContentRules")
                log.error("Failed to look up content rules: \(error.localizedDescription, privacy: .public)")
                completion(nil)
                return
            }
            completion(ruleList)
        }
    }

    func getWhitelistedDomains() -> [String] {
        return UserDefaults.standard.stringArray(forKey: "contentBlockerWhitelist") ?? []
    }
    
    func addDomainToWhitelist(_ domain: String) {
        var list = getWhitelistedDomains()
        if !list.contains(domain) {
            list.append(domain)
            UserDefaults.standard.set(list, forKey: "contentBlockerWhitelist")
        }
    }
    
    func removeDomainFromWhitelist(_ domain: String) {
        var list = getWhitelistedDomains()
        if let idx = list.firstIndex(of: domain) {
            list.remove(at: idx)
            UserDefaults.standard.set(list, forKey: "contentBlockerWhitelist")
        }
    }
    
    func isDomainWhitelisted(_ domain: String) -> Bool {
        return getWhitelistedDomains().contains(domain)
    }

    func refreshContentRules(webView: WKWebView) {
        removeAllContentRules(from: webView)

        // Core emits the toggle as `content_blocker` (settings_manager.rs).
        let isEnabled = (CoreSettingsSnapshot.values(from: self)["content_blocker"] as? Bool) ?? false
        guard isEnabled else { return }
        
        if let url = webView.url, let host = url.host, isDomainWhitelisted(host) {
            return
        }

        guard let jsonRules: String = withCore({ ptr -> String? in
            FFIString.consume(maho_core_get_content_rules(ptr))
        }) ?? nil else { return }
        // Compiling ~100k rules is expensive; WebKit keeps compiled lists on
        // disk, so key them by content and reuse instead of recompiling per page.
        let identifier = "dev.maho.contentblocker." + Self.contentRulesDigest(jsonRules)
        lookupContentRules(identifier: identifier) { [weak self] cached in
            if let cached {
                self?.applyContentRules(cached, to: webView)
                return
            }
            self?.compileContentRules(json: jsonRules, identifier: identifier) { ruleList in
                guard let ruleList else { return }
                self?.applyContentRules(ruleList, to: webView)
                Self.removeStaleContentRuleLists(keeping: identifier)
            }
        }
    }

    private static func contentRulesDigest(_ json: String) -> String {
        SHA256.hash(data: Data(json.utf8)).prefix(12).map { String(format: "%02x", $0) }.joined()
    }

    private static func removeStaleContentRuleLists(keeping identifier: String) {
        let store = WKContentRuleListStore.default()
        store?.getAvailableContentRuleListIdentifiers { identifiers in
            for stale in identifiers ?? [] where stale.hasPrefix("dev.maho.contentblocker") && stale != identifier {
                store?.removeContentRuleList(forIdentifier: stale) { _ in }
            }
        }
    }


    @discardableResult
    func processContentRulesUpdate(_ update: CoreUpdate, handler: ContentRulesUpdateHandler) -> Bool {
        switch update {
        case .contentRulesCompiled(let ruleCount):
            handler.didReceiveContentRulesCompiled(ruleCount: ruleCount)
            return true
        case .contentBlockerStateChanged(let enabled, let popupBlocking):
            handler.didReceiveContentBlockerStateChanged(enabled: enabled, popupBlocking: popupBlocking)
            return true
        case .filterListUpdated(let id, let ruleCount):
            handler.didReceiveFilterListUpdated(id: id, ruleCount: ruleCount)
            return true
        default:
            return false
        }
    }
}

protocol ContentRulesUpdateHandler: AnyObject {
    func didReceiveContentRulesCompiled(ruleCount: Int)
    func didReceiveContentBlockerStateChanged(enabled: Bool, popupBlocking: Bool)
    func didReceiveFilterListUpdated(id: String, ruleCount: Int)
}

/// Downloads the enabled filter lists so the content blocker has rules to apply.
/// Core owns validation, candidate promotion and persistence; this only does the
/// HTTP fetch (with ETag/Last-Modified revalidation) and drives core's
/// apply -> compile -> install pipeline, then tells web views to re-apply.
final class FilterListUpdater: @unchecked Sendable {
    static let shared = FilterListUpdater()

    private struct ListMeta: Decodable {
        let id: String
        let url: String
        let enabled: Bool
        let etag: String?
        let lastModified: String?
        let lastSuccessTimestamp: Int64?
        let nextRetryTimestamp: Int64?
        let lastError: String?
    }

    private struct UpdateResponse: Encodable {
        let listId: String
        let statusCode: Int
        let body: String?
        let etag: String?
        let lastModified: String?
        let sha256: String?
    }

    private struct MutationResult: Decodable {
        let success: Bool
        let compileRequired: Bool?
    }

    /// Lists are refreshed at most this often unless one has never succeeded.
    private static let refreshInterval: TimeInterval = 24 * 60 * 60
    /// Core's `TRANSPORT_FAILURE_STATUS`: the request never reached a server.
    /// Core records it without escalating the retry backoff, so a device that
    /// was merely offline retries on the next foreground.
    static let transportFailureStatus = 1
    /// Any other failure without an HTTP response (bad TLS, non-HTTP reply);
    /// core counts it toward the backoff like an HTTP error.
    static let unreachableStatus = 0
    private let log = Logger(subsystem: "dev.maho.browser", category: "FilterLists")
    private let stateLock = NSLock()
    private var isRunning = false

    /// Refreshes stale or never-fetched lists while the content blocker is on.
    /// Pass force to ignore the refresh interval (used when the user turns it on).
    func refreshIfNeeded(force: Bool = false) {
        let bridge = MahoBridge.shared
        guard (CoreSettingsSnapshot.values(from: bridge)["content_blocker"] as? Bool) == true else { return }
        stateLock.lock()
        if isRunning {
            stateLock.unlock()
            return
        }
        isRunning = true
        stateLock.unlock()

        let now = Date().timeIntervalSince1970
        let lists = currentLists().filter { list in
            guard list.enabled, URL(string: list.url)?.scheme == "https" else { return false }
            // Honour core's failure backoff so a broken list is not refetched on every foreground.
            if !force, let retryAt = list.nextRetryTimestamp, now < TimeInterval(retryAt) { return false }
            guard let last = list.lastSuccessTimestamp else { return true }
            return force || now - TimeInterval(last) >= Self.refreshInterval
        }
        guard !lists.isEmpty else {
            finish()
            return
        }

        Task.detached(priority: .utility) { [self] in
            var changed = false
            for list in lists {
                let response = await self.fetch(list)
                if self.apply(response) {
                    changed = self.compileAndInstall() || changed
                }
            }
            self.finish()
            if changed {
                await MainActor.run {
                    NotificationCenter.default.post(name: .mahoContentBlockerChanged, object: nil)
                }
            }
        }
    }

    private func finish() {
        stateLock.lock()
        isRunning = false
        stateLock.unlock()
        DispatchQueue.main.async {
            NotificationCenter.default.post(name: .mahoFilterListRefreshFinished, object: nil)
        }
    }

    /// True when an enabled list's last download attempt failed, so the
    /// settings footer can offer Try Again instead of "Downloading".
    func lastDownloadFailed() -> Bool {
        currentLists().contains { $0.enabled && $0.lastError != nil }
    }

    private func currentLists() -> [ListMeta] {
        guard let json = MahoBridge.shared.withCore({ ptr in
                  FFIString.consume(maho_core_get_filter_lists(ptr))
              }) ?? nil,
              let lists = try? JSONDecoder().decode([ListMeta].self, from: Data(json.utf8)) else {
            return []
        }
        return lists
    }

    private func fetch(_ list: ListMeta) async -> UpdateResponse {
        // A request that never produced an HTTP response is still reported so
        // core records the attempt. Connectivity errors use the transport code,
        // which core does not count toward the retry backoff; anything else
        // without a response engages the backoff (nextRetryTimestamp).
        func failure(_ statusCode: Int) -> UpdateResponse {
            UpdateResponse(
                listId: list.id, statusCode: statusCode, body: nil, etag: nil, lastModified: nil, sha256: nil
            )
        }
        guard let url = URL(string: list.url) else { return failure(Self.unreachableStatus) }
        var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 60)
        if let etag = list.etag { request.setValue(etag, forHTTPHeaderField: "If-None-Match") }
        if let modified = list.lastModified { request.setValue(modified, forHTTPHeaderField: "If-Modified-Since") }
        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            guard let http = response as? HTTPURLResponse else { return failure(Self.unreachableStatus) }
            return UpdateResponse(
                listId: list.id,
                statusCode: http.statusCode,
                body: http.statusCode == 200 ? String(decoding: data, as: UTF8.self) : nil,
                etag: http.value(forHTTPHeaderField: "ETag"),
                lastModified: http.value(forHTTPHeaderField: "Last-Modified"),
                sha256: nil
            )
        } catch {
            log.error("Filter list \(list.id, privacy: .public) fetch failed: \(error.localizedDescription, privacy: .public)")
            return failure(Self.isConnectivityError(error) ? Self.transportFailureStatus : Self.unreachableStatus)
        }
    }

    /// Errors that mean the device could not reach the network at all, which
    /// says nothing about the list itself.
    static func isConnectivityError(_ error: Error) -> Bool {
        guard let urlError = error as? URLError else { return false }
        switch urlError.code {
        case .notConnectedToInternet, .networkConnectionLost, .timedOut,
             .cannotFindHost, .cannotConnectToHost, .dnsLookupFailed,
             .internationalRoamingOff, .callIsActive, .dataNotAllowed,
             .cancelled:
            return true
        default:
            return false
        }
    }

    /// Returns true when core recorded a new candidate that needs compiling.
    private func apply(_ response: UpdateResponse) -> Bool {
        guard let payload = try? JSONEncoder().encode(response),
              let json = String(data: payload, encoding: .utf8),
              let resultJson = MahoBridge.shared.withCore({ ptr in
                  json.withCString { FFIString.consume(maho_core_apply_filter_list_update_result_json(ptr, $0)) }
              }) ?? nil,
              let result = try? JSONDecoder().decode(MutationResult.self, from: Data(resultJson.utf8)) else {
            return false
        }
        return result.success && (result.compileRequired ?? false)
    }

    /// Snapshot under the core lock, compile outside it, install under it.
    /// Core rejects an engine built from a stale generation, so a setting
    /// change during the compile cannot install outdated rules.
    private func compileAndInstall() -> Bool {
        guard let snapshot = MahoBridge.shared.withCore({ ptr in maho_core_create_compile_snapshot(ptr) }) ?? nil,
              let engine = maho_compile_engine_from_snapshot(snapshot) else {
            return false
        }
        // The install call consumes the handle even when it rejects it; free it
        // only when the core was gone and the call never ran.
        guard let ran = MahoBridge.shared.withCore({ ptr in
            FFIString.consume(maho_content_engine_install_result_json(ptr, engine))
        }) else {
            maho_content_engine_free(engine)
            return false
        }
        guard let resultJson = ran else { return false }
        return (try? JSONDecoder().decode(MutationResult.self, from: Data(resultJson.utf8)))?.success ?? false
    }
}
