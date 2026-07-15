use crate::common::{
    Cookie, DataTypes, FindOptions, FindResult, ImageData, JsValue, Rect, ScriptWorld, TabSnapshot,
    Url,
};
use crate::events::core_update::MahoError;

#[derive(Clone, Debug)]
pub struct WebViewConfig {
    pub initial_url: Option<Url>,
    pub user_agent: Option<String>,
    pub enable_javascript: bool,
    pub enable_webgl: bool,
    pub private_browsing: bool,
}

pub trait WebViewEngine {
    fn create(&mut self, config: WebViewConfig) -> Result<(), MahoError>;
    fn destroy(&mut self);

    fn navigate(&mut self, url: &Url) -> Result<(), MahoError>;
    fn go_back(&mut self) -> bool;
    fn go_forward(&mut self) -> bool;
    fn reload(&mut self);
    fn stop(&mut self);

    fn capture_state(&self) -> Result<TabSnapshot, MahoError>;
    fn restore_state(&mut self, snapshot: &TabSnapshot) -> Result<(), MahoError>;

    fn inject_script(&mut self, js: &str, world: ScriptWorld) -> Result<(), MahoError>;
    fn evaluate_script(&mut self, js: &str) -> Result<JsValue, MahoError>;

    fn suspend(&mut self) -> Result<(), MahoError>;
    fn resume(&mut self) -> Result<(), MahoError>;
    fn is_suspended(&self) -> bool;

    fn get_title(&self) -> Option<String>;
    fn get_url(&self) -> Option<Url>;
    fn get_favicon(&self) -> Option<ImageData>;
    fn is_loading(&self) -> bool;
    fn estimated_progress(&self) -> f64;

    fn can_go_back(&self) -> bool;
    fn can_go_forward(&self) -> bool;

    fn find_in_page(&mut self, query: &str, options: &FindOptions)
        -> Result<FindResult, MahoError>;
    fn dismiss_find(&mut self);

    fn take_screenshot(&self, rect: Option<&Rect>) -> Result<ImageData, MahoError>;
    fn is_playing_audio(&self) -> bool;
    fn set_muted(&mut self, muted: bool);

    fn set_user_agent(&mut self, ua: &str);
    fn set_zoom_level(&mut self, level: f64);

    fn cookies(&self, url: Option<&Url>) -> Result<Vec<Cookie>, MahoError>;
    fn clear_data(&mut self, types: &DataTypes) -> Result<(), MahoError>;
}
