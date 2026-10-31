use serde::{Deserialize, Serialize};

use crate::common::Orientation;
use crate::identifiers::TabId;

pub const MIN_SPLIT_PANES: usize = 2;
pub const MAX_SPLIT_PANES: usize = 4;

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SplitPane {
    pub tab_id: TabId,
}

/// Authoritative binary split topology. Leaves reference entries in
/// `SplitViewConfig::panes` by index; their traversal order must be exactly
/// `0..panes.len()`.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum SplitLayoutNode {
    Pane {
        pane_index: usize,
    },
    Split {
        orientation: Orientation,
        ratio: f64,
        first: Box<SplitLayoutNode>,
        second: Box<SplitLayoutNode>,
    },
}

impl SplitLayoutNode {
    pub fn create_two_pane(orientation: Orientation, ratio: f64) -> Option<Self> {
        if !valid_ratio(ratio) {
            return None;
        }
        Some(Self::Split {
            orientation,
            ratio,
            first: Box::new(Self::Pane { pane_index: 0 }),
            second: Box::new(Self::Pane { pane_index: 1 }),
        })
    }

    pub fn validate_for_pane_count(&self, pane_count: usize) -> bool {
        if !(MIN_SPLIT_PANES..=MAX_SPLIT_PANES).contains(&pane_count) {
            return false;
        }
        let mut leaves = Vec::with_capacity(pane_count);
        if !self.collect_valid_leaves(&mut leaves) {
            return false;
        }
        leaves == (0..pane_count).collect::<Vec<_>>()
    }

    pub fn leaf_order(&self) -> Vec<usize> {
        let mut leaves = Vec::new();
        self.collect_leaves(&mut leaves);
        leaves
    }

    pub fn root_orientation(&self) -> Option<Orientation> {
        match self {
            Self::Pane { .. } => None,
            Self::Split { orientation, .. } => Some(orientation.clone()),
        }
    }

    pub fn root_ratio(&self) -> Option<f64> {
        match self {
            Self::Pane { .. } => None,
            Self::Split { ratio, .. } => Some(*ratio),
        }
    }

    /// Replaces `target_leaf` with a new split. `insert_before` controls the new
    /// pane's visual order. Leaves are renumbered after insertion.
    pub fn insert_pane_at_leaf(
        &mut self,
        target_leaf: usize,
        orientation: Orientation,
        ratio: f64,
        insert_before: bool,
    ) -> bool {
        let pane_count = self.leaf_order().len();
        if pane_count >= MAX_SPLIT_PANES || !valid_ratio(ratio) {
            return false;
        }
        if !self.insert_at_leaf(target_leaf, &orientation, ratio, insert_before) {
            return false;
        }
        self.renumber_leaves(&mut 0);
        self.validate_for_pane_count(pane_count + 1)
    }

    /// Removes a pane, collapses its parent split, and renumbers the remaining
    /// leaves. A two-pane tree cannot be reduced because one pane is not a split.
    pub fn remove_pane(&mut self, pane_index: usize) -> bool {
        let pane_count = self.leaf_order().len();
        if pane_count <= MIN_SPLIT_PANES {
            return false;
        }
        let Some(replacement) = self.remove_leaf(pane_index) else {
            return false;
        };
        *self = replacement;
        self.renumber_leaves(&mut 0);
        self.validate_for_pane_count(pane_count - 1)
    }

    pub fn leaf_ratios(&self, pane_count: usize) -> Option<Vec<f64>> {
        if !self.validate_for_pane_count(pane_count) {
            return None;
        }
        let mut ratios = vec![0.0; pane_count];
        self.collect_leaf_ratios(1.0, &mut ratios);
        Some(ratios)
    }

    /// Backward-compatible pane resizing. The divider directly containing the
    /// target leaf is adjusted so that leaf reaches the requested global weight;
    /// all other topology and descendant divider ratios remain unchanged.
    pub fn resize_pane_weight(&mut self, pane_index: usize, weight: f64) -> bool {
        if !valid_ratio(weight) {
            return false;
        }
        self.resize_leaf_at_weight(pane_index, weight, 1.0)
    }

    fn collect_valid_leaves(&self, leaves: &mut Vec<usize>) -> bool {
        match self {
            Self::Pane { pane_index } => {
                leaves.push(*pane_index);
                true
            }
            Self::Split {
                ratio,
                first,
                second,
                ..
            } => {
                valid_ratio(*ratio)
                    && first.collect_valid_leaves(leaves)
                    && second.collect_valid_leaves(leaves)
            }
        }
    }

