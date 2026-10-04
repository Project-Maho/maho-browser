use serde::Deserialize;

/// Single pay-what-you-want LemonSqueezy product buy link (variant 1988896).
/// Kept in sync with maho_subscription_checkout.h (kPaygCreditsCheckoutUrl).
pub const PAYG_CREDITS_CHECKOUT_URL: &str =
    "https://noveling.lemonsqueezy.com/checkout/buy/93f5ccbc-cd1c-4c4a-94a0-5ff4ad8d2c35";

/// Self balance read uses `/auth/subscription` (user bearer token), NOT the
/// service-token-gated `/billing/credit_balance` route which is Worker-only.
#[derive(Deserialize)]
pub struct CreditBalanceResponse {
    #[serde(rename = "credit_balance_usd")]
    pub balance_usd: f64,
    #[serde(default)]
    pub lifetime_purchased_usd: f64,
}

pub async fn fetch_balance(
    relay_url: &str,
    access_token: &str,
) -> anyhow::Result<CreditBalanceResponse> {
    let client = reqwest::Client::builder()
        .timeout(std::time::Duration::from_secs(10))
        .build()?;
    let resp = client
        .get(format!("{relay_url}/auth/subscription"))
        .header("Authorization", format!("Bearer {access_token}"))
        .send()
        .await?;
    if !resp.status().is_success() {
        anyhow::bail!("balance fetch failed: HTTP {}", resp.status());
    }
    Ok(resp.json().await?)
}

/// Builds the pay-what-you-want credits checkout URL, attributing the purchase
/// to numeric `user_id` via checkout custom data (matching relay billing parsing
/// and C++ `BuildPaygCreditsCheckoutUrl` in `maho_subscription_checkout.h`).
///
/// Fails closed when `user_id <= 0` so callers cannot return or open an
/// unattributable checkout that would credit no account.
pub fn build_payg_credits_checkout_url(user_id: i32) -> anyhow::Result<String> {
    if user_id <= 0 {
        anyhow::bail!("positive numeric user_id is required to attribute checkout");
    }
    Ok(format!(
        "{PAYG_CREDITS_CHECKOUT_URL}?checkout[custom][user_id]={user_id}"
    ))
}

pub fn checkout_url_for_pack(_amount: &str, user_id: i32) -> anyhow::Result<String> {
    build_payg_credits_checkout_url(user_id)
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
    fn checkout_url_for_pack_attributes_numeric_user_id() {
        let expected =
            "https://noveling.lemonsqueezy.com/checkout/buy/93f5ccbc-cd1c-4c4a-94a0-5ff4ad8d2c35?checkout[custom][user_id]=42";
        assert_eq!(checkout_url_for_pack("10", 42).unwrap(), expected);
        assert_eq!(checkout_url_for_pack("50", 42).unwrap(), expected);
        assert_eq!(checkout_url_for_pack("100", 42).unwrap(), expected);
    }

    #[test]
    fn checkout_url_for_pack_fails_closed_when_user_id_non_positive() {
        assert!(checkout_url_for_pack("10", 0).is_err());
        assert!(checkout_url_for_pack("10", -1).is_err());
        assert!(build_payg_credits_checkout_url(0).is_err());
        assert!(build_payg_credits_checkout_url(-5).is_err());
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
