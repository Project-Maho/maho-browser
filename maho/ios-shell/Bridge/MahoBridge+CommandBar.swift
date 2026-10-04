import Foundation

extension MahoBridge {

    // MARK: - Command Bar Actions

    /// Sends a command bar query to the core for ranked results.
    @discardableResult
    func commandBarQuery(query: String, mode: String? = nil, isIncognito: Bool = false) -> [CoreUpdate] {
        handleEvent(.commandBarQuery(text: query, mode: mode, isIncognito: isIncognito))
    }

    /// Selects a command bar item by index.
    @discardableResult
    func commandBarSelectItem(index: Int, key: String) -> [CoreUpdate] {
        handleEvent(.commandBarSelect(index: index, key: key))
    }

    /// Executes a quick action from the command bar.
    @discardableResult
    func commandBarAction(action: QuickAction) -> [CoreUpdate] {
        handleEvent(.commandBarAction(action: action))
    }

    /// Saves a search query to recent searches.
    func saveSearch(query: String) {
        sendEvent(.saveSearch(query: query))
    }

    /// Adds a search engine.
    @discardableResult
    func addSearchEngine(engine: SearchEngineModel) -> [CoreUpdate] {
        handleEvent(.addSearchEngine(engine: engine))
    }

    /// Removes a search engine by ID.
    @discardableResult
    func removeSearchEngine(id: String) -> [CoreUpdate] {
        handleEvent(.removeSearchEngine(id: id))
    }

    /// Sets the default search engine.
    @discardableResult
    func setDefaultSearchEngine(id: String) -> [CoreUpdate] {
        handleEvent(.setDefaultSearchEngine(id: id))
    }

    /// Retrieves recent searches from the core.
    func getRecentSearches() -> [String]? {
        withCore { ptr in
            FFIString.consumeJSON(maho_core_get_recent_searches(ptr))
        } ?? nil
    }

    /// Records usage of a command bar item for frecency ranking.
    func recordUsage(itemId: String) {
        withCore { ptr in
            itemId.withCString { cStr in
                maho_core_record_usage(ptr, cStr)
            }
        }
    }

    // MARK: - CoreUpdate Processing

    /// Processes command-bar-related CoreUpdates and returns true if handled.
    @discardableResult
    func processCommandBarUpdate(_ update: CoreUpdate, handler: CommandBarUpdateHandler) -> Bool {
        switch update {
        case .commandBarResults(let suggestions):
            handler.didReceiveCommandBarResults(suggestions: suggestions)
            return true
        case .recentSearchesUpdated(let searches):
            handler.didReceiveRecentSearchesUpdated(searches: searches)
            return true
        case .searchEnginesUpdated(let engines):
            handler.didReceiveSearchEnginesUpdated(engines: engines)
            return true
        default:
            return false
        }
    }
}

// MARK: - CommandBarUpdateHandler Protocol

protocol CommandBarUpdateHandler: AnyObject {
    func didReceiveCommandBarResults(suggestions: [SuggestionViewModel])
    func didReceiveRecentSearchesUpdated(searches: [String])
    func didReceiveSearchEnginesUpdated(engines: [SearchEngineViewModel])
}
