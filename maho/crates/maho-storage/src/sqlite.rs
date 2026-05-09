use rusqlite::Connection;

use crate::StorageError;

type BookmarkRecord = (String, String, String, Option<String>, String);
type PasswordRecord = (String, String, String, String, Option<String>, Option<String>);
type AutofillAddressRecord = (
    String,
    String,
    String,
    String,
    String,
    String,
    String,
    Option<String>,
    Option<String>,
);
type BookmarkFolderRecord = (String, String, Option<String>, String);

pub struct AutofillAddressParams<'a> {
    pub id: &'a str,
    pub name: &'a str,
    pub street: &'a str,
    pub city: &'a str,
    pub state: &'a str,
    pub zip: &'a str,
    pub country: &'a str,
    pub phone: Option<&'a str>,
    pub email: Option<&'a str>,
}

/// SQLite storage for persistent data (history, bookmarks, settings)
pub struct SqliteStorage {
    conn: Connection,
}

impl SqliteStorage {
    pub fn open(path: &str) -> Result<Self, StorageError> {
        let conn = Connection::open(path)?;
        let storage = Self { conn };
        storage.initialize_schema()?;
        storage.run_migrations()?;
        Ok(storage)
    }

    pub fn open_in_memory() -> Result<Self, StorageError> {
        let conn = Connection::open_in_memory()?;
        let storage = Self { conn };
        storage.initialize_schema()?;
        storage.run_migrations()?;
        Ok(storage)
    }

    fn run_migrations(&self) -> Result<(), StorageError> {
        // NOTE: password stored as plaintext TEXT column. Encryption at rest is a future improvement.
        let _ = self.conn.execute("ALTER TABLE passwords ADD COLUMN password TEXT", []);
        let _ = self.conn.execute(
            "ALTER TABLE shortcuts ADD COLUMN enabled INTEGER NOT NULL DEFAULT 1",
            [],
        );
        Ok(())
    }

