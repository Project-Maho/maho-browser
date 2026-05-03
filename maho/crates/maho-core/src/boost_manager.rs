use std::collections::HashMap;

use regex::Regex;

use maho_types::boost::{Boost, BoostUpdate, ColorOverride};
use maho_types::common::{UrlPattern, UrlPatternType};
use maho_types::identifiers::{BoostId, SpaceId};
use maho_types::traits::shell_renderer::BoostViewModel;

#[derive(Clone, Debug)]
pub struct BoostTemplate {
    pub id: &'static str,
    pub name: &'static str,
    pub description: &'static str,
    pub css: &'static str,
}

pub fn get_default_templates() -> Vec<BoostTemplate> {
    vec![
        BoostTemplate { id: "dark_mode", name: "Dark Mode", description: "Force dark background with light text", css: "html, body { background: #1a1a2e !important; color: #e0e0e0 !important; } a { color: #6cb4ee !important; }" },
        BoostTemplate { id: "sans_serif", name: "Sans Serif", description: "Override to clean sans-serif font", css: "* { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif !important; }" },
        BoostTemplate { id: "large_text", name: "Large Text", description: "Increase base font size", css: "html { font-size: 20px !important; } body { font-size: 1rem !important; line-height: 1.6 !important; }" },
        BoostTemplate { id: "hide_ads", name: "Hide Ads", description: "Hide common ad containers", css: "[class*='ad-'], [class*='ads-'], [id*='google_ads'], .adsbygoogle, [data-ad] { display: none !important; }" },
        BoostTemplate { id: "sepia", name: "Sepia Mode", description: "Warm sepia reading tone", css: "html, body { background: #f4ecd8 !important; color: #3b3121 !important; } img { filter: sepia(20%); }" },
        BoostTemplate { id: "high_contrast", name: "High Contrast", description: "Maximum contrast for readability", css: "html, body { background: #000 !important; color: #fff !important; } a { color: #ffff00 !important; }" },
        BoostTemplate { id: "no_images", name: "Hide Images", description: "Remove all images from page", css: "img, svg, video, picture, [style*='background-image'] { display: none !important; }" },
        BoostTemplate { id: "narrow_width", name: "Narrow Width", description: "Constrain content to readable width", css: "body > *, main, article, .content { max-width: 680px !important; margin-left: auto !important; margin-right: auto !important; }" },
        BoostTemplate { id: "monospace", name: "Monospace", description: "Override to monospace font", css: "* { font-family: 'SF Mono', Menlo, Monaco, 'Courier New', monospace !important; }" },
        BoostTemplate { id: "remove_fixed", name: "Remove Fixed Elements", description: "Remove sticky headers and footers", css: "[style*='position: fixed'], [style*='position: sticky'] { position: static !important; } .sticky, .fixed { position: static !important; }" },
    ]
}

pub struct BoostManager {
    boosts: HashMap<BoostId, Boost>,
    globally_enabled: bool,
    js_enabled: bool,
}

impl Default for BoostManager {
    fn default() -> Self {
        Self::new()
    }
}

impl BoostManager {
    pub fn new() -> Self {
        Self {
            boosts: HashMap::new(),
            globally_enabled: true,
            js_enabled: true,
        }
    }

    pub fn create_boost(&mut self, url_pattern: UrlPattern) -> Boost {
        let boost = Boost {
            id: BoostId::generate(),
            url_pattern,
            custom_css: None,
            custom_js: None,
            color_theme: None,
            font_override: None,
            enabled: true,
            space_scope: None,
        };
        self.boosts.insert(boost.id.clone(), boost.clone());
        boost
    }

    pub fn update_boost(&mut self, boost_id: &BoostId, changes: BoostUpdate) {
        if let Some(boost) = self.boosts.get_mut(boost_id) {
            if let Some(url_pattern) = changes.url_pattern {
                boost.url_pattern = url_pattern;
            }
            if let Some(custom_css) = changes.custom_css {
                boost.custom_css = custom_css;
            }
            if let Some(custom_js) = changes.custom_js {
                boost.custom_js = custom_js;
            }
            if let Some(color_theme) = changes.color_theme {
                boost.color_theme = color_theme;
            }
            if let Some(font_override) = changes.font_override {
                boost.font_override = font_override;
            }
            if let Some(enabled) = changes.enabled {
                boost.enabled = enabled;
            }
            if let Some(space_scope) = changes.space_scope {
                boost.space_scope = space_scope;
            }
        }
    }

    pub fn toggle_boost(&mut self, boost_id: &BoostId, enabled: bool) {
        if let Some(boost) = self.boosts.get_mut(boost_id) {
            boost.enabled = enabled;
        }
    }

    pub fn delete_boost(&mut self, boost_id: &BoostId) -> Option<Boost> {
        self.boosts.remove(boost_id)
    }

    pub fn get_boost(&self, boost_id: &BoostId) -> Option<&Boost> {
        self.boosts.get(boost_id)
    }

    pub fn get_all_boosts(&self) -> Vec<&Boost> {
        self.boosts.values().collect()
    }

    pub fn restore_boost(&mut self, boost: Boost) {
        self.boosts.insert(boost.id.clone(), boost);
    }

    pub fn get_boosts_for_url(&self, url: &str) -> Vec<&Boost> {
        self.boosts
            .values()
            .filter(|boost| boost.enabled && matches_url_pattern(&boost.url_pattern, url))
            .collect()
    }

