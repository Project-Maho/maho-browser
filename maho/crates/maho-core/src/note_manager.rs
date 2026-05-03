use std::collections::HashMap;

use maho_types::common::{DateTime, Url};
use maho_types::identifiers::{NoteId, TabId};
use maho_types::note::Note;
use maho_types::traits::shell_renderer::NoteViewModel;

pub struct NoteManager {
    notes: HashMap<NoteId, Note>,
}

impl Default for NoteManager {
    fn default() -> Self {
        Self::new()
    }
}

pub enum NoteExportFormat {
    Markdown,
    Html,
    Json,
}

fn escape_html(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
        .replace('\'', "&#39;")
}

impl NoteManager {
    pub fn new() -> Self {
        Self {
            notes: HashMap::new(),
        }
    }

    pub fn create_note(&mut self, linked_tab: Option<TabId>, content: String) -> Note {
        let now = DateTime::now();
        let note = Note {
            id: NoteId::generate(),
            linked_tab_id: linked_tab,
            linked_url: None,
            content,
            created_at: now.clone(),
            updated_at: now,
        };
        self.notes.insert(note.id.clone(), note.clone());
        note
    }

    pub fn update_note(&mut self, note_id: &NoteId, content: String) {
        if let Some(note) = self.notes.get_mut(note_id) {
            note.content = content;
            note.updated_at = DateTime::now();
        }
    }

    pub fn delete_note(&mut self, note_id: &NoteId) -> Option<Note> {
        self.notes.remove(note_id)
    }

    pub fn get_note(&self, note_id: &NoteId) -> Option<&Note> {
        self.notes.get(note_id)
    }

    pub fn get_all_notes(&self) -> Vec<&Note> {
        self.notes.values().collect()
    }

    pub fn get_notes_for_tab(&self, tab_id: &TabId) -> Vec<&Note> {
        self.notes
            .values()
            .filter(|note| note.linked_tab_id.as_ref() == Some(tab_id))
            .collect()
    }

    pub fn get_notes_for_url(&self, url: &str) -> Vec<&Note> {
        self.notes
            .values()
            .filter(|note| {
                note.linked_url
                    .as_ref()
                    .map(|u| u.as_ref() == url)
                    .unwrap_or(false)
            })
            .collect()
    }

    pub fn restore_note(&mut self, note: Note) {
        self.notes.insert(note.id.clone(), note);
    }

    pub fn search_notes(&self, query: &str) -> Vec<&Note> {
        let query_lower = query.to_lowercase();
        self.notes
            .values()
            .filter(|note| note.content.to_lowercase().contains(&query_lower))
            .collect()
    }

    pub fn link_note_to_tab(&mut self, note_id: &NoteId, tab_id: TabId) {
        if let Some(note) = self.notes.get_mut(note_id) {
            note.linked_tab_id = Some(tab_id);
            note.updated_at = DateTime::now();
        }
    }

    pub fn link_note_to_url(&mut self, note_id: &NoteId, url: String) {
        if let Some(note) = self.notes.get_mut(note_id) {
            note.linked_url = Some(Url::new(url));
            note.updated_at = DateTime::now();
        }
    }

    pub fn unlink_note_from_tab(&mut self, note_id: &NoteId) {
        if let Some(note) = self.notes.get_mut(note_id) {
            note.linked_tab_id = None;
            note.updated_at = DateTime::now();
        }
    }

    pub fn get_linked_tab_id(&self, note_id: &NoteId) -> Option<TabId> {
        self.notes
            .get(note_id)
            .and_then(|note| note.linked_tab_id.clone())
    }

    pub fn export_notes(&self, format: NoteExportFormat) -> String {
        match format {
            NoteExportFormat::Markdown => {
                let mut output = String::new();
                for note in self.notes.values() {
                    output.push_str("## Note\n\n");
                    output.push_str(&note.content);
                    output.push_str("\n\n---\n\n");
                }
                output
            }
            NoteExportFormat::Html => {
                let mut output = String::from("<html><body>");
                for note in self.notes.values() {
                    output.push_str("<article><h2>Note</h2><div>");
                    output.push_str(&escape_html(&note.content));
                    output.push_str("</div></article>");
                }
                output.push_str("</body></html>");
                output
            }
            NoteExportFormat::Json => {
                let notes: Vec<&Note> = self.notes.values().collect();
                serde_json::to_string_pretty(&notes).unwrap_or_default()
            }
        }
    }

    pub fn export_single_note(&self, note_id: &NoteId, format: NoteExportFormat) -> Option<String> {
        let note = self.notes.get(note_id)?;
        Some(match format {
            NoteExportFormat::Markdown => {
                format!("## Note\n\n{}\n\n---\n\n", note.content)
            }
            NoteExportFormat::Html => {
                format!(
                    "<html><body><article><h2>Note</h2><div>{}</div></article></body></html>",
                    escape_html(&note.content)
                )
            }
            NoteExportFormat::Json => serde_json::to_string_pretty(note).unwrap_or_default(),
        })
    }

    pub fn to_view_model(note: &Note) -> NoteViewModel {
        NoteViewModel {
            id: note.id.as_ref().to_string(),
            content: note.content.clone(),
            linked_url: note.linked_url.as_ref().map(|u| u.as_ref().to_string()),
        }
    }

    pub fn get_all_view_models(&self) -> Vec<NoteViewModel> {
        self.notes.values().map(Self::to_view_model).collect()
    }
}
