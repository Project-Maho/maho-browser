import Foundation
import Combine

/// Test-only live-site agentic journey runner for physical-device E2E.
///
/// Drives the production browser-tool pipeline — `AgenticBrowsingDOM.invoke`,
/// the exact entry the Rust maho-agent loop calls through
/// `agentBrowserToolCallback` — against a real live website loaded in the
/// user-visible browsing WKWebView. Perception (get_page_snapshot), form
/// fills (fill_input), clicks (click_element), and final verification all
/// mutate and read the real page over the network. The LLM planning layer is
/// NOT exercised (no BYOK key in the test environment); the tool plan is
/// scripted here while every tool action runs through the real pipeline.
final class E2EAgenticJourney: ObservableObject {
    static let shared = E2EAgenticJourney()
    static let envKey = "MAHO_E2E_AGENTIC_JOURNEY"
    static let targetSite = "https://www.saucedemo.com/"
    static let isArmed: Bool = ProcessInfo.processInfo.environment[envKey] == "1"

    @Published private(set) var status: String = "idle"
    @Published private(set) var steps: [String] = []

    private let lock = NSLock()
    private var started = false

    private func setStatus(_ next: String) {
        DispatchQueue.main.async { self.status = next }
    }

    private func record(_ step: String) {
        DispatchQueue.main.async { self.steps.append(step) }
    }

    private func fail(_ stage: String, _ detail: String) {
        record("error:\(stage):\(detail)")
        setStatus("failed:\(stage)")
    }

