package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.NoteViewModel
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.support.MahoJson

object BridgeNotes {

    fun getAllNoteViewModels(): List<NoteViewModel> {
        val json = MahoBridge.getNoteViewModels() ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<NoteViewModel>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun createNewNote(content: String, linkedTab: String? = null): List<CoreUpdate> {
        return MahoBridge.sendEvent(
            ShellEvent.CreateNote(linkedTab = linkedTab, content = content),
        )
    }

    fun updateExistingNote(noteId: String, content: String): List<CoreUpdate> {
        return MahoBridge.sendEvent(
            ShellEvent.UpdateNote(noteId = noteId, content = content),
        )
    }

    fun deleteExistingNote(noteId: String): List<CoreUpdate> {
        return MahoBridge.sendEvent(ShellEvent.DeleteNote(noteId = noteId))
    }

    fun linkNoteToTab(noteId: String, tabId: String) {
        MahoBridge.linkNoteToTab(noteId, tabId)
    }

    fun linkNoteToUrl(noteId: String, url: String) {
        MahoBridge.linkNoteToUrl(noteId, url)
    }

    fun unlinkNoteFromTab(noteId: String) {
        MahoBridge.unlinkNoteFromTab(noteId)
    }

    fun searchNotesFullText(query: String): List<NoteViewModel> {
        val json = MahoBridge.searchNotesFts(query) ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<NoteViewModel>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun exportAllNotes(format: String = "markdown"): String? {
        return MahoBridge.exportNotes(format)
    }

    fun exportSingleNoteById(noteId: String, format: String = "markdown"): String? {
        return MahoBridge.exportSingleNote(noteId, format)
    }
}
