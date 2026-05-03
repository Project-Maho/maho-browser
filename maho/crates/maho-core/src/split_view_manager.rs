use std::collections::HashMap;

use maho_types::common::Orientation;
use maho_types::identifiers::{PaneId, TabId};
use maho_types::split_view::{SplitPane, SplitViewConfig};

pub struct SplitViewManager {
    active_config: Option<SplitViewConfig>,
    pane_counter: u32,
    pane_index_map: HashMap<PaneId, usize>,
}

impl Default for SplitViewManager {
    fn default() -> Self {
        Self::new()
    }
}

impl SplitViewManager {
    pub fn new() -> Self {
        Self {
            active_config: None,
            pane_counter: 0,
            pane_index_map: HashMap::new(),
        }
    }

    pub fn create_split(
        &mut self,
        tab_ids: Vec<TabId>,
        orientation: Orientation,
    ) -> SplitViewConfig {
        let count = tab_ids.len();
        let ratio = 1.0 / count as f64;

        let mut panes = Vec::with_capacity(count);
        self.pane_index_map.clear();

        for (i, tab_id) in tab_ids.into_iter().enumerate() {
            let pane_id = self.next_pane_id();
            self.pane_index_map.insert(pane_id, i);
            panes.push(SplitPane { tab_id });
        }

        let ratios = vec![ratio; count];

        let config = SplitViewConfig {
            panes,
            orientation,
            ratios,
        };

        self.active_config = Some(config.clone());
        config
    }

    pub fn remove_split(&mut self, pane_id: &PaneId) -> Option<SplitViewConfig> {
        let index = match self.pane_index_map.get(pane_id) {
            Some(&idx) => idx,
            None => return self.active_config.clone(),
        };

        let config = self.active_config.as_mut()?;

        if config.panes.len() <= 2 {
            self.active_config = None;
            self.pane_index_map.clear();
            return None;
        }

        let removed_ratio = config.ratios[index];
        config.panes.remove(index);
        config.ratios.remove(index);

        let remaining_total: f64 = config.ratios.iter().sum();
        if remaining_total > 0.0 {
            let scale = (remaining_total + removed_ratio) / remaining_total;
            for r in config.ratios.iter_mut() {
                *r *= scale;
            }
        }

        self.pane_index_map.remove(pane_id);
        let mut rebuilt = HashMap::new();
        for (pid, &old_idx) in &self.pane_index_map {
            let new_idx = if old_idx > index {
                old_idx - 1
            } else {
                old_idx
            };
            rebuilt.insert(pid.clone(), new_idx);
        }
        self.pane_index_map = rebuilt;

        self.active_config.clone()
    }

    pub fn resize_split(&mut self, pane_id: &PaneId, ratio: f64) {
        let index = match self.pane_index_map.get(pane_id) {
            Some(&idx) => idx,
            None => return,
        };

        let config = match self.active_config.as_mut() {
            Some(c) => c,
            None => return,
        };

        if config.ratios.len() < 2 {
            return;
        }

        let old_ratio = config.ratios[index];
        let delta = ratio - old_ratio;

        let others_total: f64 = config
            .ratios
            .iter()
            .enumerate()
            .filter(|&(i, _)| i != index)
            .map(|(_, r)| r)
            .sum();

        if others_total <= 0.0 {
            return;
        }

        config.ratios[index] = ratio;

        let scale = (others_total - delta) / others_total;
        for (i, r) in config.ratios.iter_mut().enumerate() {
            if i != index {
                *r *= scale;
            }
        }
    }

    pub fn get_config(&self) -> Option<&SplitViewConfig> {
        self.active_config.as_ref()
    }

    pub fn get_pane_ids(&self) -> Vec<PaneId> {
        self.pane_index_map.keys().cloned().collect()
    }

    pub fn close_all_splits(&mut self) {
        self.active_config = None;
        self.pane_index_map.clear();
        self.pane_counter = 0;
    }

    fn next_pane_id(&mut self) -> PaneId {
        self.pane_counter += 1;
        PaneId::generate()
    }
}