    func armAndStartIfRequested() {
        guard Self.isArmed else { return }
        lock.lock()
        defer { lock.unlock() }
        guard !started else { return }
        started = true
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            self?.run()
        }
    }

    // MARK: - Tool plumbing (production path)

    private typealias Snapshot = (url: String, elements: [[String: Any]])

    private func tool(_ name: String, _ args: [String: Any] = [:]) -> [String: Any] {
        AgenticBrowsingDOM.invoke(name: name, args: args)
    }

    private func snapshot() -> Snapshot? {
        let result = tool("get_page_snapshot")
        guard result["ok"] as? Bool == true,
              let payload = result["result"] as? [String: Any],
              let url = payload["url"] as? String,
              let elements = payload["elements"] as? [[String: Any]] else {
            return nil
        }
        return (url, elements)
    }

    private func waitForSnapshot(
        timeout: TimeInterval,
        where predicate: (Snapshot) -> Bool
    ) -> Snapshot? {
        var reportedErrors = Set<String>()
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if let snap = snapshot(), predicate(snap) {
                return snap
            }
            if reportedErrors.count < 6, let code = lastSnapshotErrorCode, !reportedErrors.contains(code) {
                reportedErrors.insert(code)
                record("snapshot_error:\(code)")
            }
            Thread.sleep(forTimeInterval: 1.0)
        }
        return nil
    }

    private var lastSnapshotErrorCode: String? {
        let result = tool("get_page_snapshot")
        if result["ok"] as? Bool == true { return nil }
        if let code = result["error"] as? String { return code }
        return "ok_without_result"
    }

    private func elementMatches(
        _ element: [String: Any],
        labelContains: String?,
        type: String?,
        hrefContains: String?
    ) -> Bool {
        if let type, (element["type"] as? String)?.lowercased() != type.lowercased() { return false }
        if let hrefContains, !((element["href"] as? String) ?? "").lowercased().contains(hrefContains.lowercased()) { return false }
        if let labelContains {
            let label = ((element["label"] as? String) ?? "") + " " + ((element["text"] as? String) ?? "")
            if !label.lowercased().contains(labelContains.lowercased()) { return false }
        }
        return true
    }

    /// Finds a visible element, scrolling the real page when it sits below the fold.
    private func findElement(
        labelContains: String? = nil,
        type: String? = nil,
        hrefContains: String? = nil
    ) -> (element: [String: Any], snap: Snapshot)? {
        for attempt in 0..<8 {
            if let snap = snapshot(),
               let element = snap.elements.first(where: {
                   elementMatches($0, labelContains: labelContains, type: type, hrefContains: hrefContains)
               }) {
                return (element, snap)
            }
            guard attempt < 7 else { break }
            _ = tool("scroll_page", ["direction": attempt < 4 ? "down" : "up"])
            Thread.sleep(forTimeInterval: 0.7)
        }
        return nil
    }

    private func fill(_ labelContains: String, text: String) -> Bool {
        guard let found = findElement(labelContains: labelContains, type: nil) else { return false }
        let result = tool("fill_input", ["id": found.element["id"] ?? 0, "text": text])
        let ok = result["ok"] as? Bool == true
        record("filled:\(labelContains):\(ok ? "ok" : String(describing: result["error"] ?? "?"))")
        return ok
    }

    private func clickSelector(_ stage: String, selector: String) -> Bool {
        var lastError = "?"
        for attempt in 0..<4 {
            let result = tool("click_element", ["selector": selector])
            if result["ok"] as? Bool == true {
                record("clicked:\(stage):ok")
                return true
            }
            lastError = (result["error"] as? String) ?? "?"
            if lastError == "element_not_interactable" {
                _ = tool("scroll_page", ["direction": "up", "amount": 10_000])
                Thread.sleep(forTimeInterval: 1.2)
            } else {
                break
            }
        }
        record("clicked:\(stage):\(lastError)")
        return false
    }

    private func click(
        _ stage: String,
        labelContains: String? = nil,
        type: String? = nil,
        hrefContains: String? = nil
    ) -> Bool {
        guard let found = findElement(labelContains: labelContains, type: type, hrefContains: hrefContains) else {
            record("clicked:\(stage):element_not_found")
            return false
        }
        let result = tool("click_element", ["id": found.element["id"] ?? 0])
        let ok = result["ok"] as? Bool == true
        record("clicked:\(stage):\(ok ? "ok" : String(describing: result["error"] ?? "?"))")
        return ok
    }

    private func waitForNavigation(_ urlFragment: String, timeout: TimeInterval = 20) -> Bool {
        let snap = waitForSnapshot(timeout: timeout) { $0.url.contains(urlFragment) }
        if let snap {
            record("nav:\(urlFragment):\(snap.url.prefix(120))")
            return true
        }
        return false
    }

    // MARK: - Journey

    private func run() {
        setStatus("running:waiting_page")
        guard let initial = waitForSnapshot(timeout: 90, where: { $0.url.contains("saucedemo.com") }) else {
            let probe = AgenticBrowsingPageRegistry.shared.evaluate(
                "(function(){return JSON.stringify({href:location.href,readyState:document.readyState,title:(document.title||'').slice(0,80)})})();"
            )
            switch probe {
            case .success(let json):
                record("page_probe:\(json.prefix(300))")
            case .failure(let code, let message):
                record("page_probe_failed:\(code):\(message ?? "")")
            }
            fail("waiting_page", "live page never became interactive")
            return
        }
        record("page_loaded:\(initial.url)")

        setStatus("running:login")
        guard fill("Username", text: "standard_user"),
              fill("Password", text: "secret_sauce"),
              click("login", type: "submit") || click("login", labelContains: "login") else {
            fail("login", "could not drive login form")
            return
        }
        guard waitForNavigation("inventory") else {
            fail("login", "real login did not navigate to inventory")
            return
        }

        setStatus("running:inventory")
        guard click("add_to_cart", labelContains: "add to cart") else {
            fail("inventory", "no add-to-cart control found on live page")
            return
        }

        setStatus("running:cart")
        guard clickSelector("cart_link", selector: "a.shopping_cart_link") || click("cart_link", hrefContains: "cart.html") else {
            fail("cart", "cart link not found")
            return
        }
        guard waitForNavigation("cart.html") else {
            fail("cart", "did not navigate to cart")
            return
        }

        guard click("checkout", labelContains: "checkout") else {
            fail("cart", "checkout button not found")
            return
        }
        guard waitForNavigation("checkout-step-one") else {
            fail("cart", "did not reach checkout form")
            return
        }

        setStatus("running:checkout_form")
        guard fill("First Name", text: "Maho"),
              fill("Last Name", text: "Shopper"),
              fill("Zip", text: "04524"),
              click("continue", type: "submit") else {
            fail("checkout_form", "could not fill or submit checkout form")
            return
        }
        guard waitForNavigation("checkout-step-two") else {
            fail("checkout_form", "did not reach overview")
            return
        }

        setStatus("running:confirm")
        guard click("finish", labelContains: "finish") else {
            fail("confirm", "finish button not found")
            return
        }
        guard waitForNavigation("checkout-complete") else {
            fail("confirm", "order was not completed")
            return
        }

        let read = AgenticBrowsingPageRegistry.shared.evaluate(
            "(function(){return JSON.stringify({text:(document.body.innerText||'').slice(0,4000)})})();"
        )
        var thanked = false
        if case .success(let json) = read,
           let data = json.data(using: .utf8),
           let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
           let text = object["text"] as? String {
            thanked = text.lowercased().contains("thank you for your order")
        }
        record("verified:order_complete:\(thanked ? "yes" : "no")")
        if thanked {
            setStatus("completed")
        } else {
            fail("confirm", "confirmation text missing")
        }
    }
}