    pub fn get_boosts_for_url_in_space(&self, url: &str, space_id: &SpaceId) -> Vec<&Boost> {
        self.boosts
            .values()
            .filter(|boost| {
                boost.enabled
                    && matches_url_pattern(&boost.url_pattern, url)
                    && (boost.space_scope.is_none() || boost.space_scope == Some(space_id.clone()))
            })
            .collect()
    }

    pub fn get_templates(&self) -> Vec<BoostTemplate> {
        get_default_templates()
    }

    pub fn create_boost_from_template(
        &mut self,
        template_id: &str,
        url_pattern: UrlPattern,
    ) -> Option<Boost> {
        let template = get_default_templates()
            .into_iter()
            .find(|t| t.id == template_id)?;
        let mut boost = self.create_boost(url_pattern);
        boost.custom_css = Some(template.css.to_string());
        self.boosts.insert(boost.id.clone(), boost.clone());
        Some(boost)
    }

    pub fn to_view_model(boost: &Boost) -> BoostViewModel {
        BoostViewModel {
            id: boost.id.clone(),
            url_pattern: boost.url_pattern.pattern.clone(),
            custom_css: boost.custom_css.clone(),
            custom_js: boost.custom_js.clone(),
            enabled: boost.enabled,
        }
    }

    pub fn get_all_view_models(&self) -> Vec<BoostViewModel> {
        self.boosts.values().map(Self::to_view_model).collect()
    }

    pub fn is_globally_enabled(&self) -> bool {
        self.globally_enabled
    }

    pub fn toggle_global(&mut self) -> bool {
        self.globally_enabled = !self.globally_enabled;
        self.globally_enabled
    }

    pub fn is_js_enabled(&self) -> bool {
        self.js_enabled
    }

    pub fn toggle_js(&mut self) -> bool {
        self.js_enabled = !self.js_enabled;
        self.js_enabled
    }

    pub fn list_boosts(&self) -> Vec<BoostViewModel> {
        self.get_all_view_models()
    }

    pub fn apply_color_override(color_override: &ColorOverride) -> String {
        let mut css_parts = Vec::new();

        if let Some(ref bg) = color_override.background {
            let rgba = format!("rgba({},{},{},{})", bg.r, bg.g, bg.b, bg.a);
            css_parts.push(format!(
                "html, body {{ background-color: {} !important; }}",
                rgba
            ));
            css_parts.push(format!(":root {{ --maho-boost-bg: {}; }}", rgba));
        }

        if let Some(ref text) = color_override.text {
            let rgba = format!("rgba({},{},{},{})", text.r, text.g, text.b, text.a);
            css_parts.push(format!(
                "html, body, p, span, div, li, td, th, h1, h2, h3, h4, h5, h6, label, pre, code {{ color: {} !important; }}",
                rgba
            ));
            css_parts.push(format!(":root {{ --maho-boost-text: {}; }}", rgba));
        }

        if let Some(ref links) = color_override.links {
            let rgba = format!("rgba({},{},{},{})", links.r, links.g, links.b, links.a);
            css_parts.push(format!(
                "a, a:visited, a:hover, a:active {{ color: {} !important; }}",
                rgba
            ));
            css_parts.push(format!(":root {{ --maho-boost-links: {}; }}", rgba));
        }

        css_parts.join("\n")
    }

    pub fn generate_boost_injection_css(boost: &Boost) -> String {
        let mut parts = Vec::new();

        if let Some(ref color_theme) = boost.color_theme {
            let color_css = Self::apply_color_override(color_theme);
            if !color_css.is_empty() {
                parts.push(color_css);
            }
        }

        if let Some(ref font) = boost.font_override {
            let mut font_parts = Vec::new();
            if let Some(ref family) = font.font_family {
                font_parts.push(format!("font-family: {} !important;", family));
            }
            if let Some(size) = font.font_size {
                font_parts.push(format!("font-size: {}px !important;", size));
            }
            if let Some(lh) = font.line_height {
                font_parts.push(format!("line-height: {} !important;", lh));
            }
            if !font_parts.is_empty() {
                parts.push(format!(
                    "html, body, p, span, div, li, td, th {{ {} }}",
                    font_parts.join(" ")
                ));
            }
        }

        if let Some(ref css) = boost.custom_css {
            if !css.is_empty() {
                parts.push(css.clone());
            }
        }

        parts.join("\n")
    }
}

fn glob_to_regex(glob: &str) -> String {
    let mut regex = String::with_capacity(glob.len() * 2);
    regex.push('^');
    for ch in glob.chars() {
        match ch {
            '*' => regex.push_str(".*"),
            '?' => regex.push('.'),
            '.' | '+' | '(' | ')' | '{' | '}' | '[' | ']' | '^' | '$' | '|' | '\\' => {
                regex.push('\\');
                regex.push(ch);
            }
            _ => regex.push(ch),
        }
    }
    regex.push('$');
    regex
}

fn matches_url_pattern(pattern: &UrlPattern, url: &str) -> bool {
    match pattern.pattern_type {
        UrlPatternType::Glob => {
            let regex_str = glob_to_regex(&pattern.pattern);
            Regex::new(&regex_str)
                .map(|re| re.is_match(url))
                .unwrap_or_else(|_| url.contains(&pattern.pattern))
        }
        UrlPatternType::Regex => Regex::new(&pattern.pattern)
            .map(|re| re.is_match(url))
            .unwrap_or_else(|_| url.contains(&pattern.pattern)),
    }
}
