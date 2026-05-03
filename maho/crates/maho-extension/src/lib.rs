pub mod error;
pub mod lifecycle;
pub mod loader;
pub mod manifest;
pub mod storage;

pub use error::ExtensionError;
pub use lifecycle::{ContentScriptMatch, ExtensionLifecycle, ExtensionState, LoadedExtension};
pub use loader::{
    discover_extensions, extract_content_scripts, load_extension_resources, parse_manifest,
    validate_manifest, ExtensionBundle,
};
pub use manifest::ExtensionManifest;
pub use storage::{ExtensionStorage, ExtensionStorageManager, StorageChangeRecord};
