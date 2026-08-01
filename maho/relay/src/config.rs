use worker::*;

#[derive(Clone, Debug)]
pub struct RelayConfig {
    pub require_auth: bool,
    pub open_registration: bool,
    pub min_password_length: usize,
    pub max_room_size_mb: u32,
    pub max_message_size_bytes: usize,
    pub max_connections_per_room: u32,
    pub retention_days: u32,
    pub google_oauth_allowed_auds: Vec<String>,
}

impl RelayConfig {
    pub fn from_env(env: &Env) -> Self {
        let require_auth = env
            .var("REQUIRE_AUTH")
            .map(|v| v.to_string() == "true")
            .unwrap_or(true);

        let open_registration = env
            .var("OPEN_REGISTRATION")
            .map(|v| v.to_string() == "true")
            .unwrap_or(false);

        let min_password_length = env
            .var("MIN_PASSWORD_LENGTH")
            .ok()
            .and_then(|v| v.to_string().parse().ok())
            .unwrap_or(12);

        let max_room_size_mb = env
            .var("MAX_ROOM_SIZE_MB")
            .ok()
            .and_then(|v| v.to_string().parse().ok())
            .unwrap_or(50);

        let max_message_size_bytes = env
            .var("MAX_MESSAGE_SIZE_BYTES")
            .ok()
            .and_then(|v| v.to_string().parse().ok())
            .unwrap_or(1_048_576);

        let max_connections_per_room = env
            .var("MAX_CONNECTIONS_PER_ROOM")
            .ok()
            .and_then(|v| v.to_string().parse().ok())
            .unwrap_or(5);

        let retention_days = env
            .var("RETENTION_DAYS")
            .ok()
            .and_then(|v| v.to_string().parse().ok())
            .unwrap_or(30);

        let google_oauth_allowed_auds = env
            .var("GOOGLE_OAUTH_ALLOWED_AUDS")
            .map(|v| crate::auth::google::parse_audiences(&v.to_string()))
            .unwrap_or_default();

        Self {
            require_auth,
            open_registration,
            min_password_length,
            max_room_size_mb,
            max_message_size_bytes,
            max_connections_per_room,
            retention_days,
            google_oauth_allowed_auds,
        }
    }
}

pub fn tier_ceiling_usd(tier: &str) -> f64 {
    match tier {
        "pro" => 2.10,
        "max" => 13.30,
        _ => 0.50,
    }
}

pub fn max_snapshot_bytes(env: &Env) -> usize {
    env.var("MAX_SNAPSHOT_BYTES")
        .ok()
        .and_then(|v| v.to_string().parse::<usize>().ok())
        .unwrap_or(1024 * 1024)
}

/// Upper bound on decoded bytes implied by a base64 string of `encoded_len`,
/// used to reject oversize uploads before allocating the decode buffer.
pub fn base64_exceeds_cap(encoded_len: usize, max_decoded_bytes: usize) -> bool {
    encoded_len > max_decoded_bytes * 4 / 3 + 4
}

#[derive(Clone, Debug, PartialEq)]
pub enum VariantKind {
    Subscription(&'static str),
    PaygCredit(f64),
}

#[derive(Clone, Debug, Default)]
pub struct VariantMap {
    pub pro: Option<i32>,
    pub max: Option<i32>,
    pub payg_10: Option<i32>,
    pub payg_50: Option<i32>,
    pub payg_100: Option<i32>,
}

impl VariantMap {
    pub fn from_env(env: &Env) -> Self {
        let parse = |key: &str| {
            env.var(key)
                .ok()
                .and_then(|v| v.to_string().parse::<i32>().ok())
        };
        Self {
            pro: parse("LS_VARIANT_PRO"),
            max: parse("LS_VARIANT_MAX"),
            payg_10: parse("LS_VARIANT_PAYG_10"),
            payg_50: parse("LS_VARIANT_PAYG_50"),
            payg_100: parse("LS_VARIANT_PAYG_100"),
        }
    }

    pub fn map_variant(&self, variant_id: i32) -> Option<VariantKind> {
        let v = Some(variant_id);
        if v == self.pro {
            Some(VariantKind::Subscription("pro"))
        } else if v == self.max {
            Some(VariantKind::Subscription("max"))
        } else if v == self.payg_10 {
            Some(VariantKind::PaygCredit(10.0))
        } else if v == self.payg_50 {
            Some(VariantKind::PaygCredit(50.0))
        } else if v == self.payg_100 {
            Some(VariantKind::PaygCredit(100.0))
        } else {
            None
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_map() -> VariantMap {
        VariantMap {
            pro: Some(1001),
            max: Some(2002),
            payg_10: Some(4001),
            payg_50: Some(4002),
            payg_100: Some(4003),
        }
    }

    #[test]
    fn map_variant_resolves_subscription_and_payg() {
        let m = sample_map();
        assert_eq!(m.map_variant(1001), Some(VariantKind::Subscription("pro")));
        assert_eq!(m.map_variant(2002), Some(VariantKind::Subscription("max")));
        assert_eq!(m.map_variant(4001), Some(VariantKind::PaygCredit(10.0)));
        assert_eq!(m.map_variant(4002), Some(VariantKind::PaygCredit(50.0)));
        assert_eq!(m.map_variant(4003), Some(VariantKind::PaygCredit(100.0)));
    }

    #[test]
    fn map_variant_unmapped_returns_none() {
        let m = sample_map();
        assert_eq!(m.map_variant(9999), None);
        assert_eq!(m.map_variant(0), None);
    }

    #[test]
    fn map_variant_unconfigured_ids_do_not_match_zero() {
        let m = VariantMap::default();
        assert_eq!(m.map_variant(0), None);
        assert_eq!(m.map_variant(1001), None);
    }

    #[test]
    fn tier_ceiling_matches_known_tiers() {
        assert_eq!(tier_ceiling_usd("pro"), 2.10);
        assert_eq!(tier_ceiling_usd("max"), 13.30);
        assert_eq!(tier_ceiling_usd("free"), 0.50);
        assert_eq!(tier_ceiling_usd("anything_else"), 0.50);
    }

    #[test]
    fn base64_cap_rejects_oversize_before_decode() {
        let max = 1024 * 1024;
        assert!(!base64_exceeds_cap(0, max));
        assert!(!base64_exceeds_cap(max, max));
        assert!(base64_exceeds_cap(max * 2, max));
    }
}
