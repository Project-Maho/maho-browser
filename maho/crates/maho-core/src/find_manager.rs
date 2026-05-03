use maho_types::identifiers::TabId;
use maho_types::traits::shell_renderer::FindBarState;

#[derive(Clone, Debug, serde::Serialize)]
pub struct FindSession {
    pub tab_id: TabId,
    pub query: String,
    pub match_count: u32,
    pub active_index: u32,
    pub case_sensitive: bool,
    pub whole_word: bool,
}

pub struct FindManager {
    active_find: Option<FindSession>,
}

impl FindManager {
    pub fn new() -> Self {
        Self { active_find: None }
    }

    pub fn start_find(
        &mut self,
        tab_id: TabId,
        query: String,
        case_sensitive: bool,
        whole_word: bool,
    ) -> FindSession {
        let session = FindSession {
            tab_id,
            query,
            match_count: 0,
            active_index: 0,
            case_sensitive,
            whole_word,
        };
        self.active_find = Some(session.clone());
        session
    }

    pub fn next_match(&mut self) -> Option<u32> {
        let session = self.active_find.as_mut()?;
        if session.match_count == 0 {
            return None;
        }
        session.active_index = (session.active_index + 1) % session.match_count;
        Some(session.active_index)
    }

    pub fn previous_match(&mut self) -> Option<u32> {
        let session = self.active_find.as_mut()?;
        if session.match_count == 0 {
            return None;
        }
        if session.active_index == 0 {
            session.active_index = session.match_count - 1;
        } else {
            session.active_index -= 1;
        }
        Some(session.active_index)
    }

    pub fn update_results(&mut self, match_count: u32) {
        if let Some(session) = self.active_find.as_mut() {
            session.match_count = match_count;
            if match_count == 0 || session.active_index >= match_count {
                session.active_index = 0;
            }
        }
    }

    pub fn dismiss_find(&mut self) {
        self.active_find = None;
    }

    pub fn get_active_session(&self) -> Option<&FindSession> {
        self.active_find.as_ref()
    }

    pub fn to_find_bar_state(&self) -> Option<FindBarState> {
        self.active_find.as_ref().map(|session| FindBarState {
            query: session.query.clone(),
            match_count: session.match_count,
            active_index: session.active_index,
            is_visible: true,
        })
    }
}

impl Default for FindManager {
    fn default() -> Self {
        Self::new()
    }
}
