//! Pure logic for the Linux grounding helper backends: tool selection within a
//! probe chain, per-tool capture argument construction, tesseract TSV parsing,
//! and AT-SPI JSON parsing. Everything here is platform-independent and unit
//! tested on every host; the `#[cfg(target_os = "linux")]` dispatch layers in
//! `capture`/`ocr`/`ax_tree` only glue these helpers to `SystemRunner`.

use crate::error::PlatformError;
use crate::grounding::ScreenElement;
use serde::Deserialize;

/// First existing tool from `chain`, judged by `available`.
pub fn select_tool<'a>(chain: &[&'a str], available: &dyn Fn(&str) -> bool) -> Option<&'a str> {
    chain
        .iter()
        .copied()
        .find(|t| !t.starts_with("builtin:") && available(t))
}

/// Capture command for `tool` writing to `path`.
pub fn capture_args(tool: &str, path: &std::path::Path) -> Vec<String> {
    let path = path.to_string_lossy().into_owned();
    match tool {
        "grim" => vec![path],
        "gnome-screenshot" => vec!["-f".into(), path],
        "spectacle" => vec!["-b".into(), "-n".into(), "-o".into(), path],
        "scrot" => vec![path],
        "import" => vec!["-window".into(), "root".into(), path],
        _ => vec![path],
    }
}

#[cfg(any(target_os = "linux", test))]
pub(crate) fn atspi_args(pid: i32) -> Vec<String> {
    vec!["-".into(), pid.to_string()]
}

/// One recognized word from tesseract TSV output.
#[derive(Debug, Clone, PartialEq)]
pub struct TsvWord {
    pub text: String,
    pub x: i32,
    pub y: i32,
    pub w: u32,
    pub h: u32,
    pub confidence: f32,
}

/// Parse `tesseract <img> stdout tsv` output (one word per level-5 row).
pub fn parse_tesseract_tsv(tsv: &str) -> Vec<TsvWord> {
    tsv.lines()
        .skip(1) // header
        .filter_map(|line| {
            let cols: Vec<&str> = line.splitn(12, '\t').collect();
            if cols.len() < 12 {
                return None;
            }
            if cols[0] != "5" {
                return None;
            }
            let text = cols[11].trim().to_string();
            if text.is_empty() {
                return None;
            }
            let conf: f32 = cols[10].parse().ok()?;
            if conf < 0.0 {
                return None;
            }
            Some(TsvWord {
                text,
                x: cols[6].parse().ok()?,
                y: cols[7].parse().ok()?,
                w: cols[8].parse().ok()?,
                h: cols[9].parse().ok()?,
                confidence: conf,
            })
        })
        .collect()
}

/// One element emitted by the embedded AT-SPI inspection script.
#[derive(Debug, Deserialize)]
struct AtspiElement {
    role: String,
    #[serde(default)]
    label: Option<String>,
    x: i32,
    y: i32,
    w: u32,
    h: u32,
    pid: i32,
}

/// Parse the AT-SPI helper script's JSON output into `ScreenElement`s.
pub fn parse_atspi_json(json: &str) -> Result<Vec<ScreenElement>, PlatformError> {
    let rows: Vec<AtspiElement> = serde_json::from_str(json)
        .map_err(|e| PlatformError::Io(format!("atspi script returned invalid JSON: {e}")))?;
    Ok(rows
        .into_iter()
        .map(|e| ScreenElement {
            role: e.role,
            label: e.label.filter(|l| !l.is_empty()),
            x: e.x,
            y: e.y,
            w: e.w,
            h: e.h,
            pid: e.pid,
        })
        .collect())
}

