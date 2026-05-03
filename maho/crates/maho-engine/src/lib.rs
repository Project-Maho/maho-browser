//! WebViewEngine implementations for each platform.
//!
//! Each platform has its own module implementing the WebViewEngine trait:
//! - macOS/iOS: WKWebView (via objc2)
//! - Linux: WebKitGTK (via webkit2gtk)
//! - Windows: WebView2 (via webview2-com)
//!
//! This crate also provides a `StubWebViewEngine` for tests and trait
//! validation. It is intentionally a lightweight demo/test engine rather than
//! production browser logic, but it still tracks realistic state transitions so
//! integration tests can exercise the full trait surface without a real engine.

pub use maho_types::traits::webview_engine::{WebViewConfig, WebViewEngine};

use maho_types::common::{
    Cookie, DataTypes, DateTime, FindOptions, FindResult, ImageData, ImageFormat, JsValue, Rect,
    ScriptWorld, ScrollPosition, TabSnapshot, Url,
};
use maho_types::events::core_update::MahoError;

// ---------------------------------------------------------------------------
// StubWebViewEngine
// ---------------------------------------------------------------------------

/// A lightweight test/demo implementation of [`WebViewEngine`].
///
/// Maintains realistic internal state so callers can exercise the full trait
/// surface without any platform dependencies. Every mutating method updates
/// the corresponding field; every query method reads it back.
#[derive(Debug)]
pub struct StubWebViewEngine {
    created: bool,
    destroyed: bool,
    suspended: bool,
    loading: bool,
    muted: bool,
    title: Option<String>,
    url: Option<Url>,
    user_agent: String,
    zoom_level: f64,
    progress: f64,
    history: Vec<Url>,
    history_index: isize, // -1 = no history
    injected_scripts: Vec<(String, ScriptWorld)>,
    cookies: Vec<Cookie>,
}

impl Default for StubWebViewEngine {
    fn default() -> Self {
        Self::new()
    }
}

impl StubWebViewEngine {
    pub fn new() -> Self {
        Self {
            created: false,
            destroyed: false,
            suspended: false,
            loading: false,
            muted: false,
            title: None,
            url: None,
            user_agent: String::from("Maho/0.1 StubEngine"),
            zoom_level: 1.0,
            progress: 0.0,
            history: Vec::new(),
            history_index: -1,
            injected_scripts: Vec::new(),
            cookies: Vec::new(),
        }
    }

    fn ensure_alive(&self) -> Result<(), MahoError> {
        if self.destroyed {
            return Err(MahoError {
                code: "ENGINE_DESTROYED".into(),
                message: "WebViewEngine has been destroyed".into(),
                details: None,
            });
        }
        if !self.created {
            return Err(MahoError {
                code: "ENGINE_NOT_CREATED".into(),
                message: "WebViewEngine has not been created yet".into(),
                details: None,
            });
        }
        Ok(())
    }

    fn push_url(&mut self, url: Url) {
        // Truncate forward history when navigating to a new page.
        let new_index = self.history_index + 1;
        self.history.truncate(new_index as usize);
        self.history.push(url.clone());
        self.history_index = new_index;
        self.url = Some(url);
        self.loading = true;
        self.progress = 0.5; // Simulate partial load
    }

    fn finish_loading(&mut self) {
        self.loading = false;
        self.progress = 1.0;
    }
}

impl WebViewEngine for StubWebViewEngine {
    fn create(&mut self, config: WebViewConfig) -> Result<(), MahoError> {
        if self.created {
            return Err(MahoError {
                code: "ALREADY_CREATED".into(),
                message: "Engine already created".into(),
                details: None,
            });
        }
        self.created = true;
        if let Some(ua) = config.user_agent {
            self.user_agent = ua;
        }
        if let Some(url) = config.initial_url {
            self.push_url(url);
            self.finish_loading();
            self.title = Some("Stub Page".into());
        }
        Ok(())
    }

