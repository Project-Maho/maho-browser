use serde::Deserialize;

#[derive(Deserialize)]
pub struct CreditBalanceResponse {
    #[allow(dead_code)]
    pub user_id: i32,
    pub balance_usd: f64,
    pub last_purchase_at: Option<i64>,
    pub last_consumption_at: Option<i64>,
}

pub async fn fetch_balance(
    relay_url: &str,
    access_token: &str,
) -> anyhow::Result<CreditBalanceResponse> {
    let client = reqwest::Client::builder()
        .timeout(std::time::Duration::from_secs(10))
        .build()?;
    let resp = client
        .get(format!("{relay_url}/billing/credit_balance"))
        .header("Authorization", format!("Bearer {access_token}"))
        .send()
        .await?;
    if !resp.status().is_success() {
        anyhow::bail!("balance fetch failed: HTTP {}", resp.status());
    }
    Ok(resp.json().await?)
}

pub fn checkout_url_for_pack(amount: &str) -> &'static str {
    match amount {
        "10" => "https://maho.lemonsqueezy.com/buy/credit-pack-10",
        "50" => "https://maho.lemonsqueezy.com/buy/credit-pack-50",
        "100" => "https://maho.lemonsqueezy.com/buy/credit-pack-100",
        _ => "https://maho.lemonsqueezy.com/buy",
    }
}

/// Outcome of a checkout URL launch attempt.
///
/// Callers use this to print a matching status line and to distinguish the
/// test-skip path from a real browser launch.
#[derive(Debug, PartialEq, Eq)]
pub enum OpenOutcome {
    /// `MAHO_CLI_TEST` was set — no browser was launched.
    SkippedForTest,
    /// Maho Browser was detected on this machine and launched.
    OpenedInMahoBrowser,
    /// Maho Browser was not found; the OS default handler was used instead.
    OpenedInSystemBrowser,
}

/// Open a checkout URL, preferring Maho Browser when installed and falling
/// back to the OS default handler otherwise.
///
/// Returns `Ok(SkippedForTest)` when `MAHO_CLI_TEST` is set (any value).
/// Returns `Err` only when Maho Browser is absent AND the system-default
/// launch also fails.
pub fn open_checkout_url(url: &str) -> anyhow::Result<OpenOutcome> {
    if std::env::var_os("MAHO_CLI_TEST").is_some() {
        return Ok(OpenOutcome::SkippedForTest);
    }

    if try_open_in_maho_browser(url) {
        return Ok(OpenOutcome::OpenedInMahoBrowser);
    }

    open::that(url)
        .map(|_| OpenOutcome::OpenedInSystemBrowser)
        .map_err(anyhow::Error::from)
}

fn try_open_in_maho_browser(url: &str) -> bool {
    #[cfg(target_os = "macos")]
    {
        let mut candidates: Vec<std::path::PathBuf> = Vec::new();
        candidates.push(std::path::PathBuf::from("/Applications/Maho.app"));
        if let Some(home) = std::env::var_os("HOME") {
            candidates.push(std::path::PathBuf::from(&home).join("Applications/Maho.app"));
        }

        if !candidates.iter().any(|p| p.exists()) {
            return false;
        }

        // `open -a Maho <url>` launches Maho.app (or focuses it if already
        // running) and hands off the URL as a launch argument, so Maho
        // opens it in a new tab per its own URL-handling logic.
        std::process::Command::new("open")
            .args(["-a", "Maho", url])
            .status()
            .map(|s| s.success())
            .unwrap_or(false)
    }

    #[cfg(windows)]
    {
        fn maho_exe() -> Option<std::path::PathBuf> {
            if let Ok(out) = std::process::Command::new("reg")
                .args(["query", r"HKCU\Software\Maho", "/v", "InstallPath"])
                .output()
            {
                if out.status.success() {
                    if let Ok(text) = String::from_utf8(out.stdout) {
                        // Line format: "    InstallPath    REG_SZ    C:\...\Maho"
                        if let Some(dir) = text
                            .lines()
                            .find(|l| l.contains("InstallPath"))
                            .and_then(|l| l.split("REG_SZ").nth(1))
                            .map(str::trim)
                            .filter(|d| !d.is_empty())
                        {
                            let exe = std::path::Path::new(dir).join("maho.exe");
                            if exe.exists() {
                                return Some(exe);
                            }
                        }
                    }
                }
            }
            if let Some(local) = std::env::var_os("LOCALAPPDATA") {
                let exe = std::path::PathBuf::from(local)
                    .join("Programs")
                    .join("Maho")
                    .join("maho.exe");
                if exe.exists() {
                    return Some(exe);
                }
            }
            None
        }

        match maho_exe() {
            Some(exe) => std::process::Command::new(exe)
                .arg(url)
                .spawn()
                .map(|_| true)
                .unwrap_or(false),
            None => false,
        }
    }

    #[cfg(target_os = "linux")]
    {
        fn maho_exe() -> Option<std::path::PathBuf> {
            let packaged = std::path::PathBuf::from("/opt/maho/maho");
            if packaged.exists() {
                return Some(packaged);
            }
            if let Some(home) = std::env::var_os("HOME") {
                let desktop =
                    std::path::PathBuf::from(&home).join(".local/share/applications/maho.desktop");
                if let Ok(text) = std::fs::read_to_string(&desktop) {
                    // Exec=/opt/maho/maho %U → take the first token.
                    if let Some(exe) = text
                        .lines()
                        .find_map(|l| l.strip_prefix("Exec="))
                        .and_then(|e| e.split_whitespace().next())
                        .map(std::path::PathBuf::from)
                        .filter(|p| p.exists())
                    {
                        return Some(exe);
                    }
                }
            }
            None
        }

        match maho_exe() {
            Some(exe) => std::process::Command::new(exe)
                .arg(url)
                .spawn()
                .map(|_| true)
                .unwrap_or(false),
            None => false,
        }
    }

    #[cfg(not(any(target_os = "macos", target_os = "linux", windows)))]
    {
        let _ = url;
        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn checkout_url_for_pack_returns_known_slugs() {
        assert_eq!(
            checkout_url_for_pack("10"),
            "https://maho.lemonsqueezy.com/buy/credit-pack-10"
        );
        assert_eq!(
            checkout_url_for_pack("50"),
            "https://maho.lemonsqueezy.com/buy/credit-pack-50"
        );
        assert_eq!(
            checkout_url_for_pack("100"),
            "https://maho.lemonsqueezy.com/buy/credit-pack-100"
        );
    }

    #[test]
    fn checkout_url_for_pack_falls_back_on_unknown() {
        assert_eq!(
            checkout_url_for_pack("999"),
            "https://maho.lemonsqueezy.com/buy"
        );
        assert_eq!(
            checkout_url_for_pack(""),
            "https://maho.lemonsqueezy.com/buy"
        );
        assert_eq!(
            checkout_url_for_pack("abc"),
            "https://maho.lemonsqueezy.com/buy"
        );
    }

    #[test]
    fn open_checkout_url_skips_when_test_env_set() {
        // Env-mutating; if more env-touching tests are added, gate them with
        // serial_test to avoid parallel-run races.
        std::env::set_var("MAHO_CLI_TEST", "1");
        let outcome = open_checkout_url("https://example.com/never-opened")
            .expect("skip path should not error");
        assert_eq!(outcome, OpenOutcome::SkippedForTest);
        std::env::remove_var("MAHO_CLI_TEST");
    }
}
