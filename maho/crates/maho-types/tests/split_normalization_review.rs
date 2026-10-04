use maho_types::common::Orientation;
use maho_types::split_view::{SplitPane, SplitViewConfig};
use maho_types::identifiers::TabId;

#[test]
fn review_invalid_legacy_ratios_return_false_without_panicking() {
    for ratios in [vec![0.0, 1.0], vec![-1.0, 1.0]] {
        let mut config = SplitViewConfig {
            panes: vec![SplitPane { tab_id: TabId::new("first") }, SplitPane { tab_id: TabId::new("second") }],
            orientation: Orientation::Horizontal,
            ratios,
            layout: None,
        };
        assert!(!config.normalize_layout());
    }
}
