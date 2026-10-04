import Foundation

extension MahoBridge {

    func getNoteViewModels() -> [NoteViewModel] {
        (withCore { ptr in
            FFIString.consumeJSON(maho_core_get_note_view_models(ptr))
        } ?? nil) ?? []
    }

    func createNote(content: String, linkedTab: TabId? = nil) {
        sendEvent(.createNote(linkedTab: linkedTab, content: content))
    }

    func updateNote(id: NoteId, content: String) {
        sendEvent(.updateNote(noteId: id, content: content))
    }

    func deleteNote(id: NoteId) {
        sendEvent(.deleteNote(noteId: id))
    }

    func linkNoteToTab(noteId: NoteId, tabId: TabId) {
        withCore { ptr in
            noteId.withCString { cNoteId in
                tabId.withCString { cTabId in
                    maho_core_link_note_to_tab(ptr, cNoteId, cTabId)
                }
            }
        }
    }

    func linkNoteToUrl(noteId: NoteId, url: String) {
        withCore { ptr in
            noteId.withCString { cNoteId in
                url.withCString { cUrl in
                    maho_core_link_note_to_url(ptr, cNoteId, cUrl)
                }
            }
        }
    }

    func unlinkNoteFromTab(noteId: NoteId) {
        withCore { ptr in
            noteId.withCString { cNoteId in
                maho_core_unlink_note_from_tab(ptr, cNoteId)
            }
        }
    }

    func searchNotes(query: String) -> [NoteViewModel] {
        (withCore { ptr in
            query.withCString { cQuery in
                FFIString.consumeJSON(maho_core_search_notes_fts(ptr, cQuery))
            }
        } ?? nil) ?? []
    }

    func exportNotes(format: String = "markdown") -> String? {
        withCore { ptr in
            format.withCString { cFormat in
                FFIString.consume(maho_core_export_notes(ptr, cFormat))
            }
        } ?? nil
    }

    func exportSingleNote(id: NoteId, format: String = "markdown") -> String? {
        withCore { ptr in
            id.withCString { cId in
                format.withCString { cFormat in
                    FFIString.consume(maho_core_export_single_note(ptr, cId, cFormat))
                }
            }
        } ?? nil
    }

    func handleNoteUpdate(_ update: CoreUpdate) {
        switch update {
        case .settingsChanged:
            break
        default:
            break
        }
    }
}
