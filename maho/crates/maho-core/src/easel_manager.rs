use std::collections::HashMap;

use maho_types::common::{Point, Viewport};
use maho_types::easel::{CanvasItem, Easel};
use maho_types::identifiers::EaselId;

pub struct EaselManager {
    easels: HashMap<EaselId, Easel>,
}

impl Default for EaselManager {
    fn default() -> Self {
        Self::new()
    }
}

impl EaselManager {
    pub fn new() -> Self {
        Self {
            easels: HashMap::new(),
        }
    }

    pub fn create_easel(&mut self, name: String) -> Easel {
        let easel = Easel {
            id: EaselId::generate(),
            name,
            canvas_items: Vec::new(),
            viewport: Viewport {
                offset: Point { x: 0.0, y: 0.0 },
                zoom: 1.0,
            },
        };
        self.easels.insert(easel.id.clone(), easel.clone());
        easel
    }

    pub fn get_easel(&self, id: &EaselId) -> Option<&Easel> {
        self.easels.get(id)
    }

    pub fn get_all_easels(&self) -> Vec<&Easel> {
        self.easels.values().collect()
    }

    pub fn delete_easel(&mut self, id: &EaselId) -> Option<Easel> {
        self.easels.remove(id)
    }

    pub fn update_easel(
        &mut self,
        id: &EaselId,
        name: Option<String>,
        items: Option<Vec<CanvasItem>>,
        viewport: Option<Viewport>,
    ) -> bool {
        if let Some(easel) = self.easels.get_mut(id) {
            if let Some(n) = name {
                easel.name = n;
            }
            if let Some(i) = items {
                easel.canvas_items = i;
            }
            if let Some(v) = viewport {
                easel.viewport = v;
            }
            true
        } else {
            false
        }
    }

    pub fn restore_easel(&mut self, easel: Easel) {
        self.easels.insert(easel.id.clone(), easel);
    }

    pub fn get_view_models(&self) -> Vec<EaselViewModel> {
        self.easels
            .values()
            .map(|e| EaselViewModel {
                id: e.id.to_string(),
                name: e.name.clone(),
                item_count: e.canvas_items.len(),
            })
            .collect()
    }
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct EaselViewModel {
    pub id: String,
    pub name: String,
    pub item_count: usize,
}
