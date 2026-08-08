//! Color parsing and conversion helper for `MahoCoreDestination`.

use maho_types::space::SpaceColor;

/// Convert a hex color string (e.g. "#FF5733" or "FF5733") to `SpaceColor`.
///
/// SpaceColor uses HSB (hue 0–360, saturation 0–1, brightness 0–1).
/// Falls back to a default blue if parsing fails.
pub(super) fn hex_to_space_color(hex: &str) -> SpaceColor {
    let hex = hex.trim_start_matches('#');
    if hex.len() < 6 {
        return default_space_color();
    }

    let r = u8::from_str_radix(&hex[0..2], 16).unwrap_or(100);
    let g = u8::from_str_radix(&hex[2..4], 16).unwrap_or(149);
    let b = u8::from_str_radix(&hex[4..6], 16).unwrap_or(237);

    let (h, s, v) = rgb_to_hsb(r, g, b);
    SpaceColor {
        hue: h,
        saturation: s,
        brightness: v,
        grain: 0.0,
    }
}

fn default_space_color() -> SpaceColor {
    SpaceColor {
        hue: 220.0,
        saturation: 0.7,
        brightness: 0.9,
        grain: 0.0,
    }
}

/// Convert RGB (0–255 each) to HSB (hue 0–360, saturation 0–1, brightness 0–1).
fn rgb_to_hsb(r: u8, g: u8, b: u8) -> (f64, f64, f64) {
    let rf = r as f64 / 255.0;
    let gf = g as f64 / 255.0;
    let bf = b as f64 / 255.0;

    let max = rf.max(gf).max(bf);
    let min = rf.min(gf).min(bf);
    let delta = max - min;

    let brightness = max;
    let saturation = if max == 0.0 { 0.0 } else { delta / max };

    let hue = if delta == 0.0 {
        0.0
    } else if max == rf {
        60.0 * (((gf - bf) / delta) % 6.0)
    } else if max == gf {
        60.0 * (((bf - rf) / delta) + 2.0)
    } else {
        60.0 * (((rf - gf) / delta) + 4.0)
    };

    let hue = if hue < 0.0 { hue + 360.0 } else { hue };

    (hue, saturation, brightness)
}
