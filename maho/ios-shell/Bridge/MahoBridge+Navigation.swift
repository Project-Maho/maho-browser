import Foundation

extension MahoBridge {

    func navigate(tabId: TabId, url: Url) {
        sendEvent(.navigateTo(tabId: tabId, url: url))
    }

    func goBack(tabId: TabId) {
        sendEvent(.goBack(tabId: tabId))
    }

    func goForward(tabId: TabId) {
        sendEvent(.goForward(tabId: tabId))
    }

    func reload(tabId: TabId) {
        sendEvent(.reload(tabId: tabId))
    }

    func stop(tabId: TabId) {
        sendEvent(.stop(tabId: tabId))
    }

    func setZoom(tabId: TabId, level: Double) {
        sendEvent(.setZoom(tabId: tabId, zoomLevel: level))
    }

    @discardableResult
    func processNavigationUpdate(_ update: CoreUpdate, handler: NavigationUpdateHandler) -> Bool {
        switch update {
        case .navigateTab(let tabId, let url):
            handler.didReceiveNavigateTab(tabId: tabId, url: url)
            return true
        case .navigationStateChanged(let tabId, let url, let title, let canGoBack, let canGoForward, let isLoading, let progress):
            handler.didReceiveNavigationStateChanged(
                tabId: tabId,
                url: url,
                title: title,
                canGoBack: canGoBack,
                canGoForward: canGoForward,
                isLoading: isLoading,
                progress: progress
            )
            return true
        case .zoomChanged(let tabId, let zoomLevel):
            handler.didReceiveZoomChanged(tabId: tabId, zoomLevel: zoomLevel)
            return true
        default:
            return false
        }
    }
}

protocol NavigationUpdateHandler: AnyObject {
    func didReceiveNavigateTab(tabId: TabId, url: Url)
    func didReceiveNavigationStateChanged(
        tabId: TabId,
        url: Url,
        title: String,
        canGoBack: Bool,
        canGoForward: Bool,
        isLoading: Bool,
        progress: Double
    )
    func didReceiveZoomChanged(tabId: TabId, zoomLevel: Double)
}
