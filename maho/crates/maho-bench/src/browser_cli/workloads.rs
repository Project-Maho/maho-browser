use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "action", rename_all = "snake_case")]
pub enum WorkloadStep {
    Navigate {
        url: String,
    },
    Click {
        selector: String,
    },
    Fill {
        selector: String,
        value: String,
    },
    SelectOption {
        selector: String,
        value: String,
    },
    Check {
        selector: String,
    },
    TakeSnapshot,
    AssertText {
        selector: String,
        expected: String,
    },
    WaitUntil {
        selector: String,
    },
    Custom {
        name: String,
        params: serde_json::Value,
    },
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct WorkloadDefinition {
    pub id: String,
    pub name: String,
    pub fixture_path: String,
    pub goal_description: String,
    pub steps: Vec<WorkloadStep>,
}

pub fn all_workloads() -> Vec<WorkloadDefinition> {
    vec![
        w1_static_form_workload(),
        w2_spa_modal_workload(),
        w3_dynamic_list_workload(),
        w4_iframe_page_workload(),
        w5_large_ax_tree_workload(),
        w6_navigation_transition_workload(),
    ]
}

pub fn get_workload(id: &str) -> Option<WorkloadDefinition> {
    all_workloads().into_iter().find(|w| w.id == id)
}

pub fn w1_static_form_workload() -> WorkloadDefinition {
    WorkloadDefinition {
        id: "w1_static_form".into(),
        name: "W1: Static Form".into(),
        fixture_path: "browser_cli/fixtures/w1_static_form.html".into(),
        goal_description: "Fill form with 3 textboxes, select option, checkbox, submit, and verify validation message".into(),
        steps: vec![
            WorkloadStep::Navigate {
                url: "browser_cli/fixtures/w1_static_form.html".into(),
            },
            WorkloadStep::TakeSnapshot,
            WorkloadStep::Fill {
                selector: "#fullname".into(),
                value: "Alex Mercer".into(),
            },
            WorkloadStep::Fill {
                selector: "#email".into(),
                value: "alex.mercer@example.com".into(),
            },
            WorkloadStep::Fill {
                selector: "#phone".into(),
                value: "555-0142".into(),
            },
            WorkloadStep::SelectOption {
                selector: "#role".into(),
                value: "developer".into(),
            },
            WorkloadStep::Check {
                selector: "#terms".into(),
            },
            WorkloadStep::TakeSnapshot,
            WorkloadStep::Click {
                selector: "#submit-btn".into(),
            },
            WorkloadStep::AssertText {
                selector: "#validation-message".into(),
                expected: "Form submitted successfully".into(),
            },
            WorkloadStep::TakeSnapshot,
        ],
    }
}

pub fn w2_spa_modal_workload() -> WorkloadDefinition {
    WorkloadDefinition {
        id: "w2_spa_modal".into(),
        name: "W2: SPA Modal".into(),
        fixture_path: "browser_cli/fixtures/w2_spa_modal.html".into(),
        goal_description: "Open dialog via button, update content via dialog button causing front-of-tree insertion and ref renumbering".into(),
        steps: vec![
            WorkloadStep::Navigate {
                url: "browser_cli/fixtures/w2_spa_modal.html".into(),
            },
            WorkloadStep::TakeSnapshot,
            WorkloadStep::Click {
                selector: "#open-modal-btn".into(),
            },
            WorkloadStep::TakeSnapshot,
            WorkloadStep::Fill {
                selector: "#banner-input".into(),
                value: "Scheduled maintenance notice".into(),
            },
            WorkloadStep::Click {
                selector: "#dialog-confirm-btn".into(),
            },
            WorkloadStep::TakeSnapshot,
            WorkloadStep::AssertText {
                selector: "#status-display".into(),
                expected: "Banner prepended to tree".into(),
            },
        ],
    }
}

pub fn w3_dynamic_list_workload() -> WorkloadDefinition {
    WorkloadDefinition {
        id: "w3_dynamic_list".into(),
        name: "W3: Dynamic List".into(),
        fixture_path: "browser_cli/fixtures/w3_dynamic_list.html".into(),
        goal_description:
            "Add and remove rows while background script mutates list shape between snapshots"
                .into(),
        steps: vec![
            WorkloadStep::Navigate {
                url: "browser_cli/fixtures/w3_dynamic_list.html".into(),
            },
            WorkloadStep::TakeSnapshot,
            WorkloadStep::Fill {
                selector: "#new-item-name".into(),
                value: "High Priority Task".into(),
            },
            WorkloadStep::Click {
                selector: "#add-item-btn".into(),
            },
            WorkloadStep::TakeSnapshot,
            WorkloadStep::Click {
                selector: "#remove-item-btn".into(),
            },
            WorkloadStep::TakeSnapshot,
            WorkloadStep::Click {
                selector: "#toggle-ticker-btn".into(),
            },
            WorkloadStep::TakeSnapshot,
        ],
    }
}

pub fn w4_iframe_page_workload() -> WorkloadDefinition {
    WorkloadDefinition {
        id: "w4_iframe_page".into(),
        name: "W4: Iframe Page".into(),
        fixture_path: "browser_cli/fixtures/w4_iframe_page.html".into(),
        goal_description: "Execute interactive actions in both parent frame and child iframe"
            .into(),
        steps: vec![
            WorkloadStep::Navigate {
                url: "browser_cli/fixtures/w4_iframe_page.html".into(),
            },
            WorkloadStep::TakeSnapshot,
            WorkloadStep::Fill {
                selector: "#main-input".into(),
                value: "Search Term Main".into(),
            },
            WorkloadStep::Click {
                selector: "#main-action-btn".into(),
            },
            WorkloadStep::AssertText {
                selector: "#main-status".into(),
                expected: "Parent query received".into(),
            },
            WorkloadStep::TakeSnapshot,
            WorkloadStep::Fill {
                selector: "#iframe-username".into(),
                value: "admin_user".into(),
            },
            WorkloadStep::Click {
                selector: "#iframe-submit-btn".into(),
            },
            WorkloadStep::AssertText {
                selector: "#iframe-feedback".into(),
                expected: "Action executed for admin_user".into(),
            },
            WorkloadStep::TakeSnapshot,
        ],
    }
}

pub fn w5_large_ax_tree_workload() -> WorkloadDefinition {
    WorkloadDefinition {
        id: "w5_large_ax_tree".into(),
        name: "W5: Large Accessibility Tree".into(),
        fixture_path: "browser_cli/fixtures/w5_large_ax_tree.html".into(),
        goal_description:
            "Traverse and search a large accessibility tree with thousands of static nodes".into(),
        steps: vec![
            WorkloadStep::Navigate {
                url: "browser_cli/fixtures/w5_large_ax_tree.html".into(),
            },
            WorkloadStep::TakeSnapshot,
            WorkloadStep::Fill {
                selector: "#large-tree-search".into(),
                value: "timestamp".into(),
            },
            WorkloadStep::SelectOption {
                selector: "#large-tree-filter".into(),
                value: "critical".into(),
            },
            WorkloadStep::Click {
                selector: "#large-tree-action-btn".into(),
            },
            WorkloadStep::TakeSnapshot,
            WorkloadStep::AssertText {
                selector: "#large-tree-summary".into(),
                expected: "Filter applied".into(),
            },
        ],
    }
}

pub fn w6_navigation_transition_workload() -> WorkloadDefinition {
    WorkloadDefinition {
        id: "w6_navigation_transition".into(),
        name: "W6: Login-Like Transition".into(),
        fixture_path: "browser_cli/fixtures/w6_navigation_transition.html".into(),
        goal_description: "Submit login form credentials, transition route into dashboard view, and refresh metrics".into(),
        steps: vec![
            WorkloadStep::Navigate {
                url: "browser_cli/fixtures/w6_navigation_transition.html".into(),
            },
            WorkloadStep::TakeSnapshot,
            WorkloadStep::Fill {
                selector: "#login-username".into(),
                value: "admin".into(),
            },
            WorkloadStep::Fill {
                selector: "#login-password".into(),
                value: "password123".into(),
            },
            WorkloadStep::Click {
                selector: "#login-submit-btn".into(),
            },
            WorkloadStep::TakeSnapshot,
            WorkloadStep::AssertText {
                selector: "#dashboard-status".into(),
                expected: "Authenticated as admin".into(),
            },
            WorkloadStep::Click {
                selector: "#refresh-dashboard-btn".into(),
            },
            WorkloadStep::TakeSnapshot,
        ],
    }
}
