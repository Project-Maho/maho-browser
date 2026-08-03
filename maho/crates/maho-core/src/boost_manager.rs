use crate::boost_css_sanitizer::{sanitize_custom_css, sanitize_font_family};
use maho_types::boost::{
    Boost, BoostUpdate, CaseMode, ColorBoost, Point, SizeMode, TypographyBoost, ZenCompatBoostData,
};
use maho_types::common::DateTime;
use maho_types::identifiers::BoostId;
use rand::Rng;
use std::collections::HashMap;

const MAX_BOOST_NAME_BYTES: usize = 256;
const MAX_FONT_FAMILY_BYTES: usize = 256;
const MAX_BOOST_IMPORT_BYTES: usize = 512 * 1024;
const MAX_CUSTOM_CSS_BYTES: usize = 256 * 1024;
const MAX_ZAP_SELECTORS: usize = 256;
const MAX_ZAP_SELECTOR_BYTES: usize = 2 * 1024;

pub struct BoostManager {
    boosts: HashMap<BoostId, Boost>,
    active_boosts: HashMap<String, BoostId>,
    previous_active_boosts: HashMap<String, BoostId>,
}

impl Default for BoostManager {
    fn default() -> Self {
        Self::new()
    }
}

fn zen_defaults() -> (ColorBoost, TypographyBoost) {
    let color = ColorBoost {
        dot_pos: Point { x: 0.76, y: 0.66 },
        dot_distance: 0.0,
        dot_angle_deg: 0.0,
        secondary_dot_pos: Point { x: 0.5, y: 0.81 },
        secondary_dot_angle_deg_delta: 55.0,
        magic_theme: false,
        color_boost_enabled: true,
        smart_invert: false,
        contrast: 0.75,
        brightness: 0.5,
        saturation: 0.5,
    };
    let typography = TypographyBoost {
        font_family: String::new(),
        case_mode: CaseMode::None,
        size_mode: SizeMode::K100,
    };
    (color, typography)
}

impl BoostManager {
    pub fn new() -> Self {
        Self {
            boosts: HashMap::new(),
            active_boosts: HashMap::new(),
            previous_active_boosts: HashMap::new(),
        }
    }

    pub fn list_for_domain(&self, domain: &str) -> Vec<Boost> {
        self.boosts
            .values()
            .filter(|b| b.domain == domain)
            .cloned()
            .collect()
    }

    pub fn get_boost(&self, boost_id: &BoostId) -> Option<&Boost> {
        self.boosts.get(boost_id)
    }

    pub fn get_all_boosts(&self) -> Vec<&Boost> {
        self.boosts.values().collect()
    }

    pub fn create(&mut self, domain: String, name: String) -> Boost {
        self.create_with_name(domain, name)
    }

    fn create_with_name(&mut self, domain: String, name: String) -> Boost {
        let id = BoostId::generate();
        let (color, typography) = zen_defaults();
        let boost = Boost {
            id: id.clone(),
            domain,
            name,
            color,
            typography,
            zap_selectors: vec![],
            custom_css: String::new(),
            change_was_made: false,
            created_at: DateTime::now(),
            updated_at: DateTime::now(),
        };
        self.boosts.insert(id, boost.clone());
        boost
    }

    pub fn create_temp(&mut self, domain: String) -> Boost {
        if let Some(current_active) = self.active_boosts.get(&domain).cloned() {
            self.previous_active_boosts
                .insert(domain.clone(), current_active);
        }
        let boost = self.create_with_name(domain.clone(), "My Boost".to_string());
        self.active_boosts.insert(domain, boost.id.clone());
        boost
    }

    pub fn commit_boost(&mut self, id: &BoostId) -> Option<Boost> {
        let boost = self.boosts.get_mut(id)?;
        boost.change_was_made = true;
        boost.updated_at = DateTime::now();
        Some(boost.clone())
    }

    pub fn discard_boost(&mut self, id: &BoostId) -> Option<Option<BoostId>> {
        let boost = self.boosts.get(id)?;
        let domain = boost.domain.clone();
        let change_was_made = boost.change_was_made;

        if change_was_made {
            return None;
        }

        self.boosts.remove(id);
        if self.active_boosts.get(&domain) == Some(id) {
            self.active_boosts.remove(&domain);
        }

        let prev = self.previous_active_boosts.remove(&domain);
        if let Some(ref prev_id) = prev {
            if self.boosts.contains_key(prev_id) {
                self.active_boosts.insert(domain, prev_id.clone());
            }
        }
        Some(prev)
    }

    pub fn update(&mut self, boost_id: &BoostId, changes: BoostUpdate) -> Option<Boost> {
        if !is_valid_update_payload(&changes) {
            return None;
        }
        let boost = self.boosts.get_mut(boost_id)?;
        let mut any_change = false;

        if let Some(ref name) = changes.name {
            if boost.name != *name {
                boost.name = name.clone();
                any_change = true;
            }
        }
        if let Some(color_update) = changes.color {
            if let Some(enable) = color_update.color_boost_enabled {
                if boost.color.color_boost_enabled != enable {
                    boost.color.color_boost_enabled = enable;
                    any_change = true;
                }
            }
            if let Some(deg) = color_update.dot_angle_deg {
                if (boost.color.dot_angle_deg - deg).abs() > f64::EPSILON {
                    boost.color.dot_angle_deg = deg;
                    any_change = true;
                }
            }
            if let Some(delta) = color_update.secondary_dot_angle_deg_delta {
                if (boost.color.secondary_dot_angle_deg_delta - delta).abs() > f64::EPSILON {
                    boost.color.secondary_dot_angle_deg_delta = delta;
                    any_change = true;
                }
            }
            if let Some(b) = color_update.brightness {
                if (boost.color.brightness - b).abs() > f64::EPSILON {
                    boost.color.brightness = b;
                    any_change = true;
                }
            }
            if let Some(s) = color_update.saturation {
                if (boost.color.saturation - s).abs() > f64::EPSILON {
                    boost.color.saturation = s;
                    any_change = true;
                }
            }
            if let Some(c) = color_update.contrast {
                if (boost.color.contrast - c).abs() > f64::EPSILON {
                    boost.color.contrast = c;
                    any_change = true;
                }
            }
            if let Some(auto) = color_update.magic_theme {
                if boost.color.magic_theme != auto {
                    boost.color.magic_theme = auto;
                    any_change = true;
                }
            }
            if let Some(smart) = color_update.smart_invert {
                if boost.color.smart_invert != smart {
                    boost.color.smart_invert = smart;
                    any_change = true;
                }
            }
            if let Some(pos) = color_update.dot_pos {
                if boost.color.dot_pos != pos {
                    boost.color.dot_pos = pos;
                    any_change = true;
                }
            }
            if let Some(dist) = color_update.dot_distance {
                if (boost.color.dot_distance - dist).abs() > f32::EPSILON {
                    boost.color.dot_distance = dist;
                    any_change = true;
                }
            }
            if let Some(pos) = color_update.secondary_dot_pos {
                if boost.color.secondary_dot_pos != pos {
                    boost.color.secondary_dot_pos = pos;
                    any_change = true;
                }
            }
        }
        if let Some(typo_update) = changes.typography {
            if let Some(ref font) = typo_update.font_family {
                if boost.typography.font_family != *font {
                    boost.typography.font_family = font.clone();
                    any_change = true;
                }
            }
            if let Some(case) = typo_update.case_mode {
                if boost.typography.case_mode != case {
                    boost.typography.case_mode = case;
                    any_change = true;
                }
            }
            if let Some(size) = typo_update.size_mode {
                if boost.typography.size_mode != size {
                    boost.typography.size_mode = size;
                    any_change = true;
                }
            }
        }
        if let Some(ref zaps) = changes.zap_selectors {
            if boost.zap_selectors != *zaps {
                boost.zap_selectors = zaps.clone();
                any_change = true;
            }
        }
        if let Some(ref css) = changes.custom_css {
            if boost.custom_css != *css {
                boost.custom_css = css.clone();
                any_change = true;
            }
        }

        if any_change {
            boost.change_was_made = true;
            boost.updated_at = DateTime::now();
        }
        Some(boost.clone())
    }

