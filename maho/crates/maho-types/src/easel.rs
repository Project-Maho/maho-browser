use serde::{Deserialize, Serialize};

use crate::common::{DrawPath, ImageData, Rect, Url, Viewport};
use crate::identifiers::EaselId;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum CanvasItem {
    WebEmbed { url: Url, rect: Rect },
    Note { content: String, rect: Rect },
    Drawing { paths: Vec<DrawPath>, rect: Rect },
    Image { data: ImageData, rect: Rect },
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Easel {
    pub id: EaselId,
    pub name: String,
    pub canvas_items: Vec<CanvasItem>,
    pub viewport: Viewport,
}
