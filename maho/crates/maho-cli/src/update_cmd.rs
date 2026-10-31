use std::cmp::Ordering;
use std::path::Path;

use anyhow::{Context, Result};

pub const APPCAST_URL: &str =
    "https://github.com/Project-Maho/maho-browser/releases/latest/download/appcast.xml";
const MAC_INFO_PLIST: &str = "/Applications/Maho.app/Contents/Info.plist";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Platform {
    MacOs,
    Windows,
    Linux,
}

impl Platform {
    pub fn current() -> Self {
        if cfg!(target_os = "macos") {
            Platform::MacOs
        } else if cfg!(target_os = "windows") {
            Platform::Windows
        } else {
            Platform::Linux
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct UpdateReport {
    pub installed: Option<String>,
    pub latest: Option<String>,
    pub update_available: bool,
    pub how_to_update: String,
}

pub fn latest_appcast_version(xml: &str) -> Option<String> {
    let mut best: Option<String> = None;
    for attr in ["sparkle:shortVersionString=\"", "sparkle:version=\""] {
        for (start, _) in xml.match_indices(attr) {
            let rest = &xml[start + attr.len()..];
            let Some(end) = rest.find('"') else { continue };
            let candidate = rest[..end].trim();
            if candidate.is_empty() {
                continue;
            }
            if best
                .as_deref()
                .is_none_or(|b| compare_versions(candidate, b) == Ordering::Greater)
            {
                best = Some(candidate.to_string());
            }
        }
        if best.is_some() {
            break;
        }
    }
    best
}

pub fn compare_versions(a: &str, b: &str) -> Ordering {
    let parts = |v: &str| -> Vec<u64> {
        v.split(['.', '-', '+'])
            .map(|p| p.chars().take_while(char::is_ascii_digit).collect::<String>())
            .map(|p| p.parse().unwrap_or(0))
            .collect()
    };
    let (pa, pb) = (parts(a), parts(b));
    for i in 0..pa.len().max(pb.len()) {
        match pa.get(i).unwrap_or(&0).cmp(pb.get(i).unwrap_or(&0)) {
            Ordering::Equal => continue,
            other => return other,
        }
    }
    Ordering::Equal
}

pub fn plist_short_version(plist: &str) -> Option<String> {
    let key = plist.find("<key>CFBundleShortVersionString</key>")?;
    let rest = &plist[key..];
    let start = rest.find("<string>")? + "<string>".len();
    let end = rest[start..].find("</string>")?;
    Some(rest[start..start + end].trim().to_string())
}

pub fn build_report(platform: Platform, installed: Option<&str>, latest: Option<&str>) -> UpdateReport {
    let update_available = match (installed, latest) {
        (Some(i), Some(l)) => compare_versions(l, i) == Ordering::Greater,
        (None, Some(_)) => true,
        _ => false,
    };
    let how_to_update = match platform {
        Platform::Windows => {
            "Maho on Windows updates only through the Microsoft Store: open the Store and choose Update."
        }
        Platform::MacOs if update_available => {
            "Open Maho and choose Maho > Check for Updates…; the built-in updater installs the signed release."
        }
        Platform::MacOs => "Maho is up to date.",
        Platform::Linux => {
            "Install the newest package from https://github.com/Project-Maho/maho-browser/releases/latest."
        }
    }
    .to_string();
    UpdateReport {
        installed: installed.map(str::to_string),
        latest: latest.map(str::to_string),
        update_available,
        how_to_update,
    }
}

fn installed_version(platform: Platform) -> Option<String> {
    match platform {
        Platform::MacOs => std::fs::read_to_string(Path::new(MAC_INFO_PLIST))
            .ok()
            .and_then(|plist| plist_short_version(&plist)),
        Platform::Windows | Platform::Linux => None,
    }
}

pub async fn run(json: bool) -> Result<()> {
    let platform = Platform::current();
    let latest = if platform == Platform::Windows {
        None
    } else {
        let xml = reqwest::Client::new()
            .get(APPCAST_URL)
            .timeout(std::time::Duration::from_secs(20))
            .send()
            .await
            .and_then(reqwest::Response::error_for_status)
            .context("fetch the Maho release feed")?
            .text()
            .await
            .context("read the Maho release feed")?;
        Some(latest_appcast_version(&xml).context("the release feed lists no version")?)
    };
    let installed = installed_version(platform);
    let report = build_report(platform, installed.as_deref(), latest.as_deref());
    if json {
        println!(
            "{}",
            serde_json::json!({
                "installed": report.installed,
                "latest": report.latest,
                "updateAvailable": report.update_available,
                "howToUpdate": report.how_to_update,
            })
        );
    } else {
        println!(
            "installed: {}\nlatest:    {}\n{}",
            report.installed.as_deref().unwrap_or("unknown"),
            report.latest.as_deref().unwrap_or("see the Microsoft Store"),
            report.how_to_update
        );
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    const APPCAST: &str = r#"<rss><channel>
<item><enclosure url="a" sparkle:version="2026.9.26" sparkle:shortVersionString="2026.9.26"/></item>
<item><enclosure url="b" sparkle:version="2026.10.20" sparkle:shortVersionString="2026.10.20"/></item>
</channel></rss>"#;

    #[test]
    fn appcast_newest_version_wins_numerically() {
        assert_eq!(latest_appcast_version(APPCAST).as_deref(), Some("2026.10.20"));
        assert_eq!(latest_appcast_version("<rss/>"), None);
    }

    #[test]
    fn versions_compare_numerically() {
        assert_eq!(compare_versions("2026.10.20", "2026.9.26"), Ordering::Greater);
        assert_eq!(compare_versions("2026.10", "2026.10.0"), Ordering::Equal);
        assert_eq!(compare_versions("1.2.3", "1.10"), Ordering::Less);
    }

    #[test]
    fn plist_version_is_read() {
        let plist = "<dict><key>CFBundleName</key><string>Maho</string>\n<key>CFBundleShortVersionString</key>\n<string>2026.10.7</string></dict>";
        assert_eq!(plist_short_version(plist).as_deref(), Some("2026.10.7"));
    }

    #[test]
    fn report_routes_windows_to_store_and_mac_to_sparkle() {
        let win = build_report(Platform::Windows, None, None);
        assert!(!win.update_available);
        assert!(win.how_to_update.contains("Microsoft Store"));
        let mac = build_report(Platform::MacOs, Some("2026.10.7"), Some("2026.10.20"));
        assert!(mac.update_available);
        assert!(mac.how_to_update.contains("Check for Updates"));
        let current = build_report(Platform::MacOs, Some("2026.10.20"), Some("2026.10.20"));
        assert!(!current.update_available);
    }
}