/// Keep only blocks whose centroid falls inside `region` (mirrors the macOS
/// OCR region rule so both platforms attribute edge-spanning text uniquely).
pub fn filter_region(
    blocks: Vec<crate::grounding::TextBlock>,
    region: Option<crate::grounding::Region>,
) -> Vec<crate::grounding::TextBlock> {
    match region {
        None => blocks,
        Some(r) => {
            let rx_end = i64::from(r.x) + i64::from(r.w);
            let ry_end = i64::from(r.y) + i64::from(r.h);
            blocks
                .into_iter()
                .filter(|b| {
                    let cx = i64::from(b.x) + i64::from(b.w) / 2;
                    let cy = i64::from(b.y) + i64::from(b.h) / 2;
                    cx >= i64::from(r.x) && cx < rx_end && cy >= i64::from(r.y) && cy < ry_end
                })
                .collect()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::grounding::TextBlock;

    #[test]
    fn atspi_python_executes_stdin_and_receives_pid() {
        use crate::probe::{CommandRunner, SystemRunner};
        let args = atspi_args(42);
        let refs: Vec<&str> = args.iter().map(String::as_str).collect();
        let program = SystemRunner
            .which("python3")
            .expect("AT-SPI requires Python 3");
        let out = SystemRunner
            .run_with_stdin(
                &program.to_string_lossy(),
                &refs,
                "import sys\nprint(int(sys.argv[1]))\n",
            )
            .unwrap();
        assert_eq!(out.status, 0, "{}", out.stderr);
        assert_eq!(out.stdout.trim(), "42");
    }

    #[test]
    fn select_tool_skips_builtin_and_missing_entries() {
        let chain = ["grim", "gnome-screenshot", "spectacle"];
        let have_grim = |t: &str| t == "grim";
        assert_eq!(select_tool(&chain, &have_grim), Some("grim"));
        let have_spectacle = |t: &str| t == "spectacle";
        assert_eq!(select_tool(&chain, &have_spectacle), Some("spectacle"));
        let have_none = |_: &str| false;
        assert_eq!(select_tool(&chain, &have_none), None);
        let builtin_only = ["builtin:gdi"];
        assert_eq!(select_tool(&builtin_only, &|_| true), None);
    }

    #[test]
    fn capture_args_match_each_tool_contract() {
        let p = std::path::Path::new("/tmp/shot.png");
        assert_eq!(capture_args("grim", p), vec!["/tmp/shot.png"]);
        assert_eq!(
            capture_args("gnome-screenshot", p),
            vec!["-f", "/tmp/shot.png"]
        );
        assert_eq!(
            capture_args("spectacle", p),
            vec!["-b", "-n", "-o", "/tmp/shot.png"]
        );
        assert_eq!(capture_args("scrot", p), vec!["/tmp/shot.png"]);
        assert_eq!(
            capture_args("import", p),
            vec!["-window", "root", "/tmp/shot.png"]
        );
    }

    const SAMPLE_TSV: &str = "level\tpage\tblock\tpar\tline\tword\tleft\ttop\twidth\theight\tconf\ttext\n5\t1\t1\t1\t1\t1\t10\t20\t100\t30\t96.5\tMaho\n5\t1\t1\t1\t1\t2\t200\t20\t50\t30\t-1\tskipped\n5\t1\t1\t1\t1\t3\t300\t20\t40\t30\t88\tbrowser\n2\t1\t1\t1\t1\t4\t0\t0\t9\t9\t90\tpage\n";

    #[test]
    fn tsv_parser_keeps_only_confident_words() {
        let words = parse_tesseract_tsv(SAMPLE_TSV);
        assert_eq!(words.len(), 2);
        assert_eq!(words[0].text, "Maho");
        assert_eq!(
            (words[0].x, words[0].y, words[0].w, words[0].h),
            (10, 20, 100, 30)
        );
        assert!((words[0].confidence - 96.5).abs() < 0.01);
        assert_eq!(words[1].text, "browser");
    }

    #[test]
    fn tsv_parser_tolerates_missing_columns_and_short_conf() {
        assert!(parse_tesseract_tsv("not enough\tcols\n\n").is_empty());
        assert!(parse_tesseract_tsv("").is_empty());
    }

    #[test]
    fn atspi_parser_maps_json_to_elements() {
        let json = r#"[{"role":"frame","label":"Maho","x":0,"y":10,"w":800,"h":600,"pid":42},
                        {"role":"entry","label":"","x":5,"y":5,"w":1,"h":1,"pid":42}]"#;
        let els = parse_atspi_json(json).expect("valid json");
        assert_eq!(els.len(), 2);
        assert_eq!(els[0].role, "frame");
        assert_eq!(els[0].label.as_deref(), Some("Maho"));
        assert_eq!(els[1].label, None);
        assert_eq!(els[0].pid, 42);
    }

    #[test]
    fn atspi_parser_rejects_invalid_json_as_io_error() {
        let err = parse_atspi_json("not json").unwrap_err();
        assert_eq!(err.code(), "platform_io");
    }

    #[test]
    fn region_filter_accepts_full_coordinate_and_size_ranges() {
        use crate::grounding::Region;
        let block = TextBlock {
            text: "edge".into(),
            x: i32::MAX,
            y: i32::MAX,
            w: 2,
            h: 2,
            confidence: 1.0,
        };
        let region = Region {
            x: i32::MAX,
            y: i32::MAX,
            w: 3,
            h: 3,
        };
        assert_eq!(
            filter_region(vec![block.clone()], Some(region)),
            vec![block]
        );
        let block = TextBlock {
            text: "wide".into(),
            x: 1,
            y: 1,
            w: u32::MAX,
            h: u32::MAX,
            confidence: 1.0,
        };
        let region = Region {
            x: 0,
            y: 0,
            w: u32::MAX,
            h: u32::MAX,
        };
        assert_eq!(
            filter_region(vec![block.clone()], Some(region)),
            vec![block]
        );
    }

    #[test]
    fn region_filter_uses_centroid_rule() {
        let mk = |x: i32, y: i32| TextBlock {
            text: String::new(),
            x,
            y,
            w: 10,
            h: 10,
            confidence: 1.0,
        };
        let blocks = vec![mk(0, 0), mk(95, 0), mk(200, 0)];
        let region = crate::grounding::Region {
            x: 50,
            y: 0,
            w: 100,
            h: 50,
        };
        let kept = filter_region(blocks, Some(region));
        assert_eq!(kept.len(), 1);
        assert_eq!(kept[0].x, 95);
        assert_eq!(filter_region(vec![mk(0, 0)], None).len(), 1);
    }
}
