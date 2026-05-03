import Foundation

extension MahoBridge {
    @discardableResult
    func startFind(tabId: TabId, query: String, caseSensitive: Bool = false, wholeWord: Bool = false) -> FindBarState? {
        withCore { ptr -> FindBarState? in
            tabId.withCString { tabIdCStr -> FindBarState? in
                query.withCString { queryCStr -> FindBarState? in
                    let result = maho_core_start_find(ptr, tabIdCStr, queryCStr, caseSensitive, wholeWord)
                    return FFIString.consumeJSON(result)
                }
            }
        } ?? nil
    }

    func findNext() -> FindBarState? {
        (withCore { ptr in
            FFIString.consumeJSON(maho_core_find_next(ptr))
        } ?? nil) as FindBarState?
    }

    func findPrevious() -> FindBarState? {
        (withCore { ptr in
            FFIString.consumeJSON(maho_core_find_previous(ptr))
        } ?? nil) as FindBarState?
    }

    func dismissFind() {
        withCore { ptr in
            maho_core_dismiss_find(ptr)
        }
    }

    func getFindBarState() -> FindBarState? {
        (withCore { ptr in
            FFIString.consumeJSON(maho_core_get_find_bar_state(ptr))
        } ?? nil) as FindBarState?
    }
}