    fn initialize_schema(&self) -> Result<(), StorageError> {
        self.conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                url TEXT NOT NULL,
                title TEXT NOT NULL,
                visited_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS bookmarks (
                id TEXT PRIMARY KEY,
                url TEXT NOT NULL,
                title TEXT NOT NULL,
                folder_id TEXT,
                created_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS settings (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS notes (
                id TEXT PRIMARY KEY,
                linked_tab_id TEXT,
                linked_url TEXT,
                content TEXT NOT NULL,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS boosts (
                id TEXT PRIMARY KEY,
                url_pattern TEXT NOT NULL,
                pattern_type TEXT NOT NULL DEFAULT 'glob',
                custom_css TEXT,
                custom_js TEXT,
                color_theme TEXT,
                font_override TEXT,
                enabled INTEGER NOT NULL DEFAULT 1,
                space_scope TEXT
            );
            CREATE TABLE IF NOT EXISTS css_mods (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                description TEXT,
                author TEXT,
                version TEXT,
                css TEXT NOT NULL,
                enabled INTEGER NOT NULL DEFAULT 1,
                homepage TEXT,
                source_url TEXT,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS permissions (
                origin TEXT NOT NULL,
                permission TEXT NOT NULL,
                policy TEXT NOT NULL DEFAULT 'ask',
                PRIMARY KEY (origin, permission)
            );
            CREATE TABLE IF NOT EXISTS atc_rules (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                space_id TEXT,
                url_pattern TEXT,
                max_age_hours INTEGER,
                max_tabs INTEGER,
                enabled INTEGER NOT NULL DEFAULT 1
            );
            CREATE TABLE IF NOT EXISTS search_history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                query TEXT NOT NULL,
                searched_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS per_site_zoom (
                origin TEXT PRIMARY KEY,
                zoom_level REAL NOT NULL DEFAULT 1.0
            );
            CREATE TABLE IF NOT EXISTS downloads (
                id TEXT PRIMARY KEY,
                filename TEXT NOT NULL,
                url TEXT NOT NULL,
                total_bytes INTEGER NOT NULL DEFAULT 0,
                received_bytes INTEGER NOT NULL DEFAULT 0,
                state TEXT NOT NULL DEFAULT 'downloading',
                file_path TEXT,
                started_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS bookmark_folders (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                parent_id TEXT,
                created_at TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_history_url ON history(url);
            CREATE INDEX IF NOT EXISTS idx_history_visited ON history(visited_at);
            CREATE INDEX IF NOT EXISTS idx_notes_linked_tab ON notes(linked_tab_id);
            CREATE INDEX IF NOT EXISTS idx_notes_linked_url ON notes(linked_url);
            CREATE INDEX IF NOT EXISTS idx_boosts_url_pattern ON boosts(url_pattern);
            CREATE INDEX IF NOT EXISTS idx_search_history_searched ON search_history(searched_at);
            CREATE INDEX IF NOT EXISTS idx_downloads_state ON downloads(state);
            CREATE TABLE IF NOT EXISTS usage_stats (
                item_key TEXT PRIMARY KEY,
                item_type TEXT NOT NULL,
                usage_count INTEGER NOT NULL DEFAULT 0,
                last_used_at TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_usage_stats_type ON usage_stats(item_type);
            CREATE INDEX IF NOT EXISTS idx_usage_stats_count ON usage_stats(usage_count DESC);
            CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5(note_id, content, tokenize='porter unicode61');
            CREATE TABLE IF NOT EXISTS profiles (
                id TEXT PRIMARY KEY,
                data TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS passwords (
                id TEXT PRIMARY KEY,
                domain TEXT NOT NULL,
                username TEXT NOT NULL,
                created_at TEXT NOT NULL,
                last_used TEXT
            );
            CREATE INDEX IF NOT EXISTS idx_passwords_domain ON passwords(domain);
            CREATE TABLE IF NOT EXISTS autofill_addresses (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                street TEXT NOT NULL,
                city TEXT NOT NULL,
                state TEXT NOT NULL,
                zip TEXT NOT NULL,
                country TEXT NOT NULL,
                phone TEXT,
                email TEXT
            );
            CREATE TABLE IF NOT EXISTS autofill_payments (
                id TEXT PRIMARY KEY,
                card_name TEXT NOT NULL,
                last_four TEXT NOT NULL,
                expiry TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS reading_list (
                id TEXT PRIMARY KEY,
                url TEXT NOT NULL,
                title TEXT NOT NULL,
                excerpt TEXT,
                site_name TEXT,
                favicon_url TEXT,
                preview_image_url TEXT,
                added_at TEXT NOT NULL,
                read_at TEXT,
                is_read INTEGER NOT NULL DEFAULT 0,
                estimated_read_minutes INTEGER,
                tags TEXT
            );
            CREATE INDEX IF NOT EXISTS idx_reading_list_added ON reading_list(added_at);
            CREATE TABLE IF NOT EXISTS search_engines (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                url_template TEXT NOT NULL,
                shortcut TEXT,
                icon_url TEXT,
                is_default INTEGER NOT NULL DEFAULT 0
            );
            CREATE TABLE IF NOT EXISTS shortcuts (
                action TEXT PRIMARY KEY,
                key_combo_json TEXT NOT NULL,
                is_custom INTEGER NOT NULL DEFAULT 1,
                enabled INTEGER NOT NULL DEFAULT 1
            );
            CREATE TABLE IF NOT EXISTS easels (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                canvas_items_json TEXT NOT NULL DEFAULT '[]',
                viewport_json TEXT NOT NULL DEFAULT '{}',
                created_at TEXT NOT NULL DEFAULT (datetime('now')),
                updated_at TEXT NOT NULL DEFAULT (datetime('now'))
            );
            CREATE TABLE IF NOT EXISTS sync_queue (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                entity_type TEXT NOT NULL,
                entity_id TEXT NOT NULL,
                version INTEGER NOT NULL,
                modified_at INTEGER NOT NULL,
                payload_json TEXT NOT NULL,
                deleted INTEGER NOT NULL DEFAULT 0,
                created_at INTEGER NOT NULL,
                sent_at INTEGER
            );
            CREATE TABLE IF NOT EXISTS sync_state (
                entity_type TEXT PRIMARY KEY,
                last_pulled_version INTEGER NOT NULL DEFAULT 0
            );
            CREATE TABLE IF NOT EXISTS shared_collections (
                id TEXT PRIMARY KEY,
                space_id TEXT NOT NULL,
                name TEXT NOT NULL,
                permission TEXT NOT NULL DEFAULT 'view',
                member_count INTEGER NOT NULL DEFAULT 1,
                share_link TEXT,
                created_at TEXT NOT NULL DEFAULT (datetime('now'))
            );",
        )?;
        Ok(())
    }

    // === History ===

    pub fn add_history_entry(&self, url: &str, title: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO history (url, title, visited_at) VALUES (?1, ?2, datetime('now'))",
            rusqlite::params![url, title],
        )?;
        Ok(())
    }

    pub fn search_history(
        &self,
        query: &str,
        limit: usize,
    ) -> Result<Vec<(String, String, String)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT url, title, visited_at FROM history WHERE title LIKE ?1 OR url LIKE ?1 ORDER BY visited_at DESC LIMIT ?2",
        )?;
        let pattern = format!("%{query}%");
        let rows = stmt.query_map(rusqlite::params![pattern, limit], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn clear_history(&self) -> Result<(), StorageError> {
        self.conn.execute("DELETE FROM history", [])?;
        Ok(())
    }

    pub fn delete_history_entry(&self, entry_id: i64) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM history WHERE id = ?1",
            rusqlite::params![entry_id],
        )?;
        Ok(affected > 0)
    }

    pub fn delete_history_entry_by_url(&self, url: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM history WHERE url = ?1", rusqlite::params![url])?;
        Ok(affected > 0)
    }

    // === Bookmarks ===

    pub fn save_bookmark(
        &self,
        id: &str,
        url: &str,
        title: &str,
        folder_id: Option<&str>,
        created_at: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO bookmarks (id, url, title, folder_id, created_at) VALUES (?1, ?2, ?3, ?4, ?5)",
            rusqlite::params![id, url, title, folder_id, created_at],
        )?;
        Ok(())
    }

    pub fn delete_bookmark(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM bookmarks WHERE id = ?1", rusqlite::params![id])?;
        Ok(affected > 0)
    }

    pub fn search_bookmarks(
        &self,
        query: &str,
    ) -> Result<Vec<BookmarkRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, url, title, folder_id, created_at FROM bookmarks WHERE title LIKE ?1 OR url LIKE ?1 ORDER BY created_at DESC",
        )?;
        let pattern = format!("%{query}%");
        let rows = stmt.query_map(rusqlite::params![pattern], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, Option<String>>(3)?,
                row.get::<_, String>(4)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn get_all_bookmarks(
        &self,
    ) -> Result<Vec<BookmarkRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, url, title, folder_id, created_at FROM bookmarks ORDER BY created_at DESC",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, Option<String>>(3)?,
                row.get::<_, String>(4)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn move_bookmark(&self, id: &str, folder_id: Option<&str>) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "UPDATE bookmarks SET folder_id = ?1 WHERE id = ?2",
            rusqlite::params![folder_id, id],
        )?;
        Ok(affected > 0)
    }

    pub fn save_bookmark_folder(
        &self,
        id: &str,
        name: &str,
        parent_id: Option<&str>,
        created_at: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO bookmark_folders (id, name, parent_id, created_at) VALUES (?1, ?2, ?3, ?4)",
            rusqlite::params![id, name, parent_id, created_at],
        )?;
        Ok(())
    }

    // === Settings ===

    pub fn get_setting(&self, key: &str) -> Result<Option<String>, StorageError> {
        let mut stmt = self
            .conn
            .prepare("SELECT value FROM settings WHERE key = ?1")?;
        let mut rows = stmt.query(rusqlite::params![key])?;
        match rows.next()? {
            Some(row) => Ok(Some(row.get(0)?)),
            None => Ok(None),
        }
    }

    pub fn set_setting(&self, key: &str, value: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO settings (key, value) VALUES (?1, ?2)",
            rusqlite::params![key, value],
        )?;
        Ok(())
    }

