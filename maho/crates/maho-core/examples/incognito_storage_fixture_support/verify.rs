//! `verify` subcommand: consumes baseline/final snapshots and requires complete
//! equality plus zero OTR canaries. The snapshot is canonical JSON produced by
//! `snapshot_incognito_storage.py`; this consumer is schema-agnostic and
//! compares the two structurally, then scans the final snapshot for canaries.

use super::{Args, OTR_CANARIES};

fn load_snapshot(path: &str) -> Result<serde_json::Value, String> {
    let p = std::path::Path::new(path);
    let file = if p.is_dir() {
        p.join("snapshot.json")
    } else {
        p.to_path_buf()
    };
    let body = std::fs::read_to_string(&file)
        .map_err(|e| format!("read snapshot {}: {e}", file.display()))?;
    serde_json::from_str(&body).map_err(|e| format!("parse snapshot {}: {e}", file.display()))
}

fn scan_for_canaries(value: &serde_json::Value) -> Vec<String> {
    let serialized = value.to_string();
    OTR_CANARIES
        .iter()
        .filter(|canary| serialized.contains(**canary))
        .map(|canary| canary.to_string())
        .collect()
}

pub fn run(args: &Args) -> Result<(), String> {
    let baseline_path = args.require("baseline")?;
    let final_path = args.require("final")?;
    let receipt = args.require("receipt")?;

    let baseline = load_snapshot(baseline_path)?;
    let final_snapshot = load_snapshot(final_path)?;

    let equal = baseline == final_snapshot;
    let leaked = scan_for_canaries(&final_snapshot);

    super::write_receipt(
        receipt,
        &serde_json::json!({
            "status": if equal && leaked.is_empty() { "verified" } else { "failed" },
            "baseline_equals_final": equal,
            "otr_canaries_found": leaked,
        }),
    )?;

    if !equal {
        return Err("baseline and final snapshots differ".to_string());
    }
    if !leaked.is_empty() {
        return Err(format!(
            "OTR canaries present in final snapshot: {leaked:?}"
        ));
    }
    Ok(())
}
