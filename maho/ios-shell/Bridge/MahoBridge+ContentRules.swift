import Foundation
import WebKit
import os.log

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
        
        var isEnabled = false
        if let vm = getSettings() {
            for section in vm.sections {
                for item in section.items {
                    if item.key == "privacy.contentBlockerEnabled" {
                        isEnabled = (item.value.value as? Bool) ?? false
                    }
                }
            }
        }
        guard isEnabled else { return }
        
        if let url = webView.url, let host = url.host, isDomainWhitelisted(host) {
            return
        }

        guard let jsonRules: String = withCore({ ptr -> String? in
            guard let raw = maho_core_get_content_rules(ptr) else { return nil }
            return String(cString: raw)
        }) ?? nil else { return }
        compileContentRules(json: jsonRules) { [weak self] ruleList in
            guard let ruleList else { return }
            self?.applyContentRules(ruleList, to: webView)
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