    pub fn shuffle_boost(&mut self, id: &BoostId) -> Option<Boost> {
        const COMMON_FONTS: &[&str] = &[
            "Arial",
            "Times New Roman",
            "Courier New",
            "Georgia",
            "Comic Sans MS",
            "Verdana",
            "Trebuchet MS",
            "Impact",
            "Palatino Linotype",
            "Tahoma",
            "Helvetica",
            "Garamond",
            "Century Gothic",
            "Arial Black",
            "Papyrus",
        ];

        let boost = self.boosts.get_mut(id)?;
        let mut rng = rand::thread_rng();

        let font_idx = rng.gen_range(0..COMMON_FONTS.len());
        boost.typography.font_family = COMMON_FONTS[font_idx].to_string();

        boost.color.smart_invert = rng.gen_bool(0.5);
        boost.color.magic_theme = false;
        boost.color.brightness = rng.gen_range(0.0_f64..1.0);
        boost.color.contrast = rng.gen_range(0.0_f64..1.0);
        boost.color.saturation = rng.gen_range(0.0_f64..1.0);
        boost.color.secondary_dot_angle_deg_delta = rng.gen_range(0.0_f64..360.0);

        let angle_rad: f64 = rng.gen_range(0.0..(2.0 * std::f64::consts::PI));
        let distance: f32 = rng.gen_range(0.0_f32..1.0);

        boost.color.dot_angle_deg =
            (angle_rad * 180.0 / std::f64::consts::PI + 100.0).rem_euclid(360.0);
        boost.color.dot_distance = distance;
        boost.color.dot_pos = Point {
            x: (0.5 + (angle_rad.cos() as f32) * distance * 0.5).clamp(0.0, 1.0),
            y: (0.5 + (angle_rad.sin() as f32) * distance * 0.5).clamp(0.0, 1.0),
        };

        let secondary_angle_rad =
            angle_rad + boost.color.secondary_dot_angle_deg_delta * std::f64::consts::PI / 180.0;
        boost.color.secondary_dot_pos = Point {
            x: (0.5 + (secondary_angle_rad.cos() as f32) * distance * 0.5).clamp(0.0, 1.0),
            y: (0.5 + (secondary_angle_rad.sin() as f32) * distance * 0.5).clamp(0.0, 1.0),
        };

        boost.color.color_boost_enabled = true;
        boost.change_was_made = true;
        boost.updated_at = DateTime::now();
        Some(boost.clone())
    }

    pub fn reset_boost(&mut self, id: &BoostId) -> Option<Boost> {
        let boost = self.boosts.get_mut(id)?;
        let (mut color, typography) = zen_defaults();
        // Reset means removing every page-visible effect. Keep the neutral
        // picker positions and slider values ready for the next edit, but do
        // not leave the color filter enabled on the page.
        color.color_boost_enabled = false;
        boost.color = color;
        boost.typography = typography;
        boost.zap_selectors.clear();
        boost.custom_css.clear();
        boost.change_was_made = false;
        boost.updated_at = DateTime::now();
        Some(boost.clone())
    }

    pub fn export_boost(&self, id: &BoostId) -> Option<String> {
        let boost = self.boosts.get(id)?;
        let dto = ZenCompatBoostData::from(boost);
        serde_json::to_string(&dto).ok()
    }

    pub fn import_boost(&mut self, domain: String, json: &str) -> Option<Boost> {
        if json.len() > MAX_BOOST_IMPORT_BYTES {
            return None;
        }
        // Try Zen-compat shape first
        if let Ok(dto) = serde_json::from_str::<ZenCompatBoostData>(json) {
            if let Ok(boost) = Boost::try_from((dto, domain.clone())) {
                let boost = sanitize_imported_boost(boost)?;
                self.boosts.insert(boost.id.clone(), boost.clone());
                return Some(boost);
            }
        }
        // Fallback: full Boost shape (Maho-exported legacy files)
        if let Ok(mut boost) = serde_json::from_str::<Boost>(json) {
            boost.id = BoostId::generate();
            boost.domain = domain;
            boost.created_at = DateTime::now();
            boost.updated_at = DateTime::now();
            let boost = sanitize_imported_boost(boost)?;
            self.boosts.insert(boost.id.clone(), boost.clone());
            return Some(boost);
        }
        None
    }

    pub fn append_zap_selector(&mut self, id: &BoostId, selector: String) -> Option<Boost> {
        let boost = self.boosts.get_mut(id)?;
        if selector.len() > MAX_ZAP_SELECTOR_BYTES
            || !is_safe_zap_selector(&selector)
            || (!boost.zap_selectors.contains(&selector)
                && boost.zap_selectors.len() >= MAX_ZAP_SELECTORS)
        {
            return None;
        }
        if !boost.zap_selectors.contains(&selector) {
            boost.zap_selectors.push(selector);
            boost.change_was_made = true;
            boost.updated_at = DateTime::now();
        }
        Some(boost.clone())
    }

    pub fn remove_zap_selector(&mut self, id: &BoostId, selector: &str) -> Option<Boost> {
        let boost = self.boosts.get_mut(id)?;
        let len_before = boost.zap_selectors.len();
        boost.zap_selectors.retain(|s| s != selector);
        if boost.zap_selectors.len() != len_before {
            boost.change_was_made = true;
            boost.updated_at = DateTime::now();
        }
        Some(boost.clone())
    }

    pub fn delete(&mut self, boost_id: &BoostId) -> Option<Boost> {
        let boost = self.boosts.remove(boost_id)?;
        if self.active_boosts.get(&boost.domain) == Some(&boost.id) {
            self.active_boosts.remove(&boost.domain);
        }
        Some(boost)
    }

    pub fn set_active(&mut self, domain: &str, active_boost_id: Option<BoostId>) -> bool {
        if let Some(id) = active_boost_id {
            if !self
                .boosts
                .get(&id)
                .is_some_and(|boost| boost.domain == domain)
            {
                return false;
            }
            self.active_boosts.insert(domain.to_string(), id);
        } else {
            self.active_boosts.remove(domain);
        }
        true
    }

    pub fn get_active(&self, domain: &str) -> Option<Boost> {
        let active_id = self.active_boosts.get(domain)?;
        self.get_boost(active_id)
            .filter(|boost| boost.domain == domain)
            .cloned()
    }

