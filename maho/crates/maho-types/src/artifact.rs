use serde::{Deserialize, Serialize};

/// Metadata describing a single artifact produced during a session.
///
/// This is a pure contract type: it carries identity and storage metadata for
/// an artifact but contains no domain logic. `artifact_id` is an opaque UUID
/// string (see [`ArtifactId`]); `storage_rel_path` is relative to the
/// artifact storage root.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct ArtifactInfo {
    /// Opaque UUID string identifying the artifact.
    pub artifact_id: String,
    /// Session that produced the artifact.
    pub session_id: String,
    /// Human-readable name shown in the UI.
    pub display_name: String,
    /// MIME type of the artifact contents.
    pub mime_type: String,
    /// Size of the artifact contents in bytes.
    pub size_bytes: u64,
    /// Path to the stored artifact, relative to the artifact storage root.
    pub storage_rel_path: String,
    /// Creation time as milliseconds since the Unix epoch.
    pub created_at_ms: i64,
    /// Format-typed artifact kind (e.g. "xlsx", "pdf", "html", "generic").
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub kind: Option<String>,
}

/// Generator for opaque artifact identifiers.
pub struct ArtifactId;

impl ArtifactId {
    /// Returns a fresh UUID v4 string in simple (no-hyphen) format.
    // Intentionally returns `String`: artifact ids are stored as opaque
    // strings in `ArtifactInfo`, so a `Self`-returning constructor would
    // force a wrapper type the contract does not want.
    #[allow(clippy::new_ret_no_self)]
    pub fn new() -> String {
        uuid::Uuid::new_v4().simple().to_string()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> ArtifactInfo {
        ArtifactInfo {
            artifact_id: ArtifactId::new(),
            session_id: "session-1".to_string(),
            display_name: "Report.pdf".to_string(),
            mime_type: "application/pdf".to_string(),
            size_bytes: 1024,
            storage_rel_path: "session-1/report.pdf".to_string(),
            created_at_ms: 1_754_000_000_000,
            kind: None,
        }
    }

    #[test]
    fn artifact_info_serde_roundtrip() {
        let original = sample();
        let json = serde_json::to_string(&original).unwrap();
        let decoded: ArtifactInfo = serde_json::from_str(&json).unwrap();
        assert_eq!(original, decoded);
    }

    #[test]
    fn artifact_info_partial_eq() {
        let a = sample();
        let mut b = a.clone();
        assert_eq!(a, b);
        b.display_name = "Other.txt".to_string();
        assert_ne!(a, b);
    }

    #[test]
    fn artifact_id_new_generates_unique_ids() {
        let first = ArtifactId::new();
        let second = ArtifactId::new();
        assert_ne!(first, second);
        assert_eq!(first.len(), 32);
        assert!(first.chars().all(|c| c.is_ascii_hexdigit()));
    }

    #[test]
    fn artifact_info_roundtrip_with_unusual_strings() {
        let unicode = ArtifactInfo {
            display_name: "résumé — 2026年8月 🗂.txt".to_string(),
            ..sample()
        };
        let json = serde_json::to_string(&unicode).unwrap();
        let decoded: ArtifactInfo = serde_json::from_str(&json).unwrap();
        assert_eq!(unicode, decoded);

        let empty = ArtifactInfo {
            display_name: String::new(),
            ..sample()
        };
        let json = serde_json::to_string(&empty).unwrap();
        let decoded: ArtifactInfo = serde_json::from_str(&json).unwrap();
        assert_eq!(empty, decoded);
    }
}