    fn destroy(&mut self) {
        self.destroyed = true;
        self.created = false;
        self.loading = false;
        self.url = None;
        self.title = None;
    }

    fn navigate(&mut self, url: &Url) -> Result<(), MahoError> {
        self.ensure_alive()?;
        self.push_url(url.clone());
        self.finish_loading();
        self.title = Some(format!("Stub: {}", url.0));
        Ok(())
    }

    fn go_back(&mut self) -> bool {
        if self.history_index > 0 {
            self.history_index -= 1;
            self.url = Some(self.history[self.history_index as usize].clone());
            self.title = Some(format!("Stub: {}", self.url.as_ref().unwrap().0));
            true
        } else {
            false
        }
    }

    fn go_forward(&mut self) -> bool {
        if (self.history_index + 1) < self.history.len() as isize {
            self.history_index += 1;
            self.url = Some(self.history[self.history_index as usize].clone());
            self.title = Some(format!("Stub: {}", self.url.as_ref().unwrap().0));
            true
        } else {
            false
        }
    }

    fn reload(&mut self) {
        if self.created && !self.destroyed {
            self.loading = true;
            self.progress = 0.5;
            self.finish_loading();
        }
    }

    fn stop(&mut self) {
        self.loading = false;
    }

    fn capture_state(&self) -> Result<TabSnapshot, MahoError> {
        self.ensure_alive()?;
        Ok(TabSnapshot {
            url: self.url.as_ref().map_or_else(String::new, |u| u.0.clone()),
            title: self.title.clone().unwrap_or_default(),
            scroll_position: ScrollPosition::default(),
            interaction_state: Vec::new(),
            captured_at: DateTime("1970-01-01T00:00:00Z".into()),
        })
    }

    fn restore_state(&mut self, snapshot: &TabSnapshot) -> Result<(), MahoError> {
        self.ensure_alive()?;
        let url = Url(snapshot.url.clone());
        self.push_url(url);
        self.finish_loading();
        self.title = Some(snapshot.title.clone());
        Ok(())
    }

    fn inject_script(&mut self, js: &str, world: ScriptWorld) -> Result<(), MahoError> {
        self.ensure_alive()?;
        self.injected_scripts.push((js.to_string(), world));
        Ok(())
    }

    fn evaluate_script(&mut self, js: &str) -> Result<JsValue, MahoError> {
        self.ensure_alive()?;
        // Simple stub: if the script is a string literal, return it; otherwise null.
        if js.starts_with('"') && js.ends_with('"') {
            Ok(JsValue::String(js[1..js.len() - 1].to_string()))
        } else if js == "document.title" {
            Ok(JsValue::String(
                self.title.clone().unwrap_or_else(|| "Untitled".into()),
            ))
        } else {
            Ok(JsValue::Null)
        }
    }

    fn suspend(&mut self) -> Result<(), MahoError> {
        self.ensure_alive()?;
        self.suspended = true;
        Ok(())
    }

    fn resume(&mut self) -> Result<(), MahoError> {
        self.ensure_alive()?;
        self.suspended = false;
        Ok(())
    }

    fn is_suspended(&self) -> bool {
        self.suspended
    }

    fn get_title(&self) -> Option<String> {
        self.title.clone()
    }

    fn get_url(&self) -> Option<Url> {
        self.url.clone()
    }

    fn get_favicon(&self) -> Option<ImageData> {
        if self.url.is_some() {
            // Return a 1x1 transparent PNG stub.
            Some(ImageData {
                data: vec![0u8; 4],
                width: 1,
                height: 1,
                format: ImageFormat::Png,
            })
        } else {
            None
        }
    }

    fn is_loading(&self) -> bool {
        self.loading
    }

    fn estimated_progress(&self) -> f64 {
        self.progress
    }

    fn can_go_back(&self) -> bool {
        self.history_index > 0
    }

    fn can_go_forward(&self) -> bool {
        (self.history_index + 1) < self.history.len() as isize
    }