    // === Profiles ===

    pub fn save_profile(&self, id: &str, data: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO profiles (id, data) VALUES (?1, ?2)",
            rusqlite::params![id, data],
        )?;
        Ok(())
    }

    pub fn get_all_profiles(&self) -> Result<Vec<String>, StorageError> {
        let mut stmt = self.conn.prepare("SELECT data FROM profiles")?;
        let rows = stmt.query_map([], |row| row.get::<_, String>(0))?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_profile(&self, id: &str) -> Result<(), StorageError> {
        self.conn
            .execute("DELETE FROM profiles WHERE id = ?1", rusqlite::params![id])?;
        Ok(())
    }

    // === Notes ===

    pub fn save_note(
        &self,
        id: &str,
        linked_tab_id: Option<&str>,
        linked_url: Option<&str>,
        content: &str,
        created_at: &str,
        updated_at: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO notes (id, linked_tab_id, linked_url, content, created_at, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
            rusqlite::params![id, linked_tab_id, linked_url, content, created_at, updated_at],
        )?;
        self.index_note_fts(id, content)?;
        Ok(())
    }

    #[allow(clippy::type_complexity)]
    pub fn load_notes(
        &self,
    ) -> Result<
        Vec<(
            String,
            Option<String>,
            Option<String>,
            String,
            String,
            String,
        )>,
        StorageError,
    > {
        let mut stmt = self.conn.prepare(
            "SELECT id, linked_tab_id, linked_url, content, created_at, updated_at FROM notes ORDER BY updated_at DESC",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, Option<String>>(1)?,
                row.get::<_, Option<String>>(2)?,
                row.get::<_, String>(3)?,
                row.get::<_, String>(4)?,
                row.get::<_, String>(5)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_note(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM notes WHERE id = ?1", rusqlite::params![id])?;
        self.remove_note_fts(id)?;
        Ok(affected > 0)
    }

    pub fn update_note_content(
        &self,
        id: &str,
        content: &str,
        updated_at: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "UPDATE notes SET content = ?1, updated_at = ?2 WHERE id = ?3",
            rusqlite::params![content, updated_at, id],
        )?;
        self.index_note_fts(id, content)?;
        Ok(())
    }

    pub fn index_note_fts(&self, note_id: &str, content: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO notes_fts(note_id, content) VALUES (?1, ?2)",
            rusqlite::params![note_id, content],
        )?;
        Ok(())
    }

    pub fn remove_note_fts(&self, note_id: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "DELETE FROM notes_fts WHERE note_id = ?1",
            rusqlite::params![note_id],
        )?;
        Ok(())
    }

    // === Easels ===

    pub fn save_easel(&self, easel: &maho_types::easel::Easel) -> Result<(), StorageError> {
        let canvas_items_json = serde_json::to_string(&easel.canvas_items)
            .unwrap_or_else(|_| "[]".to_string());
        let viewport_json = serde_json::to_string(&easel.viewport)
            .unwrap_or_else(|_| "{}".to_string());
        self.conn.execute(
            "INSERT OR REPLACE INTO easels (id, name, canvas_items_json, viewport_json, updated_at) VALUES (?1, ?2, ?3, ?4, datetime('now'))",
            rusqlite::params![easel.id.as_ref(), easel.name, canvas_items_json, viewport_json],
        )?;
        Ok(())
    }

    pub fn load_easels(&self) -> Result<Vec<maho_types::easel::Easel>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, canvas_items_json, viewport_json FROM easels ORDER BY updated_at DESC",
        )?;
        let rows = stmt.query_map([], |row| {
            let id: String = row.get(0)?;
            let name: String = row.get(1)?;
            let canvas_items_json: String = row.get(2)?;
            let viewport_json: String = row.get(3)?;
            Ok((id, name, canvas_items_json, viewport_json))
        })?;
        let mut results = Vec::new();
        for row in rows {
            let (id, name, items_json, vp_json) = row?;
            let canvas_items: Vec<maho_types::easel::CanvasItem> =
                serde_json::from_str(&items_json).unwrap_or_default();
            let viewport: maho_types::common::Viewport =
                serde_json::from_str(&vp_json).unwrap_or(maho_types::common::Viewport {
                    offset: maho_types::common::Point { x: 0.0, y: 0.0 },
                    zoom: 1.0,
                });
            results.push(maho_types::easel::Easel {
                id: maho_types::identifiers::EaselId::new(id),
                name,
                canvas_items,
                viewport,
            });
        }
        Ok(results)
    }

    pub fn delete_easel(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM easels WHERE id = ?1", rusqlite::params![id])?;
        Ok(affected > 0)
    }

    pub fn search_notes_fts(&self, query: &str) -> Result<Vec<(String, f64)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT note_id, bm25(notes_fts) as rank FROM notes_fts WHERE notes_fts MATCH ?1 ORDER BY rank LIMIT 50"
        )?;
        let results = stmt
            .query_map(rusqlite::params![query], |row| {
                Ok((row.get::<_, String>(0)?, row.get::<_, f64>(1)?))
            })?
            .filter_map(|r| r.ok())
            .collect();
        Ok(results)
    }

    pub fn reindex_all_notes_fts(&self, notes: &[(String, String)]) -> Result<(), StorageError> {
        self.conn.execute("DELETE FROM notes_fts", [])?;
        let mut stmt = self
            .conn
            .prepare("INSERT INTO notes_fts(note_id, content) VALUES (?1, ?2)")?;
        for (id, content) in notes {
            stmt.execute(rusqlite::params![id, content])?;
        }
        Ok(())
    }

    // === Boosts ===

    #[allow(clippy::too_many_arguments)]
    pub fn save_boost(
        &self,
        id: &str,
        url_pattern: &str,
        pattern_type: &str,
        custom_css: Option<&str>,
        custom_js: Option<&str>,
        color_theme: Option<&str>,
        font_override: Option<&str>,
        enabled: bool,
        space_scope: Option<&str>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO boosts (id, url_pattern, pattern_type, custom_css, custom_js, color_theme, font_override, enabled, space_scope) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
            rusqlite::params![id, url_pattern, pattern_type, custom_css, custom_js, color_theme, font_override, enabled, space_scope],
        )?;
        Ok(())
    }

    #[allow(clippy::type_complexity)]
    pub fn load_boosts(
        &self,
    ) -> Result<
        Vec<(
            String,
            String,
            String,
            Option<String>,
            Option<String>,
            Option<String>,
            Option<String>,
            bool,
            Option<String>,
        )>,
        StorageError,
    > {
        let mut stmt = self.conn.prepare(
            "SELECT id, url_pattern, pattern_type, custom_css, custom_js, color_theme, font_override, enabled, space_scope FROM boosts",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, Option<String>>(3)?,
                row.get::<_, Option<String>>(4)?,
                row.get::<_, Option<String>>(5)?,
                row.get::<_, Option<String>>(6)?,
                row.get::<_, bool>(7)?,
                row.get::<_, Option<String>>(8)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_boost(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM boosts WHERE id = ?1", rusqlite::params![id])?;
        Ok(affected > 0)
    }

    // === CSS Mods ===

    pub fn save_css_mod(&self, mod_entry: &maho_types::css_mod::CssMod) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO css_mods (id, name, description, author, version, css, enabled, homepage, source_url, created_at, updated_at)\n         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)",
            rusqlite::params![
                &mod_entry.id,
                &mod_entry.name,
                mod_entry.description.as_deref(),
                mod_entry.author.as_deref(),
                mod_entry.version.as_deref(),
                &mod_entry.css,
                mod_entry.enabled as i32,
                mod_entry.homepage.as_deref(),
                mod_entry.source_url.as_deref(),
                &mod_entry.created_at,
                &mod_entry.updated_at,
            ],
        )?;
        Ok(())
    }

    pub fn load_css_mods(&self) -> Result<Vec<maho_types::css_mod::CssMod>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, description, author, version, css, enabled, homepage, source_url, created_at, updated_at FROM css_mods ORDER BY name"
        )?;
        let rows = stmt.query_map([], |row| {
            Ok(maho_types::css_mod::CssMod {
                id: row.get(0)?,
                name: row.get(1)?,
                description: row.get(2)?,
                author: row.get(3)?,
                version: row.get(4)?,
                css: row.get(5)?,
                enabled: row.get::<_, i32>(6)? != 0,
                homepage: row.get(7)?,
                source_url: row.get(8)?,
                created_at: row.get(9)?,
                updated_at: row.get(10)?,
            })
        })?;
        rows.collect::<Result<Vec<_>, _>>().map_err(StorageError::from)
    }

    pub fn delete_css_mod(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute("DELETE FROM css_mods WHERE id = ?1", [id])?;
        Ok(affected > 0)
    }

    // === Permissions ===

    pub fn save_permission(
        &self,
        origin: &str,
        permission: &str,
        policy: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO permissions (origin, permission, policy) VALUES (?1, ?2, ?3)",
            rusqlite::params![origin, permission, policy],
        )?;
        Ok(())
    }

    pub fn load_permissions(&self) -> Result<Vec<(String, String, String)>, StorageError> {
        let mut stmt = self
            .conn
            .prepare("SELECT origin, permission, policy FROM permissions ORDER BY origin")?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_permissions_for_origin(&self, origin: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "DELETE FROM permissions WHERE origin = ?1",
            rusqlite::params![origin],
        )?;
        Ok(())
    }

    // === ATC Rules ===

    #[allow(clippy::too_many_arguments)]
    pub fn save_atc_rule(
        &self,
        id: &str,
        name: &str,
        space_id: Option<&str>,
        url_pattern: Option<&str>,
        max_age_hours: Option<u32>,
        max_tabs: Option<usize>,
        enabled: bool,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO atc_rules (id, name, space_id, url_pattern, max_age_hours, max_tabs, enabled) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            rusqlite::params![id, name, space_id, url_pattern, max_age_hours, max_tabs, enabled],
        )?;
        Ok(())
    }

    #[allow(clippy::type_complexity)]
    pub fn load_atc_rules(
        &self,
    ) -> Result<
        Vec<(
            String,
            String,
            Option<String>,
            Option<String>,
            Option<u32>,
            Option<usize>,
            bool,
        )>,
        StorageError,
    > {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, space_id, url_pattern, max_age_hours, max_tabs, enabled FROM atc_rules",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, Option<String>>(2)?,
                row.get::<_, Option<String>>(3)?,
                row.get::<_, Option<u32>>(4)?,
                row.get::<_, Option<usize>>(5)?,
                row.get::<_, bool>(6)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_atc_rule(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM atc_rules WHERE id = ?1", rusqlite::params![id])?;
        Ok(affected > 0)
    }

    // === Search History ===

    pub fn save_search(&self, query: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO search_history (query, searched_at) VALUES (?1, datetime('now'))",
            rusqlite::params![query],
        )?;
        Ok(())
    }

    pub fn load_recent_searches(
        &self,
        limit: usize,
    ) -> Result<Vec<(String, String)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT query, searched_at FROM search_history ORDER BY searched_at DESC LIMIT ?1",
        )?;
        let rows = stmt.query_map(rusqlite::params![limit], |row| {
            Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn clear_search_history(&self) -> Result<(), StorageError> {
        self.conn.execute("DELETE FROM search_history", [])?;
        Ok(())
    }

    // === Per-site Zoom ===

    pub fn save_zoom(&self, origin: &str, zoom_level: f64) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO per_site_zoom (origin, zoom_level) VALUES (?1, ?2)",
            rusqlite::params![origin, zoom_level],
        )?;
        Ok(())
    }

    pub fn get_zoom_for_site(&self, origin: &str) -> Result<Option<f64>, StorageError> {
        let mut stmt = self
            .conn
            .prepare("SELECT zoom_level FROM per_site_zoom WHERE origin = ?1")?;
        let mut rows = stmt.query(rusqlite::params![origin])?;
        match rows.next()? {
            Some(row) => Ok(Some(row.get(0)?)),
            None => Ok(None),
        }
    }

    pub fn delete_zoom(&self, origin: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM per_site_zoom WHERE origin = ?1",
            rusqlite::params![origin],
        )?;
        Ok(affected > 0)
    }

    // === Downloads ===

    #[allow(clippy::too_many_arguments)]
    pub fn save_download(
        &self,
        id: &str,
        filename: &str,
        url: &str,
        total_bytes: u64,
        received_bytes: u64,
        state: &str,
        file_path: Option<&str>,
        started_at: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO downloads (id, filename, url, total_bytes, received_bytes, state, file_path, started_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)",
            rusqlite::params![id, filename, url, total_bytes, received_bytes, state, file_path, started_at],
        )?;
        Ok(())
    }

    #[allow(clippy::type_complexity)]
    pub fn load_downloads(
        &self,
    ) -> Result<
        Vec<(
            String,
            String,
            String,
            u64,
            u64,
            String,
            Option<String>,
            String,
        )>,
        StorageError,
    > {
        let mut stmt = self.conn.prepare(
            "SELECT id, filename, url, total_bytes, received_bytes, state, file_path, started_at FROM downloads ORDER BY started_at DESC",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, u64>(3)?,
                row.get::<_, u64>(4)?,
                row.get::<_, String>(5)?,
                row.get::<_, Option<String>>(6)?,
                row.get::<_, String>(7)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_download(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM downloads WHERE id = ?1", rusqlite::params![id])?;
        Ok(affected > 0)
    }

    pub fn update_download_progress(
        &self,
        id: &str,
        received_bytes: u64,
        state: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "UPDATE downloads SET received_bytes = ?1, state = ?2 WHERE id = ?3",
            rusqlite::params![received_bytes, state, id],
        )?;
        Ok(())
    }

    // === Usage Stats ===

    pub fn increment_usage(&self, item_key: &str, item_type: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO usage_stats (item_key, item_type, usage_count, last_used_at) 
             VALUES (?1, ?2, 1, datetime('now'))
             ON CONFLICT(item_key) DO UPDATE SET 
             usage_count = usage_count + 1, 
             last_used_at = datetime('now')",
            rusqlite::params![item_key, item_type],
        )?;
        Ok(())
    }

    pub fn get_usage_count(&self, item_key: &str) -> Result<u32, StorageError> {
        let count: u32 = self
            .conn
            .query_row(
                "SELECT usage_count FROM usage_stats WHERE item_key = ?1",
                rusqlite::params![item_key],
                |row| row.get(0),
            )
            .unwrap_or(0);
        Ok(count)
    }

    pub fn get_top_used(
        &self,
        item_type: &str,
        limit: usize,
    ) -> Result<Vec<(String, u32)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT item_key, usage_count FROM usage_stats WHERE item_type = ?1 ORDER BY usage_count DESC LIMIT ?2",
        )?;
        let rows = stmt.query_map(rusqlite::params![item_type, limit], |row| {
            Ok((row.get::<_, String>(0)?, row.get::<_, u32>(1)?))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn clear_usage_stats(&self) -> Result<(), StorageError> {
        self.conn.execute("DELETE FROM usage_stats", [])?;
        Ok(())
    }

    // === Passwords ===

    pub fn save_password(
        &self,
        id: &str,
        domain: &str,
        username: &str,
        created_at: &str,
        password: Option<&str>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO passwords (id, domain, username, created_at, password) VALUES (?1, ?2, ?3, ?4, ?5)",
            rusqlite::params![id, domain, username, created_at, password],
        )?;
        Ok(())
    }

    pub fn get_all_passwords(
        &self,
    ) -> Result<Vec<PasswordRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, domain, username, created_at, last_used, password FROM passwords ORDER BY domain",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, String>(3)?,
                row.get::<_, Option<String>>(4)?,
                row.get::<_, Option<String>>(5)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_password(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self
            .conn
            .execute("DELETE FROM passwords WHERE id = ?1", rusqlite::params![id])?;
        Ok(affected > 0)
    }

    // === Autofill Addresses ===

    pub fn save_autofill_address(
        &self,
        address: AutofillAddressParams<'_>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO autofill_addresses (id, name, street, city, state, zip, country, phone, email) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
            rusqlite::params![
                address.id,
                address.name,
                address.street,
                address.city,
                address.state,
                address.zip,
                address.country,
                address.phone,
                address.email,
            ],
        )?;
        Ok(())
    }

    pub fn load_autofill_addresses(
        &self,
    ) -> Result<Vec<AutofillAddressRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, street, city, state, zip, country, phone, email FROM autofill_addresses",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, String>(3)?,
                row.get::<_, String>(4)?,
                row.get::<_, String>(5)?,
                row.get::<_, String>(6)?,
                row.get::<_, Option<String>>(7)?,
                row.get::<_, Option<String>>(8)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_autofill_address(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM autofill_addresses WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(affected > 0)
    }

    // === Autofill Payments ===

    pub fn save_autofill_payment(
        &self,
        id: &str,
        card_name: &str,
        last_four: &str,
        expiry: &str,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO autofill_payments (id, card_name, last_four, expiry) VALUES (?1, ?2, ?3, ?4)",
            rusqlite::params![id, card_name, last_four, expiry],
        )?;
        Ok(())
    }

    pub fn load_autofill_payments(
        &self,
    ) -> Result<Vec<(String, String, String, String)>, StorageError> {
        let mut stmt = self
            .conn
            .prepare("SELECT id, card_name, last_four, expiry FROM autofill_payments")?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, String>(3)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_autofill_payment(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM autofill_payments WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(affected > 0)
    }

    // === Search Engines ===

    pub fn save_search_engine(
        &self,
        id: &str,
        name: &str,
        url_template: &str,
        shortcut: Option<&str>,
        icon_url: Option<&str>,
        is_default: bool,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO search_engines (id, name, url_template, shortcut, icon_url, is_default) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
            rusqlite::params![id, name, url_template, shortcut, icon_url, is_default],
        )?;
        Ok(())
    }

    #[allow(clippy::type_complexity)]
    pub fn load_search_engines(
        &self,
    ) -> Result<Vec<(String, String, String, Option<String>, Option<String>, bool)>, StorageError>
    {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, url_template, shortcut, icon_url, is_default FROM search_engines",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, Option<String>>(3)?,
                row.get::<_, Option<String>>(4)?,
                row.get::<_, bool>(5)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_search_engine(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM search_engines WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(affected > 0)
    }

    pub fn update_default_search_engine(&self, id: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "UPDATE search_engines SET is_default = 0 WHERE is_default = 1",
            [],
        )?;
        self.conn.execute(
            "UPDATE search_engines SET is_default = 1 WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(())
    }

    // === Shortcuts ===

    pub fn save_shortcut(&self, action: &str, key_combo_json: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO shortcuts (action, key_combo_json, is_custom, enabled) VALUES (?1, ?2, 1, 1) ON CONFLICT(action) DO UPDATE SET key_combo_json = excluded.key_combo_json, is_custom = 1, enabled = COALESCE(shortcuts.enabled, 1)",
            rusqlite::params![action, key_combo_json],
        )?;
        Ok(())
    }

    pub fn save_shortcut_enabled(&self, action: &str, enabled: bool) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO shortcuts (action, key_combo_json, is_custom, enabled) VALUES (?1, '', 0, ?2) ON CONFLICT(action) DO UPDATE SET enabled = excluded.enabled",
            rusqlite::params![action, enabled as i32],
        )?;
        Ok(())
    }

    pub fn delete_shortcut(&self, action: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM shortcuts WHERE action = ?1",
            rusqlite::params![action],
        )?;
        Ok(affected > 0)
    }

    pub fn delete_all_shortcuts(&self) -> Result<(), StorageError> {
        self.conn.execute("DELETE FROM shortcuts", [])?;
        Ok(())
    }

    pub fn load_shortcuts(&self) -> Result<Vec<(String, String, bool)>, StorageError> {
        let mut stmt = self
            .conn
            .prepare("SELECT action, key_combo_json, enabled FROM shortcuts")?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, i32>(2)? != 0,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    // === Bookmark Folders ===

    pub fn delete_bookmark_folder(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM bookmark_folders WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(affected > 0)
    }

    pub fn load_bookmark_folders(
        &self,
    ) -> Result<Vec<BookmarkFolderRecord>, StorageError> {
        let mut stmt = self
            .conn
            .prepare("SELECT id, name, parent_id, created_at FROM bookmark_folders")?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, Option<String>>(2)?,
                row.get::<_, String>(3)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }
}

