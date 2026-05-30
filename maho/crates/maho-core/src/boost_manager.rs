use std::collections::HashMap;
use maho_types::boost::{Boost, BoostUpdate, ColorBoost, TypographyBoost, CaseMode, SizeMode, Point, ZenCompatBoostData};
use maho_types::identifiers::BoostId;
use maho_types::common::DateTime;
use rand::Rng;

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
        color_boost_enabled: false,
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
            self.previous_active_boosts.insert(domain.clone(), current_active);
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

    pub fn discard_boost(&mut self, id: &BoostId) -> Option<BoostId> {
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
        prev
    }

    pub fn update(&mut self, boost_id: &BoostId, changes: BoostUpdate) -> Option<Boost> {
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

        let secondary_angle_rad = angle_rad
            + boost.color.secondary_dot_angle_deg_delta * std::f64::consts::PI / 180.0;
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
        let (color, typography) = zen_defaults();
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
        // Try Zen-compat shape first
        if let Ok(dto) = serde_json::from_str::<ZenCompatBoostData>(json) {
            if let Ok(boost) = Boost::try_from((dto, domain.clone())) {
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
            self.boosts.insert(boost.id.clone(), boost.clone());
            return Some(boost);
        }
        None
    }

    pub fn append_zap_selector(&mut self, id: &BoostId, selector: String) -> Option<Boost> {
        let boost = self.boosts.get_mut(id)?;
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

    pub fn set_active(&mut self, domain: &str, active_boost_id: Option<BoostId>) {
        if let Some(id) = active_boost_id {
            self.active_boosts.insert(domain.to_string(), id);
        } else {
            self.active_boosts.remove(domain);
        }
    }

    pub fn get_active(&self, domain: &str) -> Option<Boost> {
        let active_id = self.active_boosts.get(domain)?;
        self.get_boost(active_id).cloned()
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
    pub fn compose_css(&self, boost: &Boost) -> String {
        let mut css = String::new();

        let mut zap_block = String::new();
        for selector in &boost.zap_selectors {
            if selector.contains('{')
                || selector.contains('}')
                || selector.contains(';')
                || selector.contains('\\')
                || selector.contains('/')
                || selector.contains('(')
            {
                continue;
            }
            zap_block.push_str(&format!(
                "{}:not([zen-zap-unhide]){{ display: none !important; }}\n",
                selector
            ));
        }
        if !zap_block.is_empty() {
            css.push_str("/* Zen-Zaps */\n");
            css.push_str(&zap_block);
            css.push('\n');
        }

        let font_decl = if !boost.typography.font_family.is_empty() {
            format!("font-family: {} !important;", boost.typography.font_family)
        } else {
            String::new()
        };
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
                css.push_str(":root { background: #fff !important; }\n");
                css.push_str(
                    "img, video, picture, picture > source, canvas, iframe, embed, object, svg image, [style*=\"url(\"] { filter: invert(1) hue-rotate(180deg) !important; }\n",
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
                     background-color: hsl({:.2}deg, {:.2}%, {:.2}%) !important; \
                     mix-blend-mode: multiply !important; \
                     opacity: {:.3} !important; \
                     isolation: isolate; \
                     }}\n",
                    visible_hue, hsl_sat_pct, hsl_light_pct, opacity
                ));
            }
        }

        if boost.color.magic_theme && boost.color.color_boost_enabled {
            css.push_str("/* Magic Theme: workspace integration pending */\n");
        }

        if !boost.custom_css.is_empty() {
            css.push_str("/* USER CSS */\n");
            css.push_str(&boost.custom_css);
            if !boost.custom_css.ends_with('\n') {
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

#[cfg(test)]
mod tests {
    use super::*;

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
        assert!(!boost.color.color_boost_enabled);
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
        assert_eq!(prev, Some(boost_a.id.clone()));
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
        assert!(!boost.color.color_boost_enabled);

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
                ..Default::default()
            }),
            ..Default::default()
        };
        mgr.update(&boost.id, update);

        let reset = mgr.reset_boost(&boost.id).unwrap();
        assert!((reset.color.brightness - 0.5).abs() < f64::EPSILON);
        assert!((reset.color.saturation - 0.5).abs() < f64::EPSILON);
        assert!((reset.color.contrast - 0.75).abs() < f64::EPSILON);
        assert!((reset.color.dot_pos.x - 0.76).abs() < f32::EPSILON);
        assert_eq!(reset.name, "Modified");
        assert!(!reset.change_was_made);
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
        assert!(value.get("enableColorBoost").is_some(), "must use Zen 'enableColorBoost'");
        assert!(value.get("textCaseOverride").is_some(), "must use Zen 'textCaseOverride'");
        assert!(value.get("sizeOverride").is_some(), "must use Zen 'sizeOverride'");
        assert!(value.get("customCSS").is_some(), "must use Zen 'customCSS'");
        assert!(value.get("name").is_none(), "must NOT use Maho 'name'");
        assert!(value.get("domain").is_none(), "must NOT include Maho-only 'domain'");
    }

    #[test]
    fn test_import_accepts_zen_compat_with_autoTheme() {
        let mut mgr = BoostManager::new();
        let zen_json = r#"{"boostName":"X","dotPos":{"x":0,"y":0},"dotDistance":0,"dotAngleDeg":0,"secondaryDotPos":{"x":0,"y":0},"secondaryDotAngleDegDelta":0,"autoTheme":true,"enableColorBoost":true,"smartInvert":false,"contrast":0.75,"brightness":0.5,"saturation":0.5,"fontFamily":"","textCaseOverride":"none","sizeOverride":1,"zapSelectors":[],"customCSS":"","changeWasMade":true}"#;
        let boost = mgr.import_boost("example.com".to_string(), zen_json).unwrap();
        assert_eq!(boost.name, "X");
        assert!(boost.color.magic_theme);
        assert!(boost.color.color_boost_enabled);
    }

    #[test]
    fn test_append_remove_zap_selector_idempotent() {
        let mut mgr = BoostManager::new();
        let boost = mgr.create_temp("example.com".to_string());

        let result = mgr.append_zap_selector(&boost.id, ".ad-banner".to_string()).unwrap();
        assert_eq!(result.zap_selectors.len(), 1);
        assert!(result.change_was_made);

        let result2 = mgr.append_zap_selector(&boost.id, ".ad-banner".to_string()).unwrap();
        assert_eq!(result2.zap_selectors.len(), 1);

        let result3 = mgr.remove_zap_selector(&boost.id, ".nonexistent").unwrap();
        assert_eq!(result3.zap_selectors.len(), 1);

        let result4 = mgr.remove_zap_selector(&boost.id, ".ad-banner").unwrap();
        assert_eq!(result4.zap_selectors.len(), 0);
    }
}
