use maho_types::passwords::SavedPassword;

pub struct PasswordManager {
    passwords: Vec<SavedPassword>,
}

impl PasswordManager {
    pub fn new() -> Self {
        Self {
            passwords: Vec::new(),
        }
    }

    pub fn list_passwords(&self) -> &[SavedPassword] {
        &self.passwords
    }

    pub fn password_by_id(&self, id: &str) -> Option<&SavedPassword> {
        self.passwords.iter().find(|p| p.id == id)
    }

    pub fn search_passwords(&self, query: &str) -> Vec<&SavedPassword> {
        let q = query.to_lowercase();
        self.passwords
            .iter()
            .filter(|p| {
                p.domain.to_lowercase().contains(&q) || p.username.to_lowercase().contains(&q)
            })
            .collect()
    }

    pub fn delete_password(&mut self, id: &str) -> bool {
        let len_before = self.passwords.len();
        self.passwords.retain(|p| p.id != id);
        self.passwords.len() < len_before
    }

    pub fn add_password(
        &mut self,
        domain: String,
        username: String,
        password: Option<String>,
    ) -> &SavedPassword {
        let id = uuid::Uuid::new_v4().to_string();
        let now = chrono::Utc::now().to_rfc3339();
        let password = SavedPassword {
            id,
            domain,
            username,
            created_at: now,
            last_used: None,
            password,
        };
        self.passwords.push(password);
        self.passwords.last().unwrap()
    }

    pub fn update_username(&mut self, id: &str, username: String) -> bool {
        if let Some(p) = self.passwords.iter_mut().find(|p| p.id == id) {
            p.username = username;
            true
        } else {
            false
        }
    }

    pub fn restore_password(&mut self, password: SavedPassword) {
        self.passwords.push(password);
    }
}

impl Default for PasswordManager {
    fn default() -> Self {
        Self::new()
    }
}