impl SqliteStorage {
    // Reading List storage methods for SqliteStorage

    // === Reading List ===

    #[allow(clippy::too_many_arguments)]
    pub fn save_reading_list_item(
        &self,
        id: &str,
        url: &str,
        title: &str,
        excerpt: Option<&str>,
        site_name: Option<&str>,
        favicon_url: Option<&str>,
        preview_image_url: Option<&str>,
        added_at: &str,
        read_at: Option<&str>,
        is_read: bool,
        estimated_read_minutes: Option<u32>,
        tags_json: Option<&str>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
        "INSERT OR REPLACE INTO reading_list (id, url, title, excerpt, site_name, favicon_url, preview_image_url, added_at, read_at, is_read, estimated_read_minutes, tags) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)",
        rusqlite::params![
            id,
            url,
            title,
            excerpt,
            site_name,
            favicon_url,
            preview_image_url,
            added_at,
            read_at,
            is_read,
            estimated_read_minutes,
            tags_json
        ],
    )?;
        Ok(())
    }

    #[allow(clippy::type_complexity)]
    pub fn load_reading_list(
        &self,
    ) -> Result<
        Vec<(
            String,
            String,
            String,
            Option<String>,
            Option<String>,
            Option<String>,
            Option<String>,
            String,
            Option<String>,
            bool,
            Option<u32>,
            Option<String>,
        )>,
        StorageError,
    > {
        let mut stmt = self.conn.prepare(
        "SELECT id, url, title, excerpt, site_name, favicon_url, preview_image_url, added_at, read_at, is_read, estimated_read_minutes, tags FROM reading_list ORDER BY added_at DESC",
    )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, Option<String>>(3)?,
                row.get::<_, Option<String>>(4)?,
                row.get::<_, Option<String>>(5)?,
                row.get::<_, Option<String>>(6)?,
                row.get::<_, String>(7)?,
                row.get::<_, Option<String>>(8)?,
                row.get::<_, bool>(9)?,
                row.get::<_, Option<u32>>(10)?,
                row.get::<_, Option<String>>(11)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_reading_list_item(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM reading_list WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(affected > 0)
    }

    pub fn update_reading_list_read_status(
        &self,
        id: &str,
        is_read: bool,
        read_at: Option<&str>,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "UPDATE reading_list SET is_read = ?1, read_at = ?2 WHERE id = ?3",
            rusqlite::params![is_read, read_at, id],
        )?;
        Ok(())
    }

    // === Sync Queue ===

    /// Enqueue an entity push to the persistent sync queue
    pub fn enqueue_sync_entity(
        &self,
        entity_type: &str,
        entity_id: &str,
        version: u64,
        modified_at: i64,
        payload_json: &str,
        deleted: bool,
    ) -> Result<i64, StorageError> {
        let now = chrono::Utc::now().timestamp();
        self.conn.execute(
            "INSERT INTO sync_queue (entity_type, entity_id, version, modified_at, payload_json, deleted, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            rusqlite::params![entity_type, entity_id, version, modified_at, payload_json, deleted as i32, now],
        )?;
        Ok(self.conn.last_insert_rowid())
    }

    /// Load all unsent sync queue entries (sent_at IS NULL)
    #[allow(clippy::type_complexity)]
    pub fn load_pending_sync_entities(
        &self,
    ) -> Result<Vec<(i64, String, String, u64, i64, String, bool)>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, entity_type, entity_id, version, modified_at, payload_json, deleted FROM sync_queue WHERE sent_at IS NULL ORDER BY id ASC"
        )?;
        let rows = stmt.query_map([], |row| {
            Ok((
                row.get::<_, i64>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, u64>(3)?,
                row.get::<_, i64>(4)?,
                row.get::<_, String>(5)?,
                row.get::<_, bool>(6)?,
            ))
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    /// Mark sync queue entries as sent
    pub fn mark_sync_entities_sent(&self, ids: &[i64]) -> Result<(), StorageError> {
        let now = chrono::Utc::now().timestamp();
        for id in ids {
            self.conn.execute(
                "UPDATE sync_queue SET sent_at = ?1 WHERE id = ?2",
                rusqlite::params![now, id],
            )?;
        }
        Ok(())
    }

    /// Acknowledge and remove sync queue entries (after server confirms)
    pub fn ack_sync_entities(&self, ids: &[i64]) -> Result<(), StorageError> {
        for id in ids {
            self.conn.execute(
                "DELETE FROM sync_queue WHERE id = ?1",
                rusqlite::params![id],
            )?;
        }
        Ok(())
    }

    /// Get last pulled version for an entity type
    pub fn get_sync_last_pulled_version(&self, entity_type: &str) -> Result<u64, StorageError> {
        let version: u64 = self
            .conn
            .query_row(
                "SELECT last_pulled_version FROM sync_state WHERE entity_type = ?1",
                rusqlite::params![entity_type],
                |row| row.get(0),
            )
            .unwrap_or(0);
        Ok(version)
    }

    /// Set last pulled version for an entity type
    pub fn set_sync_last_pulled_version(
        &self,
        entity_type: &str,
        version: u64,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO sync_state (entity_type, last_pulled_version) VALUES (?1, ?2)",
            rusqlite::params![entity_type, version],
        )?;
        Ok(())
    }

    pub fn save_shared_collection(
        &self,
        collection: &maho_types::sharing::SharedCollection,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT OR REPLACE INTO shared_collections (id, space_id, name, permission, member_count, share_link, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            rusqlite::params![collection.id, collection.space_id, collection.name, collection.permission, collection.member_count, collection.share_link, collection.created_at],
        )?;
        Ok(())
    }

    pub fn get_all_shared_collections(
        &self,
    ) -> Result<Vec<maho_types::sharing::SharedCollection>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, space_id, name, permission, member_count, share_link, created_at FROM shared_collections",
        )?;
        let rows = stmt.query_map([], |row| {
            Ok(maho_types::sharing::SharedCollection {
                id: row.get::<_, String>(0)?,
                space_id: row.get::<_, String>(1)?,
                name: row.get::<_, String>(2)?,
                permission: row.get::<_, String>(3)?,
                member_count: row.get::<_, u32>(4)?,
                share_link: row.get::<_, Option<String>>(5)?,
                created_at: row.get::<_, String>(6)?,
            })
        })?;
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        Ok(results)
    }

    pub fn delete_shared_collection(&self, id: &str) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "DELETE FROM shared_collections WHERE id = ?1",
            rusqlite::params![id],
        )?;
        Ok(affected > 0)
    }

    pub fn update_shared_collection_permission(
        &self,
        id: &str,
        permission: &str,
    ) -> Result<bool, StorageError> {
        let affected = self.conn.execute(
            "UPDATE shared_collections SET permission = ?1 WHERE id = ?2",
            rusqlite::params![permission, id],
        )?;
        Ok(affected > 0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_store() -> SqliteStorage {
        SqliteStorage::open_in_memory().unwrap()
    }

    #[test]
    fn test_enqueue_sync_entity() {
        let db = SqliteStorage::open_in_memory().unwrap();
        let id = db
            .enqueue_sync_entity("bookmarks", "bm-1", 1, 1000, r#"{"title":"Test"}"#, false)
            .unwrap();
        assert!(id > 0);

        let pending = db.load_pending_sync_entities().unwrap();
        assert_eq!(pending.len(), 1);
        let (row_id, entity_type, entity_id, version, modified_at, payload_json, deleted) =
            &pending[0];
        assert_eq!(*row_id, id);
        assert_eq!(entity_type, "bookmarks");
        assert_eq!(entity_id, "bm-1");
        assert_eq!(*version, 1);
        assert_eq!(*modified_at, 1000);
        assert_eq!(payload_json, r#"{"title":"Test"}"#);
        assert!(!deleted);
    }

    #[test]
    fn test_load_pending_sync_entities_order() {
        let db = SqliteStorage::open_in_memory().unwrap();
        let id1 = db
            .enqueue_sync_entity("bookmarks", "bm-1", 1, 1000, "{}", false)
            .unwrap();
        let id2 = db
            .enqueue_sync_entity("notes", "n-1", 2, 2000, "{}", false)
            .unwrap();
        let id3 = db
            .enqueue_sync_entity("bookmarks", "bm-2", 3, 3000, "{}", true)
            .unwrap();

        let pending = db.load_pending_sync_entities().unwrap();
        assert_eq!(pending.len(), 3);
        assert_eq!(pending[0].0, id1);
        assert_eq!(pending[1].0, id2);
        assert_eq!(pending[2].0, id3);
        assert!(id1 < id2);
        assert!(id2 < id3);
    }

    #[test]
    fn sqlite_save_and_load_css_mods() {
        let store = test_store();
        let m = maho_types::css_mod::CssMod {
            id: "mod1".into(),
            name: "Dark Scrollbars".into(),
            description: Some("Makes scrollbars dark".into()),
            author: Some("test".into()),
            version: Some("1.0.0".into()),
            css: "::-webkit-scrollbar { background: #1a1a1a; }".into(),
            enabled: true,
            homepage: None,
            source_url: None,
            created_at: "2024-01-01T00:00:00Z".into(),
            updated_at: "2024-01-01T00:00:00Z".into(),
        };
        store.save_css_mod(&m).unwrap();
        let loaded = store.load_css_mods().unwrap();
        assert_eq!(loaded.len(), 1);
        assert_eq!(loaded[0].name, "Dark Scrollbars");
        assert!(loaded[0].enabled);
    }

    #[test]
    fn sqlite_delete_css_mod() {
        let store = test_store();
        let m = maho_types::css_mod::CssMod {
            id: "mod2".into(),
            name: "Test Mod".into(),
            description: None,
            author: None,
            version: None,
            css: "body { color: red; }".into(),
            enabled: false,
            homepage: None,
            source_url: None,
            created_at: "2024-01-01T00:00:00Z".into(),
            updated_at: "2024-01-01T00:00:00Z".into(),
        };
        store.save_css_mod(&m).unwrap();
        assert!(store.delete_css_mod("mod2").unwrap());
        assert!(!store.delete_css_mod("mod2").unwrap());
        assert!(store.load_css_mods().unwrap().is_empty());
    }

    #[test]
    fn test_mark_sync_entities_sent() {
        let db = SqliteStorage::open_in_memory().unwrap();
        let id1 = db
            .enqueue_sync_entity("bookmarks", "bm-1", 1, 1000, "{}", false)
            .unwrap();
        let id2 = db
            .enqueue_sync_entity("bookmarks", "bm-2", 2, 2000, "{}", false)
            .unwrap();

        db.mark_sync_entities_sent(&[id1]).unwrap();

        let pending = db.load_pending_sync_entities().unwrap();
        assert_eq!(pending.len(), 1);
        assert_eq!(pending[0].0, id2);
    }

    #[test]
    fn test_ack_sync_entities() {
        let db = SqliteStorage::open_in_memory().unwrap();
        let id1 = db
            .enqueue_sync_entity("bookmarks", "bm-1", 1, 1000, "{}", false)
            .unwrap();
        let id2 = db
            .enqueue_sync_entity("bookmarks", "bm-2", 2, 2000, "{}", false)
            .unwrap();

        db.ack_sync_entities(&[id1]).unwrap();

        let pending = db.load_pending_sync_entities().unwrap();
        assert_eq!(pending.len(), 1);
        assert_eq!(pending[0].0, id2);
    }

    #[test]
    fn test_get_last_pulled_version_default() {
        let db = SqliteStorage::open_in_memory().unwrap();
        let version = db.get_sync_last_pulled_version("bookmarks").unwrap();
        assert_eq!(version, 0);
    }

    #[test]
    fn test_set_and_get_last_pulled_version() {
        let db = SqliteStorage::open_in_memory().unwrap();
        db.set_sync_last_pulled_version("bookmarks", 42).unwrap();
        let version = db.get_sync_last_pulled_version("bookmarks").unwrap();
        assert_eq!(version, 42);

        db.set_sync_last_pulled_version("bookmarks", 99).unwrap();
        let version = db.get_sync_last_pulled_version("bookmarks").unwrap();
        assert_eq!(version, 99);
    }

    #[test]
    fn test_pulled_version_per_entity_type() {
        let db = SqliteStorage::open_in_memory().unwrap();
        db.set_sync_last_pulled_version("bookmarks", 10).unwrap();
        db.set_sync_last_pulled_version("notes", 20).unwrap();

        assert_eq!(db.get_sync_last_pulled_version("bookmarks").unwrap(), 10);
        assert_eq!(db.get_sync_last_pulled_version("notes").unwrap(), 20);
    }
}
