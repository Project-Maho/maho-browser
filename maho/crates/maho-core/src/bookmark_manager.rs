use std::collections::HashMap;

use maho_types::common::DateTime;

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct BookmarkId(pub String);

impl BookmarkId {
    pub fn generate() -> Self {
        Self(uuid::Uuid::new_v4().to_string())
    }
    pub fn new(id: impl Into<String>) -> Self {
        Self(id.into())
    }
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct BookmarkFolder {
    pub id: String,
    pub name: String,
    pub parent_id: Option<String>,
    pub created_at: DateTime,
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct Bookmark {
    pub id: BookmarkId,
    pub title: String,
    pub url: String,
    pub folder_id: Option<String>,
    pub favicon: Option<String>,
    pub created_at: DateTime,
}

pub struct BookmarkManager {
    bookmarks: HashMap<String, Bookmark>,
    folders: HashMap<String, BookmarkFolder>,
}

impl Default for BookmarkManager {
    fn default() -> Self {
        Self::new()
    }
}

impl BookmarkManager {
    pub fn new() -> Self {
        Self {
            bookmarks: HashMap::new(),
            folders: HashMap::new(),
        }
    }

    pub fn add_bookmark(
        &mut self,
        title: String,
        url: String,
        folder_id: Option<String>,
        favicon: Option<String>,
    ) -> Bookmark {
        let id = BookmarkId::generate();
        let bookmark = Bookmark {
            id: id.clone(),
            title,
            url,
            folder_id,
            favicon,
            created_at: DateTime::now(),
        };
        self.bookmarks.insert(id.0.clone(), bookmark.clone());
        bookmark
    }

    pub fn remove_bookmark(&mut self, bookmark_id: &str) -> Option<Bookmark> {
        self.bookmarks.remove(bookmark_id)
    }

    pub fn update_bookmark(
        &mut self,
        bookmark_id: &str,
        title: Option<String>,
        url: Option<String>,
        folder_id: Option<String>,
    ) {
        if let Some(bookmark) = self.bookmarks.get_mut(bookmark_id) {
            if let Some(t) = title {
                bookmark.title = t;
            }
            if let Some(u) = url {
                bookmark.url = u;
            }
            if folder_id.is_some() {
                bookmark.folder_id = folder_id;
            }
        }
    }

    pub fn get_bookmark(&self, bookmark_id: &str) -> Option<&Bookmark> {
        self.bookmarks.get(bookmark_id)
    }

    pub fn get_all_bookmarks(&self) -> Vec<&Bookmark> {
        self.bookmarks.values().collect()
    }

    pub fn get_bookmarks_in_folder(&self, folder_id: Option<&str>) -> Vec<&Bookmark> {
        self.bookmarks
            .values()
            .filter(|b| b.folder_id.as_deref() == folder_id)
            .collect()
    }

    pub fn search_bookmarks(&self, query: &str) -> Vec<&Bookmark> {
        let query_lower = query.to_lowercase();
        self.bookmarks
            .values()
            .filter(|b| {
                b.title.to_lowercase().contains(&query_lower)
                    || b.url.to_lowercase().contains(&query_lower)
            })
            .collect()
    }

    pub fn move_bookmark(&mut self, bookmark_id: &str, folder_id: Option<String>) {
        if let Some(bookmark) = self.bookmarks.get_mut(bookmark_id) {
            bookmark.folder_id = folder_id;
        }
    }

    pub fn create_folder(&mut self, name: String, parent_id: Option<String>) -> BookmarkFolder {
        let id = uuid::Uuid::new_v4().to_string();
        let folder = BookmarkFolder {
            id: id.clone(),
            name,
            parent_id,
            created_at: DateTime::now(),
        };
        self.folders.insert(id, folder.clone());
        folder
    }

    pub fn delete_folder(&mut self, folder_id: &str) -> Option<BookmarkFolder> {
        self.folders.remove(folder_id)
    }

    pub fn rename_folder(&mut self, folder_id: &str, name: String) {
        if let Some(folder) = self.folders.get_mut(folder_id) {
            folder.name = name;
        }
    }

    pub fn get_folders(&self) -> Vec<&BookmarkFolder> {
        self.folders.values().collect()
    }

    pub fn get_subfolders(&self, parent_id: Option<&str>) -> Vec<&BookmarkFolder> {
        self.folders
            .values()
            .filter(|f| f.parent_id.as_deref() == parent_id)
            .collect()
    }

    pub fn restore_bookmark(&mut self, bookmark: Bookmark) {
        self.bookmarks.insert(bookmark.id.0.clone(), bookmark);
    }

    pub fn restore_folder(&mut self, folder: BookmarkFolder) {
        self.folders.insert(folder.id.clone(), folder);
    }
}
