//! Typed support for the offline `incognito_storage_fixture` example.
//!
//! Split into `seed` (writes deterministic normal rows) and `verify` (compares
//! baseline/final snapshots and rejects OTR canaries). The top-level example is
//! a thin CLI; all logic lives here. Every file stays under 250 pure LOC.

pub mod seed;
pub mod verify;

use std::collections::BTreeMap;

/// OTR canary tokens that must never appear in an exported named store.
pub const OTR_CANARIES: &[&str] = &[
    "OTR_DOWNLOAD_SECRET",
    "otr_session",
    "otr_local",
    "otr-cookie-canary-value",
    "otr-space-canary",
];

/// Minimal `--key value` argument bag with typed accessors.
pub struct Args {
    values: BTreeMap<String, String>,
}

impl Args {
    pub fn parse<I: Iterator<Item = String>>(mut it: I) -> Result<Args, String> {
        let mut values = BTreeMap::new();
        while let Some(token) = it.next() {
            let key = token
                .strip_prefix("--")
                .ok_or_else(|| format!("expected --flag, got {token:?}"))?;
            let value = it
                .next()
                .ok_or_else(|| format!("flag --{key} is missing its value"))?;
            values.insert(key.to_string(), value);
        }
        Ok(Args { values })
    }

    pub fn require(&self, key: &str) -> Result<&str, String> {
        self.values
            .get(key)
            .map(String::as_str)
            .ok_or_else(|| format!("missing required flag --{key}"))
    }
}

/// Write a machine-parseable receipt as JSON, creating parent dirs as needed.
pub fn write_receipt(path: &str, receipt: &serde_json::Value) -> Result<(), String> {
    if let Some(parent) = std::path::Path::new(path).parent() {
        std::fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    let body = serde_json::to_string_pretty(receipt).map_err(|e| e.to_string())?;
    std::fs::write(path, body).map_err(|e| e.to_string())
}
