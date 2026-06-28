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