    fn collect_leaves(&self, leaves: &mut Vec<usize>) {
        match self {
            Self::Pane { pane_index } => leaves.push(*pane_index),
            Self::Split { first, second, .. } => {
                first.collect_leaves(leaves);
                second.collect_leaves(leaves);
            }
        }
    }

    fn insert_at_leaf(
        &mut self,
        target_leaf: usize,
        orientation: &Orientation,
        ratio: f64,
        insert_before: bool,
    ) -> bool {
        match self {
            Self::Pane { pane_index } if *pane_index == target_leaf => {
                let existing = self.clone();
                let inserted = Self::Pane {
                    pane_index: target_leaf,
                };
                let (first, second) = if insert_before {
                    (inserted, existing)
                } else {
                    (existing, inserted)
                };
                *self = Self::Split {
                    orientation: orientation.clone(),
                    ratio,
                    first: Box::new(first),
                    second: Box::new(second),
                };
                true
            }
            Self::Pane { .. } => false,
            Self::Split { first, second, .. } => {
                first.insert_at_leaf(target_leaf, orientation, ratio, insert_before)
                    || second.insert_at_leaf(target_leaf, orientation, ratio, insert_before)
            }
        }
    }

    /// Returns the replacement subtree when the pane was found.
    fn remove_leaf(&mut self, pane_index: usize) -> Option<Self> {
        let Self::Split { first, second, .. } = self else {
            return None;
        };
        if matches!(first.as_ref(), Self::Pane { pane_index: index } if *index == pane_index) {
            return Some((**second).clone());
        }
        if matches!(second.as_ref(), Self::Pane { pane_index: index } if *index == pane_index) {
            return Some((**first).clone());
        }
        if let Some(replacement) = first.remove_leaf(pane_index) {
            **first = replacement;
            return Some(self.clone());
        }
        if let Some(replacement) = second.remove_leaf(pane_index) {
            **second = replacement;
            return Some(self.clone());
        }
        None
    }

    fn renumber_leaves(&mut self, next_index: &mut usize) {
        match self {
            Self::Pane { pane_index } => {
                *pane_index = *next_index;
                *next_index += 1;
            }
            Self::Split { first, second, .. } => {
                first.renumber_leaves(next_index);
                second.renumber_leaves(next_index);
            }
        }
    }

    fn collect_leaf_ratios(&self, weight: f64, ratios: &mut [f64]) {
        match self {
            Self::Pane { pane_index } => ratios[*pane_index] = weight,
            Self::Split {
                ratio,
                first,
                second,
                ..
            } => {
                first.collect_leaf_ratios(weight * ratio, ratios);
                second.collect_leaf_ratios(weight * (1.0 - ratio), ratios);
            }
        }
    }