    /// Composes the CSS that gets injected into target webpages.
    ///
    /// The `Zen-Zaps` / `Text Format` / `USER CSS` sections mirror Zen's
    /// `ZenBoostStyles.#generateStyleString` output 1:1 (comment markers,
    /// brace spacing, multi-line body block).
    ///
    /// Color theming, smart invert, and size override are best-effort web
    /// approximations of Zen's engine-level behavior. Zen uses Gecko patches
    /// (`browsingContext.zenBoostsData` packed NSColor, `isZenBoostsInverted`,
    /// `fullZoom`); Chromium has no equivalent so we emit:
    ///   * `:root { zoom: N }` (matches Chromium's native zoom — covers layout + images)
    ///   * Merged `:root { filter: invert(1) hue-rotate(180deg) brightness(...) contrast(...) saturate(...) }`
    ///   * `html::after` overlay with `mix-blend-mode: multiply` (tints whites toward
    ///     the chosen color while preserving readability — `color` blend would
    ///     be invisible on grayscale pages because it preserves luminosity)
    ///   * Media re-invert when smart_invert is on
    ///   * Hue +180° compensation when invert is on (so the visible tint matches user intent)
    pub fn compose_css(&self, boost: &Boost, _workspace_gradient_hue_deg: Option<f64>) -> String {
        let mut css = String::new();

        let mut zap_block = String::new();
        for selector in boost.zap_selectors.iter().take(MAX_ZAP_SELECTORS) {
            if selector.len() > MAX_ZAP_SELECTOR_BYTES || !is_safe_zap_selector(selector) {
                continue;
            }
            zap_block.push_str(&format!(
                "{}:not([maho-zap-unhide]){{ display: none !important; }}\n",
                selector
            ));
        }
        if !zap_block.is_empty() {
            css.push_str("/* Zen-Zaps */\n");
            css.push_str(&zap_block);
            css.push('\n');
        }

        let font_decl = sanitize_font_family(&boost.typography.font_family)
            .filter(|font_family| !font_family.is_empty())
            .map(|font_family| format!("font-family: {font_family} !important;"))
            .unwrap_or_default();
        let case_decl = match boost.typography.case_mode {
            CaseMode::None => String::new(),
            CaseMode::Upper => "text-transform: uppercase !important;".to_string(),
            CaseMode::Lower => "text-transform: lowercase !important;".to_string(),
            CaseMode::Capitalize => "text-transform: capitalize !important;".to_string(),
        };
        if !font_decl.is_empty() || !case_decl.is_empty() {
            css.push_str("/* Text Format */\n");
            css.push_str("body * {\n");
            css.push_str(&font_decl);
            css.push('\n');
            css.push_str(&case_decl);
            css.push('\n');
            css.push_str("}\n");
        }

        let zoom = boost.typography.size_mode.as_zoom();
        if (zoom - 1.0).abs() > f32::EPSILON {
            css.push_str(&format!(
                "/* Size Override */\n:root {{ zoom: {:.4}; }}\n",
                zoom
            ));
        }

        let invert_on = boost.color.smart_invert;
        let color_on = boost.color.color_boost_enabled;

        if color_on || invert_on {
            css.push_str("/* Color & Invert */\n");

            let mut filter_parts: Vec<String> = Vec::new();
            if invert_on {
                filter_parts.push("invert(1) hue-rotate(180deg)".to_string());
            }
            if color_on {
                let brightness_pct = (50.0 + 100.0 * boost.color.brightness).clamp(10.0, 200.0);
                let contrast_pct = (50.0 + 100.0 * boost.color.contrast).clamp(10.0, 200.0);
                let saturation_pct =
                    (100.0 + 50.0 * (1.0 - boost.color.saturation)).clamp(10.0, 200.0);
                filter_parts.push(format!("brightness({:.2}%)", brightness_pct));
                filter_parts.push(format!("contrast({:.2}%)", contrast_pct));
                filter_parts.push(format!("saturate({:.2}%)", saturation_pct));
            }
            if !filter_parts.is_empty() {
                css.push_str(&format!(
                    ":root {{ filter: {} !important; }}\n",
                    filter_parts.join(" ")
                ));
            }
            if invert_on {
                css.push_str(":root, body { background-color: #fff !important; }\n");
                css.push_str(
                    "img, video, canvas, iframe, embed, object, svg image, [style*=\"url(\"] { filter: invert(1) hue-rotate(180deg) !important; }\n",
                );
                css.push_str("[style*=\"gradient(\"] { filter: none !important; }\n");
            }
            if color_on {
                let raw_hue = boost.color.dot_angle_deg.rem_euclid(360.0);
                let visible_hue = if invert_on {
                    (raw_hue + 180.0).rem_euclid(360.0)
                } else {
                    raw_hue
                };
                let raw_secondary_hue = (boost.color.dot_angle_deg
                    + boost.color.secondary_dot_angle_deg_delta)
                    .rem_euclid(360.0);
                let visible_secondary_hue = if invert_on {
                    (raw_secondary_hue + 180.0).rem_euclid(360.0)
                } else {
                    raw_secondary_hue
                };
                let hsl_sat_pct = ((1.0 - boost.color.saturation).clamp(0.0, 1.0)) * 100.0;
                let hsl_light_pct = ((0.1 + 0.9 * boost.color.brightness).clamp(0.0, 1.0)) * 100.0;
                let opacity = boost.color.contrast.clamp(0.0, 1.0);
                css.push_str(&format!(
                    "html::after {{ \
                     content: '' !important; \
                     position: fixed !important; \
                     inset: 0 !important; \
                     pointer-events: none !important; \
                     z-index: 2147483647 !important; \
                     background: linear-gradient({:.2}deg, hsl({:.2}deg, {:.2}%, {:.2}%), hsl({:.2}deg, {:.2}%, {:.2}%)) !important; \
                     mix-blend-mode: multiply !important; \
                     opacity: {:.3} !important; \
                     isolation: isolate; \
                     }}\n",
                    boost.color.dot_angle_deg.rem_euclid(360.0),
                    visible_hue,
                    hsl_sat_pct,
                    hsl_light_pct,
                    visible_secondary_hue,
                    hsl_sat_pct,
                    hsl_light_pct,
                    opacity
                ));
            }
        }

        if boost.color.magic_theme && boost.color.color_boost_enabled {
            css.push_str("/* Magic Theme */\n:root { color-scheme: light dark !important; }\n");
        }

        let custom_css = if boost.custom_css.len() <= MAX_CUSTOM_CSS_BYTES {
            sanitize_custom_css(&boost.custom_css)
        } else {
            String::new()
        };
        if !custom_css.is_empty() {
            css.push_str("/* USER CSS */\n");
            css.push_str(&custom_css);
            if !custom_css.ends_with('\n') {
                css.push('\n');
            }
        }

        css
    }

    pub fn restore_boost(&mut self, boost: Boost) {
        self.boosts.insert(boost.id.clone(), boost);
    }

    pub fn restore_active_boost(&mut self, domain: String, active_boost_id: BoostId) {
        self.active_boosts.insert(domain, active_boost_id);
    }
}

fn sanitize_imported_boost(mut boost: Boost) -> Option<Boost> {
    if boost.name.len() > MAX_BOOST_NAME_BYTES
        || boost.custom_css.len() > MAX_CUSTOM_CSS_BYTES
        || !valid_zap_selectors(&boost.zap_selectors)
    {
        return None;
    }
    boost.typography.font_family = sanitize_font_family(&boost.typography.font_family)?;
    boost.custom_css = sanitize_custom_css(&boost.custom_css);
    Some(boost)
}

fn is_valid_update_payload(changes: &BoostUpdate) -> bool {
    changes
        .name
        .as_ref()
        .is_none_or(|name| name.len() <= MAX_BOOST_NAME_BYTES)
        && changes
            .typography
            .as_ref()
            .and_then(|typography| typography.font_family.as_ref())
            .is_none_or(|font_family| font_family.len() <= MAX_FONT_FAMILY_BYTES)
        && changes
            .custom_css
            .as_ref()
            .is_none_or(|css| css.len() <= MAX_CUSTOM_CSS_BYTES)
        && changes
            .zap_selectors
            .as_ref()
            .is_none_or(|selectors| valid_zap_selectors(selectors))
}

fn valid_zap_selectors(selectors: &[String]) -> bool {
    selectors.len() <= MAX_ZAP_SELECTORS
        && selectors.iter().all(|selector| {
            selector.len() <= MAX_ZAP_SELECTOR_BYTES && is_safe_zap_selector(selector)
        })
}

fn is_safe_zap_selector(selector: &str) -> bool {
    let s = selector.trim();
    if s.is_empty() {
        return false;
    }

    let compounds: Vec<&str> = s.split('>').collect();
    if compounds.is_empty() || compounds.len() > 12 {
        return false;
    }

    for compound in compounds {
        let compound = compound.trim();
        if compound.is_empty() {
            return false;
        }

        if compound.contains(',')
            || compound.contains('*')
            || compound.contains('+')
            || compound.contains('~')
            || compound.contains(' ')
            || compound.contains('{')
            || compound.contains('}')
            || compound.contains(';')
            || compound.contains('\\')
        {
            return false;
        }

        let mut pos = 0;
        let bytes = compound.as_bytes();

        let mut tag_len = 0;
        if pos < bytes.len() && (bytes[pos] as char).is_ascii_lowercase() {
            pos += 1;
            tag_len += 1;
            while pos < bytes.len()
                && ((bytes[pos] as char).is_ascii_lowercase()
                    || (bytes[pos] as char).is_ascii_digit())
            {
                pos += 1;
                tag_len += 1;
            }
        }

        let mut term_count = 0;

        while pos < bytes.len() {
            let ch = bytes[pos] as char;
            if ch == '#' {
                pos += 1;
                let start = pos;
                while pos < bytes.len() && is_safe_ident_char(bytes[pos]) {
                    pos += 1;
                }
                if pos == start {
                    return false;
                }
                term_count += 1;
            } else if ch == '.' {
                pos += 1;
                let start = pos;
                while pos < bytes.len() && is_safe_ident_char(bytes[pos]) {
                    pos += 1;
                }
                if pos == start {
                    return false;
                }
                term_count += 1;
            } else if ch == '[' {
                pos += 1;
                let attr_start = pos;
                while pos < bytes.len() && is_safe_attr_name_char(bytes[pos]) {
                    pos += 1;
                }
                let attr_name = &compound[attr_start..pos];
                if attr_name.is_empty() {
                    return false;
                }
                if !is_supported_zap_attr(attr_name) {
                    return false;
                }

                if pos >= bytes.len() || bytes[pos] != b'=' {
                    return false;
                }
                pos += 1;

                if pos >= bytes.len() || bytes[pos] != b'"' {
                    return false;
                }
                pos += 1;

                let val_start = pos;
                while pos < bytes.len() && bytes[pos] != b'"' {
                    if !is_safe_attr_value_char(bytes[pos]) {
                        return false;
                    }
                    pos += 1;
                }

                if pos >= bytes.len() || bytes[pos] != b'"' {
                    return false;
                }
                pos += 1;

                let val = &compound[val_start..pos - 1];
                if val.is_empty() {
                    return false;
                }

                if pos >= bytes.len() || bytes[pos] != b']' {
                    return false;
                }
                pos += 1;

                term_count += 1;
            } else if ch == ':' {
                let rest = &compound[pos..];
                if rest.starts_with(":first-child") {
                    pos += ":first-child".len();
                    term_count += 1;
                } else if rest.starts_with(":last-child") {
                    pos += ":last-child".len();
                    term_count += 1;
                } else if rest.starts_with(":nth-child(") {
                    pos += ":nth-child(".len();
                    let digit_start = pos;
                    while pos < bytes.len() && (bytes[pos] as char).is_ascii_digit() {
                        pos += 1;
                    }
                    if pos == digit_start {
                        return false;
                    }
                    if pos >= bytes.len() || bytes[pos] != b')' {
                        return false;
                    }
                    pos += 1;
                    term_count += 1;
                } else {
                    return false;
                }
            } else {
                return false;
            }
        }

        if tag_len == 0 && term_count == 0 {
            return false;
        }
    }

    true
}

