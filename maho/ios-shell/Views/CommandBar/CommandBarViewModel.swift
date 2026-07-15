import Foundation
import SwiftUI
import UIKit

class CommandBarViewModel: ObservableObject, CommandBarUpdateHandler {
    @Published var suggestions: [SuggestionViewModel] = []

    func query(_ text: String, mode: String? = nil, isIncognito: Bool = false) {
        let updates = MahoBridge.shared.commandBarQuery(query: text, mode: mode, isIncognito: isIncognito)
        for update in updates {
            _ = MahoBridge.shared.processCommandBarUpdate(update, handler: self)
        }
    }

    func selectItem(at index: Int) {
        guard index < suggestions.count else { return }
        let suggestion = suggestions[index]
        _ = MahoBridge.shared.commandBarSelectItem(index: index, key: suggestion.key)
    }

    // MARK: - CommandBarUpdateHandler

    func didReceiveCommandBarResults(suggestions: [SuggestionViewModel]) {
        DispatchQueue.main.async {
            self.suggestions = suggestions
        }
    }

    func didReceiveRecentSearchesUpdated(searches: [String]) {
        // Not used for suggestions list
    }

    func didReceiveSearchEnginesUpdated(engines: [SearchEngineViewModel]) {
        // Not used for suggestions list
    }
}

extension ImageData {
    var uiImage: UIImage? {
        return UIImage(data: Data(data))
    }
}