    fn find_in_page(
        &mut self,
        _query: &str,
        _options: &FindOptions,
    ) -> Result<FindResult, MahoError> {
        self.ensure_alive()?;
        Ok(FindResult {
            match_count: 0,
            active_match_index: 0,
        })
    }

    fn dismiss_find(&mut self) {
        // No-op in stub.
    }

    fn take_screenshot(&self, _rect: Option<&Rect>) -> Result<ImageData, MahoError> {
        self.ensure_alive()?;
        Ok(ImageData {
            data: vec![0u8; 4],
            width: 1,
            height: 1,
            format: ImageFormat::Png,
        })
    }

    fn is_playing_audio(&self) -> bool {
        false
    }

    fn set_muted(&mut self, muted: bool) {
        self.muted = muted;
    }

    fn set_user_agent(&mut self, ua: &str) {
        self.user_agent = ua.to_string();
    }

    fn set_zoom_level(&mut self, level: f64) {
        self.zoom_level = level;
    }

    fn cookies(&self, _url: Option<&Url>) -> Result<Vec<Cookie>, MahoError> {
        self.ensure_alive()?;
        Ok(self.cookies.clone())
    }

    fn clear_data(&mut self, types: &DataTypes) -> Result<(), MahoError> {
        self.ensure_alive()?;
        if types.cookies {
            self.cookies.clear();
        }
        // Other data types are no-ops in the stub.
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    fn make_config(url: Option<&str>) -> WebViewConfig {
        WebViewConfig {
            initial_url: url.map(|s| Url(s.to_string())),
            user_agent: None,
            enable_javascript: true,
            enable_webgl: false,
            private_browsing: false,
        }
    }

    #[test]
    fn test_lifecycle_create_destroy() {
        let mut engine = StubWebViewEngine::new();
        assert!(!engine.created);

        engine.create(make_config(None)).unwrap();
        assert!(engine.created);

        // Double-create should fail.
        assert!(engine.create(make_config(None)).is_err());

        engine.destroy();
        assert!(engine.destroyed);

        // Operations after destroy should fail.
        let url = Url("https://example.com".into());
        assert!(engine.navigate(&url).is_err());
    }

    #[test]
    fn test_navigation_and_history() {
        let mut engine = StubWebViewEngine::new();
        engine.create(make_config(None)).unwrap();

        let url1 = Url("https://a.com".into());
        let url2 = Url("https://b.com".into());
        let url3 = Url("https://c.com".into());

        engine.navigate(&url1).unwrap();
        engine.navigate(&url2).unwrap();
        engine.navigate(&url3).unwrap();

        assert_eq!(engine.get_url().unwrap().0, "https://c.com");
        assert!(engine.can_go_back());
        assert!(!engine.can_go_forward());

        assert!(engine.go_back());
        assert_eq!(engine.get_url().unwrap().0, "https://b.com");
        assert!(engine.can_go_forward());

        assert!(engine.go_forward());
        assert_eq!(engine.get_url().unwrap().0, "https://c.com");
    }

    #[test]
    fn test_initial_url() {
        let mut engine = StubWebViewEngine::new();
        engine
            .create(make_config(Some("https://start.com")))
            .unwrap();
        assert_eq!(engine.get_url().unwrap().0, "https://start.com");
        assert!(engine.get_title().is_some());
    }

    #[test]
    fn test_suspend_resume() {
        let mut engine = StubWebViewEngine::new();
        engine.create(make_config(None)).unwrap();

        assert!(!engine.is_suspended());
        engine.suspend().unwrap();
        assert!(engine.is_suspended());
        engine.resume().unwrap();
        assert!(!engine.is_suspended());
    }

    #[test]
    fn test_capture_restore_state() {
        let mut engine = StubWebViewEngine::new();
        engine.create(make_config(None)).unwrap();

        let url = Url("https://snapshot.test".into());
        engine.navigate(&url).unwrap();

        let snapshot = engine.capture_state().unwrap();
        assert_eq!(snapshot.url, "https://snapshot.test");

        // Restore into a fresh engine.
        let mut engine2 = StubWebViewEngine::new();
        engine2.create(make_config(None)).unwrap();
        engine2.restore_state(&snapshot).unwrap();
        assert_eq!(engine2.get_url().unwrap().0, "https://snapshot.test");
    }

    #[test]
    fn test_script_injection_and_eval() {
        let mut engine = StubWebViewEngine::new();
        engine.create(make_config(None)).unwrap();

        engine
            .inject_script("console.log('hi')", ScriptWorld::Isolated)
            .unwrap();
        assert_eq!(engine.injected_scripts.len(), 1);

        let result = engine.evaluate_script("document.title").unwrap();
        assert!(matches!(result, JsValue::String(_)));

        let result = engine.evaluate_script("unknown()").unwrap();
        assert!(matches!(result, JsValue::Null));
    }

    #[test]
    fn test_zoom_and_user_agent() {
        let mut engine = StubWebViewEngine::new();
        engine.create(make_config(None)).unwrap();

        engine.set_zoom_level(1.5);
        assert!((engine.zoom_level - 1.5).abs() < f64::EPSILON);

        engine.set_user_agent("Custom/1.0");
        assert_eq!(engine.user_agent, "Custom/1.0");
    }

    #[test]
    fn test_find_and_screenshot() {
        let mut engine = StubWebViewEngine::new();
        engine.create(make_config(None)).unwrap();

        let opts = FindOptions {
            case_sensitive: false,
            whole_word: false,
            backwards: false,
        };
        let result = engine.find_in_page("test", &opts).unwrap();
        assert_eq!(result.match_count, 0);

        engine.dismiss_find();

        let screenshot = engine.take_screenshot(None).unwrap();
        assert_eq!(screenshot.width, 1);
    }

    #[test]
    fn test_clear_data() {
        let mut engine = StubWebViewEngine::new();
        engine.create(make_config(None)).unwrap();

        let types = DataTypes {
            cookies: true,
            cache: false,
            local_storage: false,
            session_storage: false,
            indexed_db: false,
            service_workers: false,
        };
        engine.clear_data(&types).unwrap();
    }

    #[test]
    fn test_audio_and_mute() {
        let mut engine = StubWebViewEngine::new();
        engine.create(make_config(None)).unwrap();

        assert!(!engine.is_playing_audio());
        assert!(!engine.muted);
        engine.set_muted(true);
        assert!(engine.muted);
    }

    #[test]
    fn test_favicon_present_after_navigation() {
        let mut engine = StubWebViewEngine::new();
        engine.create(make_config(None)).unwrap();

        // No favicon before any navigation.
        assert!(engine.get_favicon().is_none());

        engine.navigate(&Url("https://example.com".into())).unwrap();
        let favicon = engine.get_favicon().unwrap();
        assert_eq!(favicon.width, 1);
    }

    #[test]
    fn test_loading_state() {
        let mut engine = StubWebViewEngine::new();
        engine.create(make_config(None)).unwrap();

        assert!(!engine.is_loading());
        assert!((engine.estimated_progress() - 0.0).abs() < f64::EPSILON);

        // After navigate, loading finishes synchronously in stub.
        engine.navigate(&Url("https://a.com".into())).unwrap();
        assert!(!engine.is_loading());
        assert!((engine.estimated_progress() - 1.0).abs() < f64::EPSILON);
    }

    #[test]
    fn test_operations_before_create_fail() {
        let engine = StubWebViewEngine::new();
        assert!(engine.capture_state().is_err());
        assert!(engine.take_screenshot(None).is_err());
    }

    #[test]
    fn test_reload_and_stop() {
        let mut engine = StubWebViewEngine::new();
        engine.create(make_config(None)).unwrap();
        engine.navigate(&Url("https://a.com".into())).unwrap();

        engine.reload();
        assert!(!engine.is_loading()); // finish_loading called

        engine.stop();
        assert!(!engine.is_loading());
    }
}