    fn resize_leaf_at_weight(
        &mut self,
        pane_index: usize,
        target_weight: f64,
        subtree_weight: f64,
    ) -> bool {
        let Self::Split {
            ratio,
            first,
            second,
            ..
        } = self
        else {
            return false;
        };

        if matches!(first.as_ref(), Self::Pane { pane_index: index } if *index == pane_index) {
            let local_ratio = target_weight / subtree_weight;
            if valid_ratio(local_ratio) {
                *ratio = local_ratio;
                return true;
            }
            return false;
        }
        if matches!(second.as_ref(), Self::Pane { pane_index: index } if *index == pane_index) {
            let local_ratio = 1.0 - target_weight / subtree_weight;
            if valid_ratio(local_ratio) {
                *ratio = local_ratio;
                return true;
            }
            return false;
        }

        let first_weight = subtree_weight * *ratio;
        if first.resize_leaf_at_weight(pane_index, target_weight, first_weight) {
            return true;
        }
        let second_weight = subtree_weight * (1.0 - *ratio);
        second.resize_leaf_at_weight(pane_index, target_weight, second_weight)
    }
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct SplitViewConfig {
    pub panes: Vec<SplitPane>,
    /// Legacy root orientation retained for old readers.
    pub orientation: Orientation,
    /// Legacy effective pane weights retained for old readers.
    pub ratios: Vec<f64>,
    /// Authoritative topology. Missing legacy data is migrated on load.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub layout: Option<SplitLayoutNode>,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SplitViewInfo {
    pub tab_ids: Vec<TabId>,
    pub orientation: Orientation,
    pub layout: Option<SplitLayoutNode>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SplitConfig {
    pub tab_ids: Vec<TabId>,
    pub orientation: Orientation,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub layout: Option<SplitLayoutNode>,
}

impl SplitConfig {
    pub fn into_split_view_config(self) -> Option<SplitViewConfig> {
        let pane_count = self.tab_ids.len();
        if !(MIN_SPLIT_PANES..=MAX_SPLIT_PANES).contains(&pane_count) {
            return None;
        }

        let layout = match self.layout {
            Some(layout) if layout.validate_for_pane_count(pane_count) => layout,
            Some(_) => return None,
            None => build_legacy_layout(pane_count, &self.orientation, &[]),
        };
        let orientation = layout.root_orientation()?;
        let ratios = layout.leaf_ratios(pane_count)?;
        Some(SplitViewConfig {
            panes: self
                .tab_ids
                .into_iter()
                .map(|tab_id| SplitPane { tab_id })
                .collect(),
            orientation,
            ratios,
            layout: Some(layout),
        })
    }
}

impl SplitViewConfig {
    pub fn is_empty(&self) -> bool {
        self.panes.is_empty()
    }

    /// Validates new data or materializes a concrete topology from legacy flat
    /// fields. Once normalized, `layout` is always the source of truth.
    pub fn normalize_layout(&mut self) -> bool {
        let pane_count = self.panes.len();
        if !(MIN_SPLIT_PANES..=MAX_SPLIT_PANES).contains(&pane_count) {
            return false;
        }
        let layout = match self.layout.take() {
            Some(layout) if layout.validate_for_pane_count(pane_count) => layout,
            Some(_) => return false,
            None => build_legacy_layout(pane_count, &self.orientation, &self.ratios),
        };
        if !layout.validate_for_pane_count(pane_count) {
            return false;
        }
        self.orientation = layout.root_orientation().expect("validated split root");
        self.ratios = layout
            .leaf_ratios(pane_count)
            .expect("validated split ratios");
        self.layout = Some(layout);
        true
    }

    pub fn layout_tree(&self) -> Option<SplitLayoutNode> {
        self.layout.clone().or_else(|| {
            if (MIN_SPLIT_PANES..=MAX_SPLIT_PANES).contains(&self.panes.len()) {
                Some(build_legacy_layout(
                    self.panes.len(),
                    &self.orientation,
                    &self.ratios,
                ))
            } else {
                None
            }
        })
    }

    pub fn as_info(&self) -> SplitViewInfo {
        SplitViewInfo {
            tab_ids: self.panes.iter().map(|pane| pane.tab_id.clone()).collect(),
            orientation: self.orientation.clone(),
            layout: self.layout_tree(),
        }
    }
}

fn build_legacy_layout(
    pane_count: usize,
    orientation: &Orientation,
    ratios: &[f64],
) -> SplitLayoutNode {
    fn build(
        start: usize,
        count: usize,
        orientation: &Orientation,
        ratios: &[f64],
    ) -> SplitLayoutNode {
        if count == 1 {
            return SplitLayoutNode::Pane { pane_index: start };
        }
        let subtree_sum: f64 = (start..start + count)
            .map(|index| ratios.get(index).copied().unwrap_or(1.0).max(0.0))
            .sum();
        let first_weight = ratios.get(start).copied().unwrap_or(1.0).max(0.0);
        let ratio = if subtree_sum > 0.0 {
            first_weight / subtree_sum
        } else {
            1.0 / count as f64
        };
        SplitLayoutNode::Split {
            orientation: orientation.clone(),
            ratio,
            first: Box::new(SplitLayoutNode::Pane { pane_index: start }),
            second: Box::new(build(start + 1, count - 1, orientation, ratios)),
        }
    }
    build(0, pane_count, orientation, ratios)
}

fn valid_ratio(ratio: f64) -> bool {
    ratio.is_finite() && ratio > 0.0 && ratio < 1.0
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq, Hash)]
#[serde(rename_all = "camelCase")]
pub struct StablePaneIdentity {
    pub url: String,
    pub tab_creation_time_micros: u64,
    pub window_session_uuid: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SplitViewPersistConfig {
    pub panes: Vec<StablePaneIdentity>,
    pub orientation: Orientation,
    pub ratios: Vec<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub layout: Option<SplitLayoutNode>,
}
