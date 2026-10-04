use crate::browser_cli::schema::BenchmarkError;
use std::path::{Path, PathBuf};

pub const FIXTURE_W1_STATIC_FORM: &str = include_str!("../../browser_cli/w1_static_form.html");
pub const FIXTURE_W2_SPA_MODAL: &str = include_str!("../../browser_cli/w2_spa_modal.html");
pub const FIXTURE_W3_DYNAMIC_LIST: &str = include_str!("../../browser_cli/w3_dynamic_list.html");
pub const FIXTURE_W4_IFRAME_PAGE: &str = include_str!("../../browser_cli/w4_iframe_page.html");
pub const FIXTURE_W4_CHILD_IFRAME: &str = include_str!("../../browser_cli/w4_child_iframe.html");
pub const FIXTURE_W5_LARGE_AX_TREE: &str = include_str!("../../browser_cli/w5_large_ax_tree.html");
pub const FIXTURE_W6_NAVIGATION_TRANSITION: &str =
    include_str!("../../browser_cli/w6_navigation_transition.html");

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FixtureValidationError {
    pub fixture_id: String,
    pub missing_requirement: String,
}

pub fn get_embedded_fixture(workload_id: &str) -> Option<&'static str> {
    match workload_id {
        "w1_static_form" => Some(FIXTURE_W1_STATIC_FORM),
        "w2_spa_modal" => Some(FIXTURE_W2_SPA_MODAL),
        "w3_dynamic_list" => Some(FIXTURE_W3_DYNAMIC_LIST),
        "w4_iframe_page" => Some(FIXTURE_W4_IFRAME_PAGE),
        "w4_child_iframe" => Some(FIXTURE_W4_CHILD_IFRAME),
        "w5_large_ax_tree" => Some(FIXTURE_W5_LARGE_AX_TREE),
        "w6_navigation_transition" => Some(FIXTURE_W6_NAVIGATION_TRANSITION),
        _ => None,
    }
}

pub fn validate_fixture_content(workload_id: &str, content: &str) -> Result<(), BenchmarkError> {
    match workload_id {
        "w1_static_form" => {
            let required_tokens = [
                "id=\"fullname\"",
                "id=\"email\"",
                "id=\"phone\"",
                "<select",
                "id=\"role\"",
                "type=\"checkbox\"",
                "id=\"terms\"",
                "id=\"submit-btn\"",
                "id=\"validation-message\"",
            ];
            for token in required_tokens {
                if !content.contains(token) {
                    return Err(BenchmarkError::FixtureValidation(format!(
                        "W1 fixture missing expected element/token: {token}"
                    )));
                }
            }
        }
        "w2_spa_modal" => {
            let required_tokens = [
                "id=\"open-modal-btn\"",
                "<dialog",
                "id=\"dialog-confirm-btn\"",
                "id=\"dialog-close-btn\"",
                "insertBefore",
            ];
            for token in required_tokens {
                if !content.contains(token) {
                    return Err(BenchmarkError::FixtureValidation(format!(
                        "W2 fixture missing expected element/token: {token}"
                    )));
                }
            }
        }
        "w3_dynamic_list" => {
            let required_tokens = [
                "id=\"dynamic-list\"",
                "id=\"add-item-btn\"",
                "id=\"remove-item-btn\"",
                "id=\"toggle-ticker-btn\"",
                "setInterval",
            ];
            for token in required_tokens {
                if !content.contains(token) {
                    return Err(BenchmarkError::FixtureValidation(format!(
                        "W3 fixture missing expected element/token: {token}"
                    )));
                }
            }
        }
        "w4_iframe_page" => {
            let required_tokens = [
                "id=\"main-input\"",
                "id=\"main-action-btn\"",
                "<iframe",
                "iframe-username",
                "iframe-submit-btn",
            ];
            for token in required_tokens {
                if !content.contains(token) {
                    return Err(BenchmarkError::FixtureValidation(format!(
                        "W4 fixture missing expected element/token: {token}"
                    )));
                }
            }
        }
        "w5_large_ax_tree" => {
            let required_tokens = [
                "id=\"large-tree-search\"",
                "id=\"large-tree-filter\"",
                "id=\"large-tree-action-btn\"",
                "id=\"large-tree-summary\"",
                "500", // builds 500 cards = thousands of nodes
            ];
            for token in required_tokens {
                if !content.contains(token) {
                    return Err(BenchmarkError::FixtureValidation(format!(
                        "W5 fixture missing expected element/token: {token}"
                    )));
                }
            }
        }
        "w6_navigation_transition" => {
            let required_tokens = [
                "id=\"view-login\"",
                "id=\"login-username\"",
                "id=\"login-password\"",
                "id=\"login-submit-btn\"",
                "id=\"view-dashboard\"",
                "id=\"dashboard-heading\"",
                "id=\"logout-btn\"",
            ];
            for token in required_tokens {
                if !content.contains(token) {
                    return Err(BenchmarkError::FixtureValidation(format!(
                        "W6 fixture missing expected element/token: {token}"
                    )));
                }
            }
        }
        _ => {
            return Err(BenchmarkError::FixtureValidation(format!(
                "Unknown workload fixture ID: {workload_id}"
            )));
        }
    }
    Ok(())
}

pub fn resolve_fixture_path(
    base_dir: Option<&Path>,
    relative_or_filename: &str,
) -> Option<PathBuf> {
    let candidate = PathBuf::from(relative_or_filename);
    if candidate.is_file() {
        return Some(candidate);
    }
    if let Some(base) = base_dir {
        let path = base.join(relative_or_filename);
        if path.is_file() {
            return Some(path);
        }
    }
    let default_bases = [
        PathBuf::from("crates/maho-bench"),
        PathBuf::from("maho/crates/maho-bench"),
        PathBuf::from("."),
    ];
    for b in &default_bases {
        let p = b.join(relative_or_filename);
        if p.is_file() {
            return Some(p);
        }
    }
    None
}

pub fn validate_all_fixtures(base_dir: Option<&Path>) -> Result<Vec<String>, BenchmarkError> {
    let fixture_ids = [
        ("w1_static_form", "browser_cli/fixtures/w1_static_form.html"),
        ("w2_spa_modal", "browser_cli/fixtures/w2_spa_modal.html"),
        (
            "w3_dynamic_list",
            "browser_cli/fixtures/w3_dynamic_list.html",
        ),
        ("w4_iframe_page", "browser_cli/fixtures/w4_iframe_page.html"),
        (
            "w5_large_ax_tree",
            "browser_cli/fixtures/w5_large_ax_tree.html",
        ),
        (
            "w6_navigation_transition",
            "browser_cli/fixtures/w6_navigation_transition.html",
        ),
    ];

    let mut validated = Vec::new();
    for (id, rel_path) in fixture_ids {
        let content = if let Some(path) = resolve_fixture_path(base_dir, rel_path) {
            std::fs::read_to_string(path)?
        } else if let Some(embedded) = get_embedded_fixture(id) {
            embedded.to_string()
        } else {
            return Err(BenchmarkError::FixtureValidation(format!(
                "Fixture file not found: {rel_path} (id: {id})"
            )));
        };

        validate_fixture_content(id, &content)?;
        validated.push(id.to_string());
    }

    Ok(validated)
}