fn is_safe_ident_char(b: u8) -> bool {
    let ch = b as char;
    ch.is_ascii_alphanumeric() || ch == '-' || ch == '_'
}

fn is_safe_attr_name_char(b: u8) -> bool {
    let ch = b as char;
    ch.is_ascii_alphanumeric() || ch == '-' || ch == '_'
}

fn is_safe_attr_value_char(b: u8) -> bool {
    let ch = b as char;
    if ch.is_ascii_alphanumeric() {
        return true;
    }
    matches!(
        ch,
        '-' | '_'
            | '.'
            | ':'
            | '/'
            | '?'
            | '='
            | '&'
            | '%'
            | '#'
            | '+'
            | '@'
            | '!'
            | '~'
            | '*'
            | '$'
            | ';'
    )
}

fn is_supported_zap_attr(name: &str) -> bool {
    name.starts_with("data-")
        || name == "aria-label"
        || name == "name"
        || name == "role"
        || name == "href"
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::boost_css_sanitizer::{sanitize_custom_css, sanitize_font_family};

    #[test]
    fn test_create_temp_uses_zen_defaults() {
        let mut mgr = BoostManager::new();
        let boost = mgr.create_temp("example.com".to_string());

        assert_eq!(boost.name, "My Boost");
        assert!((boost.color.brightness - 0.5).abs() < f64::EPSILON);
        assert!((boost.color.saturation - 0.5).abs() < f64::EPSILON);
        assert!((boost.color.contrast - 0.75).abs() < f64::EPSILON);
        assert!((boost.color.dot_pos.x - 0.76).abs() < f32::EPSILON);
        assert!((boost.color.dot_pos.y - 0.66).abs() < f32::EPSILON);
        assert!((boost.color.secondary_dot_pos.x - 0.5).abs() < f32::EPSILON);
        assert!((boost.color.secondary_dot_pos.y - 0.81).abs() < f32::EPSILON);
        assert!((boost.color.secondary_dot_angle_deg_delta - 55.0).abs() < f64::EPSILON);
        assert!((boost.color.dot_distance - 0.0).abs() < f32::EPSILON);
        assert!(boost.color.color_boost_enabled);
        assert!(!boost.color.magic_theme);
        assert!(!boost.color.smart_invert);
        assert_eq!(boost.typography.font_family, "");
        assert_eq!(boost.typography.case_mode, CaseMode::None);
        assert_eq!(boost.typography.size_mode, SizeMode::K100);
        assert!(!boost.change_was_made);

        let active = mgr.get_active("example.com");
        assert!(active.is_some());
        assert_eq!(active.unwrap().id, boost.id);
    }

    #[test]
    fn test_update_marks_change_was_made() {
        let mut mgr = BoostManager::new();
        let boost = mgr.create_temp("example.com".to_string());
        assert!(!boost.change_was_made);

        let no_change_update = BoostUpdate {
            name: Some("My Boost".to_string()),
            ..Default::default()
        };
        let result = mgr.update(&boost.id, no_change_update).unwrap();
        assert!(!result.change_was_made);

        let real_update = BoostUpdate {
            name: Some("Changed Name".to_string()),
            ..Default::default()
        };
        let result = mgr.update(&boost.id, real_update).unwrap();
        assert!(result.change_was_made);
    }

    #[test]
    fn test_discard_reactivates_previous() {
        let mut mgr = BoostManager::new();
        let boost_a = mgr.create_temp("example.com".to_string());
        mgr.commit_boost(&boost_a.id);

        let boost_b = mgr.create_temp("example.com".to_string());
        assert_eq!(mgr.get_active("example.com").unwrap().id, boost_b.id);

        let prev = mgr.discard_boost(&boost_b.id);
        assert_eq!(prev, Some(Some(boost_a.id.clone())));
        assert_eq!(mgr.get_active("example.com").unwrap().id, boost_a.id);
        assert!(mgr.get_boost(&boost_b.id).is_none());
    }

    #[test]
    fn test_discard_does_not_delete_if_change_was_made() {
        let mut mgr = BoostManager::new();
        let boost = mgr.create_temp("example.com".to_string());
        let update = BoostUpdate {
            name: Some("Changed".to_string()),
            ..Default::default()
        };
        mgr.update(&boost.id, update);

        let result = mgr.discard_boost(&boost.id);
        assert!(result.is_none());
        assert!(mgr.get_boost(&boost.id).is_some());
    }

    #[test]
    fn test_shuffle_matches_zen_behavior() {
        let mut mgr = BoostManager::new();
        let boost = mgr.create_temp("example.com".to_string());

        assert!(boost.typography.font_family.is_empty());
        assert!(boost.color.color_boost_enabled);

        let shuffled = mgr.shuffle_boost(&boost.id).unwrap();

        assert!(
            !shuffled.typography.font_family.is_empty(),
            "shuffle must set a font_family from COMMON_FONTS"
        );
        assert!(
            shuffled.color.color_boost_enabled,
            "shuffle must auto-enable color boost (Zen setDotPos side effect)"
        );
        assert!(
            !shuffled.color.magic_theme,
            "shuffle must force magic_theme/autoTheme = false (Zen line 1490)"
        );
        assert!(
            (0.0..1.0).contains(&shuffled.color.brightness),
            "brightness must be a fresh random value in [0, 1)"
        );
        assert!(
            (0.0..1.0).contains(&shuffled.color.contrast),
            "contrast must be a fresh random value in [0, 1)"
        );
        assert!(
            (0.0..1.0).contains(&shuffled.color.saturation),
            "saturation must be a fresh random value in [0, 1)"
        );
        assert!(
            (0.0..360.0).contains(&shuffled.color.secondary_dot_angle_deg_delta),
            "secondary_dot_angle_deg_delta must be in [0, 360)"
        );
        assert!(
            (0.0..360.0).contains(&shuffled.color.dot_angle_deg),
            "dot_angle_deg must be in [0, 360)"
        );
        assert!(shuffled.change_was_made);
    }

    #[test]
    fn test_reset_restores_defaults() {
        let mut mgr = BoostManager::new();
        let boost = mgr.create_temp("example.com".to_string());
        let update = BoostUpdate {
            name: Some("Modified".to_string()),
            color: Some(maho_types::boost::ColorBoostUpdate {
                brightness: Some(0.9),
                color_boost_enabled: Some(false),
                magic_theme: Some(true),
                ..Default::default()
            }),
            typography: Some(maho_types::boost::TypographyBoostUpdate {
                font_family: Some("Papyrus".to_string()),
                case_mode: Some(CaseMode::Upper),
                size_mode: Some(SizeMode::K150),
            }),
            custom_css: Some("body { outline: 7px solid red; }".to_string()),
            ..Default::default()
        };
        mgr.update(&boost.id, update);
        mgr.append_zap_selector(&boost.id, "#sponsored".to_string());

        let reset = mgr.reset_boost(&boost.id).unwrap();
        assert!((reset.color.brightness - 0.5).abs() < f64::EPSILON);
        assert!((reset.color.saturation - 0.5).abs() < f64::EPSILON);
        assert!((reset.color.contrast - 0.75).abs() < f64::EPSILON);
        assert!((reset.color.dot_pos.x - 0.76).abs() < f32::EPSILON);
        assert!(!reset.color.color_boost_enabled);
        assert!(!reset.color.magic_theme);
        assert_eq!(reset.name, "Modified");
        assert!(!reset.change_was_made);
        assert!(
            mgr.compose_css(&reset, None).is_empty(),
            "Reset must remove every page-visible Boost effect"
        );

        let reenabled = mgr
            .update(
                &boost.id,
                BoostUpdate {
                    color: Some(maho_types::boost::ColorBoostUpdate {
                        color_boost_enabled: Some(true),
                        ..Default::default()
                    }),
                    ..Default::default()
                },
            )
            .unwrap();
        assert!((reenabled.color.brightness - 0.5).abs() < f64::EPSILON);
        assert!((reenabled.color.saturation - 0.5).abs() < f64::EPSILON);
        assert!((reenabled.color.contrast - 0.75).abs() < f64::EPSILON);
        assert!(!reenabled.color.magic_theme);
        assert!(!reenabled.color.smart_invert);
        assert!(reenabled.typography.font_family.is_empty());
        assert_eq!(reenabled.typography.case_mode, CaseMode::None);
        assert_eq!(reenabled.typography.size_mode, SizeMode::K100);
        assert!(reenabled.zap_selectors.is_empty());
        assert!(reenabled.custom_css.is_empty());
    }

    #[test]
    fn test_smart_invert_normalizes_page_canvas_background() {
        let mut mgr = BoostManager::new();
        let boost = mgr.create_temp("example.com".to_string());
        let inverted = mgr
            .update(
                &boost.id,
                BoostUpdate {
                    color: Some(maho_types::boost::ColorBoostUpdate {
                        color_boost_enabled: Some(false),
                        smart_invert: Some(true),
                        ..Default::default()
                    }),
                    ..Default::default()
                },
            )
            .unwrap();

        let css = mgr.compose_css(&inverted, None);
        assert!(
            css.contains(":root, body { background-color: #fff !important; }"),
            "Smart invert must normalize the page canvas instead of leaving the body background as a differently inverted band: {css}"
        );
        assert!(
            !css.contains("picture,"),
            "A picture wrapper and its child img must not both receive the compensation filter: {css}"
        );
        assert!(
            !css.contains("background: #fff"),
            "Canvas normalization must preserve page background images: {css}"
        );
    }

    #[test]
    fn test_export_import_roundtrip() {
        let mut mgr = BoostManager::new();
        let boost = mgr.create_temp("example.com".to_string());
        let update = BoostUpdate {
            name: Some("Export Me".to_string()),
            ..Default::default()
        };
        mgr.update(&boost.id, update);

        let json = mgr.export_boost(&boost.id).unwrap();
        let imported = mgr.import_boost("other.com".to_string(), &json).unwrap();

        // id and timestamps are regenerated on import — only semantic fields must match
        assert_ne!(imported.id, boost.id);
        assert_eq!(imported.domain, "other.com");
        assert_eq!(imported.name, "Export Me");
        assert!((imported.color.brightness - 0.5).abs() < f64::EPSILON);
        assert_eq!(imported.color.dot_pos, boost.color.dot_pos);
        assert_eq!(imported.typography.case_mode, boost.typography.case_mode);
        assert_eq!(imported.typography.size_mode, boost.typography.size_mode);
        assert_eq!(imported.custom_css, boost.custom_css);
        assert_eq!(imported.zap_selectors, boost.zap_selectors);
    }

    #[test]
    fn test_export_emits_zen_compat_shape() {
        let mut mgr = BoostManager::new();
        let boost = mgr.create_temp("example.com".to_string());
        let json = mgr.export_boost(&boost.id).unwrap();
        let value: serde_json::Value = serde_json::from_str(&json).unwrap();
        assert!(value.get("boostName").is_some(), "must use Zen 'boostName'");
        assert!(
            value.get("enableColorBoost").is_some(),
            "must use Zen 'enableColorBoost'"
        );
        assert!(
            value.get("textCaseOverride").is_some(),
            "must use Zen 'textCaseOverride'"
        );
        assert!(
            value.get("sizeOverride").is_some(),
            "must use Zen 'sizeOverride'"
        );
        assert!(value.get("customCSS").is_some(), "must use Zen 'customCSS'");
        assert!(value.get("name").is_none(), "must NOT use Maho 'name'");
        assert!(
            value.get("domain").is_none(),
            "must NOT include Maho-only 'domain'"
        );
    }

    #[test]
    fn test_import_accepts_zen_compat_with_autoTheme() {
        let mut mgr = BoostManager::new();
        let zen_json = r#"{"boostName":"X","dotPos":{"x":0,"y":0},"dotDistance":0,"dotAngleDeg":0,"secondaryDotPos":{"x":0,"y":0},"secondaryDotAngleDegDelta":0,"autoTheme":true,"enableColorBoost":true,"smartInvert":false,"contrast":0.75,"brightness":0.5,"saturation":0.5,"fontFamily":"","textCaseOverride":"none","sizeOverride":1,"zapSelectors":[],"customCSS":"","changeWasMade":true}"#;
        let boost = mgr
            .import_boost("example.com".to_string(), zen_json)
            .unwrap();
        assert_eq!(boost.name, "X");
        assert!(boost.color.magic_theme);
        assert!(boost.color.color_boost_enabled);
    }

    #[test]
    fn css_sanitizer_rejects_invalid_font_family_values() {
        // Given: font values that could escape the generated declaration.
        let invalid_fonts = [
            "Georgia } body { color: red",
            "Georgia (serif)",
            "Georgia; color: red",
            "@font-face",
            "Georgia\nSerif",
            "Georgia\u{2028}Serif",
            "Georgia\u{2029}Serif",
            "'unbalanced",
        ];

        // When / Then: the font boundary rejects every unsafe value.
        for font in invalid_fonts {
            assert_eq!(sanitize_font_family(font), None, "must reject {font:?}");
        }
        assert_eq!(sanitize_font_family(&"a".repeat(257)), None);
    }

    #[test]
    fn css_sanitizer_preserves_valid_font_family() {
        // Given: a conventional multi-family declaration.
        let font = "Georgia, 'Times New Roman', serif";

        // When: it crosses the font boundary.
        let sanitized = sanitize_font_family(font);

        // Then: its valid CSS syntax is retained unchanged.
        assert_eq!(sanitized.as_deref(), Some(font));
    }

    #[test]
    fn css_sanitizer_preserves_font_underscore_and_256_byte_boundary() {
        let exact_boundary = "a".repeat(256);

        assert_eq!(
            sanitize_font_family("Open_Sans").as_deref(),
            Some("Open_Sans")
        );
        assert_eq!(sanitize_font_family(&exact_boundary), Some(exact_boundary));
        assert_eq!(sanitize_font_family(&"a".repeat(257)), None);
    }

    #[test]
    fn css_sanitizer_drops_prohibited_active_constructs() {
        // Given: executable or stylesheet-loading constructs, in mixed case.
        let prohibited = [
            "@ImPoRt url(https://example.test/payload.css);",
            "@CHARSET \"utf-8\";",
            "a { width: ExPrEsSiOn(alert(1)); }",
            "a { BeHaViOr: url(payload.htc); }",
            "a { -MoZ-BiNdInG: url(payload.xml#x); }",
            "a { background: url(jAvAsCrIpT:alert(1)); }",
            "a { background: url('DATA:TEXT/HTML,<script>alert(1)</script>'); }",
        ];

        // When / Then: active prohibited syntax drops the complete stylesheet.
        for stylesheet in prohibited {
            assert_eq!(
                sanitize_custom_css(stylesheet),
                "",
                "must drop {stylesheet:?}"
            );
        }
    }

    #[test]
    fn css_sanitizer_drops_escape_decoded_active_constructs() {
        let prohibited = [
            r"@\69mport url(https://example.test/payload.css);",
            r"a { background: u\72l(javascript:alert(1)); }",
            "a { behavior/**/: url(payload.htc); }",
        ];

        for stylesheet in prohibited {
            assert_eq!(
                sanitize_custom_css(stylesheet),
                "",
                "must drop {stylesheet:?}"
            );
        }
    }

    #[test]
    fn css_sanitizer_drops_unbalanced_braces() {
        // Given: a malformed stylesheet.
        let stylesheet = "@media screen { body { color: red; }";

        // When / Then: it cannot reach the injection boundary.
        assert_eq!(sanitize_custom_css(stylesheet), "");
    }

    #[test]
    fn css_sanitizer_preserves_safe_css_syntax() {
        // Given: normal rules, media queries, custom properties, quoted braces, and comments.
        let stylesheet = concat!(
            "/* @import in a comment is inert */\n",
            ":root { --label: \"{safe}\"; }\n",
            "@media (min-width: 40rem) { body { color: red; } }\n",
            ".icon { background-image: url('data:image/png;base64,AAAA'); }"
        );

        // When: it crosses the CSS boundary.
        let sanitized = sanitize_custom_css(stylesheet);

        // Then: ordinary CodeMirror-authored CSS remains byte-for-byte intact.
        assert_eq!(sanitized, stylesheet);
    }

    #[test]
    fn css_sanitizer_rejects_network_backed_urls() {
        let prohibited = [
            ".icon { background-image: url(https://example.test/icon.png); }",
            ".icon { background-image: url('//example.test/icon.png'); }",
            ".icon { background-image: url('/tracking-pixel'); }",
            ".icon { background-image: url(relative.png); }",
            r".icon { background-image: url(https\3A//example.test/icon.png); }",
            ".icon { background-image: url(\"data:image/svg+xml,%3Csvg/%3E\"); }",
        ];

        for stylesheet in prohibited {
            assert_eq!(
                sanitize_custom_css(stylesheet),
                "",
                "must drop {stylesheet:?}"
            );
        }

        let safe = [
            ".mask { mask-image: url(#local-mask); }",
            ".icon { background-image: url('data:image/png;base64,AAAA'); }",
        ];
        for stylesheet in safe {
            assert_eq!(sanitize_custom_css(stylesheet), stylesheet);
        }
    }

    #[test]
    fn css_sanitizer_rejects_image_set_url_sources() {
        let prohibited = [
            r#"body { background-image: image-set("https://attacker.example/pixel" 1x); }"#,
            r#"body { background-image: -webkit-image-set("//attacker.example/pixel" 1x); }"#,
            r#"body { background-image: image-set("relative.png" 1x); }"#,
            r#"body { background-image: image-set("/pixel" 1x); }"#,
        ];
        for stylesheet in prohibited {
            assert_eq!(sanitize_custom_css(stylesheet), "");
        }
    }

    #[test]
    fn update_rejects_oversized_boost_payloads() {
        const MAX_CSS: usize = 256 * 1024;
        const MAX_NAME: usize = 256;
        const MAX_SELECTORS: usize = 256;
        let mut mgr = BoostManager::new();
        let boost = mgr.create_temp("example.com".to_string());

        let oversized_css = BoostUpdate {
            custom_css: Some("a".repeat(MAX_CSS + 1)),
            ..Default::default()
        };
        assert!(mgr.update(&boost.id, oversized_css).is_none());

        let oversized_name = BoostUpdate {
            name: Some("n".repeat(MAX_NAME + 1)),
            ..Default::default()
        };
        assert!(mgr.update(&boost.id, oversized_name).is_none());

        let oversized_font_family = BoostUpdate {
            typography: Some(maho_types::boost::TypographyBoostUpdate {
                font_family: Some("f".repeat(MAX_NAME + 1)),
                ..Default::default()
            }),
            ..Default::default()
        };
        assert!(mgr.update(&boost.id, oversized_font_family).is_none());

        let too_many_selectors = BoostUpdate {
            zap_selectors: Some(
                (0..=MAX_SELECTORS)
                    .map(|index| format!("#item-{index}"))
                    .collect(),
            ),
            ..Default::default()
        };
        assert!(mgr.update(&boost.id, too_many_selectors).is_none());
    }

    #[test]
    fn append_and_import_reject_oversized_selector_payloads() {
        const MAX_CSS: usize = 256 * 1024;
        const MAX_SELECTOR: usize = 2 * 1024;
        const MAX_SELECTORS: usize = 256;
        let mut mgr = BoostManager::new();
        let boost = mgr.create_temp("example.com".to_string());
        assert!(mgr
            .append_zap_selector(&boost.id, "x".repeat(MAX_SELECTOR + 1))
            .is_none());

        for index in 0..MAX_SELECTORS {
            assert!(mgr
                .append_zap_selector(&boost.id, format!("#item-{index}"))
                .is_some());
        }
        assert!(mgr
            .append_zap_selector(&boost.id, "#one-too-many".to_string())
            .is_none());

        let oversized_import = format!(
            r#"{{"boostName":"X","dotPos":{{"x":0,"y":0}},"dotDistance":0,"dotAngleDeg":0,"secondaryDotPos":{{"x":0,"y":0}},"secondaryDotAngleDegDelta":0,"magicTheme":false,"enableColorBoost":false,"smartInvert":false,"contrast":0.75,"brightness":0.5,"saturation":0.5,"fontFamily":"","textCaseOverride":"none","sizeOverride":1,"zapSelectors":[],"customCSS":"{}","changeWasMade":true}}"#,
            "a".repeat(MAX_CSS + 1)
        );
        assert!(mgr
            .import_boost("example.com".to_string(), &oversized_import)
            .is_none());
    }

    #[test]
    fn css_sanitizer_ignores_prohibited_text_inside_strings() {
        // Given: syntax-looking text inside a CSS string literal.
        let stylesheet = r#".notice::before { content: "@import url(javascript:alert(1))"; }"#;

        // When / Then: quoted content is not interpreted as active CSS syntax.
        assert_eq!(sanitize_custom_css(stylesheet), stylesheet);
    }

    #[test]
    fn css_sanitizer_rejects_nul_and_unescaped_string_controls() {
        let prohibited = [
            "a { content: \"safe\0unsafe\"; }",
            "a { content: \"line\nbreak\"; }",
            "a { content: \"line\rbreak\"; }",
            "a { content: \"line\u{000C}break\"; }",
        ];

        for stylesheet in prohibited {
            assert_eq!(
                sanitize_custom_css(stylesheet),
                "",
                "must drop {stylesheet:?}"
            );
        }
        assert_eq!(
            sanitize_custom_css("a { content: \"line\\\r\nbreak\"; }"),
            "a { content: \"line\\\r\nbreak\"; }"
        );
    }

    #[test]
    fn css_sanitizer_rejects_escaped_network_urls_and_malformed_urls() {
        let escaped_network = r".icon { background: url(https\3A//example.test/icon.png); }";
        let malformed = [
            r".icon { background: url(https://example.test icon.png); }",
            r#".icon { background: url("https://example.test" extra); }"#,
        ];

        assert_eq!(sanitize_custom_css(escaped_network), "");
        for stylesheet in malformed {
            assert_eq!(
                sanitize_custom_css(stylesheet),
                "",
                "must drop {stylesheet:?}"
            );
        }
    }

    #[test]
    fn css_sanitizer_rejects_url_controls_and_unquoted_parentheses() {
        let prohibited = [
            r"a { background: url(java\9 script:alert(1)); }",
            r"a { background: url(da\A ta:text/html,payload); }",
            "a { background: url(https://example.test/(payload)); }",
        ];

        for stylesheet in prohibited {
            assert_eq!(
                sanitize_custom_css(stylesheet),
                "",
                "must drop {stylesheet:?}"
            );
        }
    }

    #[test]
    fn css_sanitizer_rejects_raw_c0_and_del_in_quoted_and_unquoted_urls() {
        let prohibited = [
            "a { background: url('https://example.test/\u{0001}icon.png'); }",
            "a { background: url(https://example.test/\u{0007}icon.png); }",
            "a { background: url(\"https://example.test/\u{007F}icon.png\"); }",
            "a { background: url(https://example.test/\u{007F}icon.png); }",
        ];

        for stylesheet in prohibited {
            assert_eq!(
                sanitize_custom_css(stylesheet),
                "",
                "must drop {stylesheet:?}"
            );
        }
    }

    #[test]
    fn css_sanitizer_rejects_whitespace_obfuscated_data_html_urls() {
        let prohibited = [
            "a { background: url('data: text/html,<p>payload</p>'); }",
            "a { background: url('data:\ttext/html,<p>payload</p>'); }",
        ];

        for stylesheet in prohibited {
            assert_eq!(
                sanitize_custom_css(stylesheet),
                "",
                "must drop {stylesheet:?}"
            );
        }
    }

    #[test]
    fn css_sanitizer_rejects_blocked_declarations_after_nested_blocks() {
        let prohibited = [
            "@media screen { .child { color: red; } behavior: url(payload.htc); }",
            ".parent { .child { color: red; } -moz-binding: url(payload.xml#x); }",
        ];

        for stylesheet in prohibited {
            assert_eq!(
                sanitize_custom_css(stylesheet),
                "",
                "must drop {stylesheet:?}"
            );
        }
    }

    #[test]
    fn css_sanitizer_preserves_valid_nested_selector_and_at_rule_contexts() {
        let stylesheet = concat!(
            ".parent { .behavior:hover { color: red; } --behavior: smooth; }\n",
            "@supports (behavior: smooth) { .card { color: green; } }"
        );

        assert_eq!(sanitize_custom_css(stylesheet), stylesheet);
    }

    #[test]
    fn css_sanitizer_preserves_local_urls_in_nested_rules() {
        let stylesheet = concat!(
            "@media screen { .icon { mask-image: url(#local-mask); } }",
            "\n",
            ".gallery { .image { background: url('data:image/png;base64,AAAA'); } }"
        );

        assert_eq!(sanitize_custom_css(stylesheet), stylesheet);
    }

    #[test]
    fn css_sanitizer_rejects_spaced_relative_url_tokens() {
        let stylesheet = ".behavior:hover, #behavior:focus { --token: url /**/ (safe); }";

        assert_eq!(sanitize_custom_css(stylesheet), "");
    }

    #[test]
    fn css_sanitizer_preserves_custom_property_prefixes() {
        let stylesheet = ":root { --behavior: smooth; }";

        assert_eq!(sanitize_custom_css(stylesheet), stylesheet);
    }

    #[test]
    fn css_sanitizer_neutralizes_malicious_already_stored_boost() {
        // Given: a legacy record that bypassed import-time validation.
        let mut mgr = BoostManager::new();
        let boost = mgr.create_temp("example.com".to_string());
        let mut stored = boost.clone();
        stored.typography.font_family = "Georgia; color: red".to_string();
        stored.custom_css = "@import url(https://example.test/payload.css);".to_string();

        // When: it is composed directly for page injection.
        let css = mgr.compose_css(&stored, None);

        // Then: neither legacy payload reaches the target page.
        assert!(!css.contains("Georgia; color: red"));
        assert!(!css.contains("@import"));
    }

    #[test]
    fn css_sanitizer_composition_neutralizes_escaped_import() {
        let mut mgr = BoostManager::new();
        let boost = mgr.create_temp("example.com".to_string());
        let mut stored = boost.clone();
        stored.custom_css = r"@\69mport url(https://example.test/payload.css);".to_string();

        let css = mgr.compose_css(&stored, None);

        assert!(!css.contains("@\\69mport"));
    }

    #[test]
    fn css_sanitizer_composition_neutralizes_raw_control_url() {
        let mut mgr = BoostManager::new();
        let boost = mgr.create_temp("example.com".to_string());
        let mut stored = boost.clone();
        stored.custom_css =
            "a { background: url('https://example.test/\u{0001}icon.png'); }".to_string();

        let css = mgr.compose_css(&stored, None);

        assert!(!css.contains("https://example.test/"));
    }

    #[test]
    fn css_sanitizer_import_rejects_malicious_font_family() {
        // Given: a Zen import with a declaration-breaking font family.
        let mut mgr = BoostManager::new();
        let json = r#"{"boostName":"X","dotPos":{"x":0,"y":0},"dotDistance":0,"dotAngleDeg":0,"secondaryDotPos":{"x":0,"y":0},"secondaryDotAngleDegDelta":0,"magicTheme":false,"enableColorBoost":false,"smartInvert":false,"contrast":0.75,"brightness":0.5,"saturation":0.5,"fontFamily":"Georgia; color: red","textCaseOverride":"none","sizeOverride":1,"zapSelectors":[],"customCSS":"body { color: red; }","changeWasMade":true}"#;

        // When: import validates the raw DTO before persistence.
        let imported = mgr.import_boost("example.com".to_string(), json);

        // Then: no Boost is created.
        assert!(imported.is_none());
    }

    #[test]
    fn css_sanitizer_import_stores_sanitized_custom_css() {
        // Given: an otherwise valid Zen import with prohibited custom CSS.
        let mut mgr = BoostManager::new();
        let json = r#"{"boostName":"X","dotPos":{"x":0,"y":0},"dotDistance":0,"dotAngleDeg":0,"secondaryDotPos":{"x":0,"y":0},"secondaryDotAngleDegDelta":0,"magicTheme":false,"enableColorBoost":false,"smartInvert":false,"contrast":0.75,"brightness":0.5,"saturation":0.5,"fontFamily":"Georgia","textCaseOverride":"none","sizeOverride":1,"zapSelectors":[],"customCSS":"@import url(https://example.test/payload.css);","changeWasMade":true}"#;

        // When: the DTO converts successfully and crosses the storage boundary.
        let imported = mgr.import_boost("example.com".to_string(), json);

        // Then: the Boost is retained but the hostile stylesheet is not stored.
        assert_eq!(imported.map(|boost| boost.custom_css), Some(String::new()));
    }

    #[test]
    fn css_sanitizer_import_stores_empty_css_for_raw_control_url() {
        let mut mgr = BoostManager::new();
        let json = r#"{"boostName":"X","dotPos":{"x":0,"y":0},"dotDistance":0,"dotAngleDeg":0,"secondaryDotPos":{"x":0,"y":0},"secondaryDotAngleDegDelta":0,"magicTheme":false,"enableColorBoost":false,"smartInvert":false,"contrast":0.75,"brightness":0.5,"saturation":0.5,"fontFamily":"Georgia","textCaseOverride":"none","sizeOverride":1,"zapSelectors":[],"customCSS":"a { background: url('https://example.test/\u0001icon.png'); }","changeWasMade":true}"#;

        let imported = mgr.import_boost("example.com".to_string(), json);

        assert_eq!(imported.map(|boost| boost.custom_css), Some(String::new()));
    }

    #[test]
    fn test_append_remove_zap_selector_idempotent() {
        let mut mgr = BoostManager::new();
        let boost = mgr.create_temp("example.com".to_string());

        let result = mgr
            .append_zap_selector(&boost.id, ".ad-banner".to_string())
            .unwrap();
        assert_eq!(result.zap_selectors.len(), 1);
        assert!(result.change_was_made);

        let result2 = mgr
            .append_zap_selector(&boost.id, ".ad-banner".to_string())
            .unwrap();
        assert_eq!(result2.zap_selectors.len(), 1);

        let result3 = mgr.remove_zap_selector(&boost.id, ".nonexistent").unwrap();
        assert_eq!(result3.zap_selectors.len(), 1);

        let result4 = mgr.remove_zap_selector(&boost.id, ".ad-banner").unwrap();
        assert_eq!(result4.zap_selectors.len(), 0);
    }

    #[test]
    fn active_boost_rejects_cross_domain_ids() {
        let mut mgr = BoostManager::new();
        let first = mgr.create("a.example".to_string(), "A".to_string());
        let foreign = mgr.create("b.example".to_string(), "B".to_string());
        assert!(mgr.set_active("a.example", Some(first.id.clone())));

        assert!(!mgr.set_active("a.example", Some(foreign.id)));
        assert_eq!(
            mgr.get_active("a.example").map(|boost| boost.id),
            Some(first.id)
        );
    }

    #[test]
    fn test_todo2_defaults_and_imported_preservation() {
        let mut mgr = BoostManager::new();
        let new_boost = mgr.create("example.com".to_string(), "New Boost".to_string());
        assert!(new_boost.color.color_boost_enabled);
        assert!(!new_boost.color.magic_theme);

        let disabled_json = r#"{"boostName":"Imported Disabled","dotPos":{"x":0,"y":0},"dotDistance":0,"dotAngleDeg":0,"secondaryDotPos":{"x":0,"y":0},"secondaryDotAngleDegDelta":0,"magicTheme":false,"enableColorBoost":false,"smartInvert":false,"contrast":0.75,"brightness":0.5,"saturation":0.5,"fontFamily":"","textCaseOverride":"none","sizeOverride":1,"zapSelectors":[],"customCSS":"","changeWasMade":true}"#;
        let imported = mgr
            .import_boost("example.com".to_string(), disabled_json)
            .unwrap();
        assert!(!imported.color.color_boost_enabled);
        assert!(!imported.color.magic_theme);
    }

    #[test]
    fn test_todo2_selector_grammar_validation() {
        let valid_selectors = [
            "#main-header",
            ".card-body",
            "button#submit-btn",
            "div.container-fluid",
            "a[href=\"/about\"]",
            "button[aria-label=\"Close\"]",
            "input[name=\"username\"]",
            "div[role=\"navigation\"]",
            "section[data-test-id=\"card_123\"]",
            "ul.nav > li.item",
            "div#sidebar > nav[role=\"navigation\"]",
        ];

        for sel in valid_selectors {
            assert!(is_safe_zap_selector(sel), "expected valid selector: {sel}");
        }

        let invalid_selectors = [
            "body { color: red; }",
            "div; display: none",
            "a\\b",
            "a/b",
            "a, b",
            "*",
            "div:hover",
            "div + p",
            "div ~ p",
            "div span p",
            "a[onclick=\"alert(1)\"]",
            "a[href='invalid_quotes']",
            "a[href=\"unclosed]",
            "a[href=unquoted]",
            "a[href=\"val\nbreak\"]",
            "",
            "   ",
        ];

        for sel in invalid_selectors {
            assert!(
                !is_safe_zap_selector(sel),
                "expected invalid selector: {sel}"
            );
        }
    }

    #[test]
    fn test_todo2_zap_css_composition_and_unhide_attribute() {
        let mut mgr = BoostManager::new();
        let mut boost = mgr.create_temp("example.com".to_string());
        boost.zap_selectors = vec![
            "#valid-id".to_string(),
            ".valid-class".to_string(),
            "div; color:red".to_string(),
            "a[onclick=\"alert(1)\"]".to_string(),
        ];

        let css = mgr.compose_css(&boost, None);

        assert!(css.contains("/* Zen-Zaps */"));
        assert!(css.contains("#valid-id:not([maho-zap-unhide]){ display: none !important; }"));
        assert!(css.contains(".valid-class:not([maho-zap-unhide]){ display: none !important; }"));
        assert!(!css.contains("zen-zap-unhide"));
        assert!(!css.contains("div; color:red"));
        assert!(!css.contains("onclick"));
    }
    #[test]
    fn test_multiple_boosts_per_domain_are_listed_independently() {
        let mut mgr = BoostManager::new();
        let first = mgr.create("example.com".to_string(), "First".to_string());
        let second = mgr.create("example.com".to_string(), "Second".to_string());
        let third = mgr.create("example.com".to_string(), "Third".to_string());
        let other = mgr.create("other.com".to_string(), "Other".to_string());

        let listed = mgr.list_for_domain("example.com");
        assert_eq!(listed.len(), 3, "a domain must hold multiple boosts");
        let mut listed_ids: Vec<String> = listed.iter().map(|b| b.id.to_string()).collect();
        listed_ids.sort();
        let mut expected_ids = vec![
            first.id.to_string(),
            second.id.to_string(),
            third.id.to_string(),
        ];
        expected_ids.sort();
        assert_eq!(listed_ids, expected_ids);
        assert!(
            !listed_ids.contains(&other.id.to_string()),
            "other domains must not leak into a domain listing"
        );

        assert_eq!(mgr.list_for_domain("other.com").len(), 1);
        assert!(mgr.list_for_domain("absent.com").is_empty());
        assert_eq!(mgr.get_all_boosts().len(), 4, "all domains combined");
    }

    #[test]
    fn test_delete_removes_only_target_and_clears_its_activation() {
        let mut mgr = BoostManager::new();
        let keep = mgr.create("example.com".to_string(), "Keep".to_string());
        let drop = mgr.create("example.com".to_string(), "Drop".to_string());
        mgr.set_active("example.com", Some(drop.id.clone()));
        assert_eq!(
            mgr.get_active("example.com").map(|b| b.id.to_string()),
            Some(drop.id.to_string())
        );

        let deleted = mgr.delete(&drop.id).expect("delete returns the boost");
        assert_eq!(deleted.id, drop.id);
        assert_eq!(deleted.name, "Drop");
        assert!(
            mgr.get_active("example.com").is_none(),
            "deleting the active boost must clear activation"
        );
        assert!(mgr.get_boost(&drop.id).is_none());
        assert!(mgr.get_boost(&keep.id).is_some(), "siblings survive");
        assert_eq!(mgr.list_for_domain("example.com").len(), 1);
        assert!(
            mgr.delete(&drop.id).is_none(),
            "deleting an absent boost reports no boost"
        );
    }

    #[test]
    fn test_delete_inactive_boost_leaves_activation_intact() {
        let mut mgr = BoostManager::new();
        let active = mgr.create("example.com".to_string(), "Active".to_string());
        let inactive = mgr.create("example.com".to_string(), "Inactive".to_string());
        mgr.set_active("example.com", Some(active.id.clone()));

        mgr.delete(&inactive.id).expect("delete returns the boost");
        assert_eq!(
            mgr.get_active("example.com").map(|b| b.id.to_string()),
            Some(active.id.to_string()),
            "deleting a non-active sibling must not disturb activation"
        );
    }

    #[test]
    fn test_set_active_switches_between_boosts_and_clears_with_none() {
        let mut mgr = BoostManager::new();
        let first = mgr.create("example.com".to_string(), "First".to_string());
        let second = mgr.create("example.com".to_string(), "Second".to_string());
        assert!(mgr.get_active("example.com").is_none(), "starts inactive");

        mgr.set_active("example.com", Some(first.id.clone()));
        assert_eq!(
            mgr.get_active("example.com").map(|b| b.name.clone()),
            Some("First".to_string())
        );

        mgr.set_active("example.com", Some(second.id.clone()));
        assert_eq!(
            mgr.get_active("example.com").map(|b| b.name.clone()),
            Some("Second".to_string()),
            "activation must move to the newly selected boost"
        );

        mgr.set_active("example.com", None);
        assert!(
            mgr.get_active("example.com").is_none(),
            "None must clear the domain activation"
        );
        assert_eq!(
            mgr.list_for_domain("example.com").len(),
            2,
            "clearing activation must not delete boosts"
        );
    }

    #[test]
    fn test_set_active_is_scoped_per_domain() {
        let mut mgr = BoostManager::new();
        let here = mgr.create("example.com".to_string(), "Here".to_string());
        let there = mgr.create("other.com".to_string(), "There".to_string());
        mgr.set_active("example.com", Some(here.id.clone()));
        mgr.set_active("other.com", Some(there.id.clone()));

        assert_eq!(
            mgr.get_active("example.com").map(|b| b.name.clone()),
            Some("Here".to_string())
        );
        mgr.set_active("example.com", None);
        assert_eq!(
            mgr.get_active("other.com").map(|b| b.name.clone()),
            Some("There".to_string()),
            "clearing one domain must not affect another"
        );
    }

    #[test]
    fn test_restore_rehydrates_boosts_and_activation() {
        let mut source = BoostManager::new();
        let first = source.create("example.com".to_string(), "First".to_string());
        let second = source.create("example.com".to_string(), "Second".to_string());
        source.set_active("example.com", Some(second.id.clone()));

        let mut restored = BoostManager::new();
        restored.restore_boost(first.clone());
        restored.restore_boost(second.clone());
        restored.restore_active_boost("example.com".to_string(), second.id.clone());

        assert_eq!(restored.list_for_domain("example.com").len(), 2);
        assert_eq!(
            restored.get_active("example.com").map(|b| b.id.to_string()),
            Some(second.id.to_string()),
            "restored activation must point at the persisted boost"
        );
        assert_eq!(
            restored.get_boost(&first.id).map(|b| b.name.clone()),
            Some("First".to_string()),
            "restore preserves stored fields verbatim"
        );
    }

    #[test]
    fn test_restore_boost_replaces_existing_entry_by_id() {
        let mut mgr = BoostManager::new();
        let boost = mgr.create("example.com".to_string(), "Original".to_string());
        let mut updated = boost.clone();
        updated.name = "Rehydrated".to_string();

        mgr.restore_boost(updated);

        assert_eq!(
            mgr.list_for_domain("example.com").len(),
            1,
            "restoring the same id must not duplicate the boost"
        );
        assert_eq!(
            mgr.get_boost(&boost.id).map(|b| b.name.clone()),
            Some("Rehydrated".to_string())
        );
    }
}
