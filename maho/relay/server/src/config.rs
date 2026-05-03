use std::env;
use std::str::FromStr;

#[derive(Clone, Debug)]
pub struct RelayConfig {
    pub port: u16,
    pub db_path: String,
    pub max_room_size_mb: u32,
    pub max_connections_per_room: u32,
    pub max_connections_per_ip: u32,
    pub max_rooms_per_ip: u32,
    pub max_messages_per_minute: u32,
    pub max_message_size_bytes: usize,
    pub retention_days: u32,
    pub auth_timeout_secs: u64,
    pub require_auth: bool,
    pub open_registration: bool,
    pub signup_rate_limit_per_ip_per_minute: u32,
    pub login_rate_limit_per_ip_per_minute: u32,
    pub min_password_length: usize,
}

impl RelayConfig {
    pub fn from_env() -> Self {
        let mut config = Self::default();

        let args: Vec<String> = env::args().collect();
        let mut i = 1;
        while i < args.len() {
            match args[i].as_str() {
                "--port" => {
                    if let Some(p) = parse_next::<u16>(&args, &mut i) {
                        config.port = p;
                    }
                }
                "--db-path" => {
                    i += 1;
                    if let Some(v) = args.get(i) {
                        config.db_path = v.clone();
                    }
                }
                "--require-auth" => {
                    config.require_auth = true;
                }
                "--no-require-auth" => {
                    config.require_auth = false;
                }
                "--open-registration" => {
                    config.open_registration = true;
                }
                "--closed-registration" => {
                    config.open_registration = false;
                }
                "--signup-rate-limit-per-ip-per-minute" => {
                    if let Some(v) = parse_next::<u32>(&args, &mut i) {
                        config.signup_rate_limit_per_ip_per_minute = v;
                    }
                }
                "--login-rate-limit-per-ip-per-minute" => {
                    if let Some(v) = parse_next::<u32>(&args, &mut i) {
                        config.login_rate_limit_per_ip_per_minute = v;
                    }
                }
                "--min-password-length" => {
                    if let Some(v) = parse_next::<usize>(&args, &mut i) {
                        config.min_password_length = v;
                    }
                }
                _ => {}
            }
            i += 1;
        }

        if let Some(v) = parse_env::<u16>("MAHO_RELAY_PORT") {
            config.port = v;
        }
        if let Some(v) = parse_env::<String>("MAHO_RELAY_DB_PATH") {
            config.db_path = v;
        }
        if let Some(v) = parse_env::<u32>("MAHO_RELAY_MAX_ROOM_SIZE_MB") {
            config.max_room_size_mb = v;
        }
        if let Some(v) = parse_env::<u32>("MAHO_RELAY_SIGNUP_RATE_LIMIT_PER_IP_PER_MINUTE") {
            config.signup_rate_limit_per_ip_per_minute = v;
        }
        if let Some(v) = parse_env::<u32>("MAHO_RELAY_LOGIN_RATE_LIMIT_PER_IP_PER_MINUTE") {
            config.login_rate_limit_per_ip_per_minute = v;
        }
        if let Some(v) = parse_env::<usize>("MAHO_RELAY_MIN_PASSWORD_LENGTH") {
            config.min_password_length = v;
        }
        if let Some(v) = parse_bool_env("MAHO_RELAY_REQUIRE_AUTH") {
            config.require_auth = v;
        }
        if let Some(v) = parse_bool_env("MAHO_RELAY_OPEN_REGISTRATION") {
            config.open_registration = v;
        }

        config
    }
}

impl Default for RelayConfig {
    fn default() -> Self {
        Self {
            port: 8080,
            db_path: "./relay.db".to_string(),
            max_room_size_mb: 50,
            max_connections_per_room: 5,
            max_connections_per_ip: 20,
            max_rooms_per_ip: 10,
            max_messages_per_minute: 100,
            max_message_size_bytes: 1_048_576,
            retention_days: 30,
            auth_timeout_secs: 5,
            require_auth: true,
            open_registration: false,
            signup_rate_limit_per_ip_per_minute: 3,
            login_rate_limit_per_ip_per_minute: 10,
            min_password_length: 12,
        }
    }
}

fn parse_next<T: FromStr>(args: &[String], i: &mut usize) -> Option<T> {
    *i += 1;
    args.get(*i)?.parse().ok()
}

fn parse_env<T: FromStr>(name: &str) -> Option<T> {
    env::var(name).ok()?.parse().ok()
}

fn parse_bool_env(name: &str) -> Option<bool> {
    let value = env::var(name).ok()?;
    match value.trim().to_ascii_lowercase().as_str() {
        "1" | "true" | "yes" | "on" => Some(true),
        "0" | "false" | "no" | "off" => Some(false),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_default_values() {
        let config = RelayConfig::default();
        assert_eq!(config.port, 8080);
        assert_eq!(config.db_path, "./relay.db");
        assert_eq!(config.max_room_size_mb, 50);
        assert_eq!(config.max_connections_per_room, 5);
        assert_eq!(config.max_connections_per_ip, 20);
        assert_eq!(config.max_rooms_per_ip, 10);
        assert_eq!(config.max_messages_per_minute, 100);
        assert_eq!(config.max_message_size_bytes, 1_048_576);
        assert_eq!(config.retention_days, 30);
        assert_eq!(config.auth_timeout_secs, 5);
        assert!(config.require_auth);
        assert!(!config.open_registration);
        assert_eq!(config.signup_rate_limit_per_ip_per_minute, 3);
        assert_eq!(config.login_rate_limit_per_ip_per_minute, 10);
        assert_eq!(config.min_password_length, 12);
    }

    #[test]
    fn test_env_var_override() {
        env::set_var("MAHO_RELAY_PORT", "9090");
        env::set_var("MAHO_RELAY_DB_PATH", "/tmp/test.db");
        env::set_var("MAHO_RELAY_MAX_ROOM_SIZE_MB", "100");
        env::set_var("MAHO_RELAY_REQUIRE_AUTH", "true");
        env::set_var("MAHO_RELAY_OPEN_REGISTRATION", "false");
        env::set_var("MAHO_RELAY_SIGNUP_RATE_LIMIT_PER_IP_PER_MINUTE", "7");
        env::set_var("MAHO_RELAY_LOGIN_RATE_LIMIT_PER_IP_PER_MINUTE", "11");
        env::set_var("MAHO_RELAY_MIN_PASSWORD_LENGTH", "16");

        let config = RelayConfig::from_env();
        assert_eq!(config.port, 9090);
        assert_eq!(config.db_path, "/tmp/test.db");
        assert_eq!(config.max_room_size_mb, 100);
        assert!(config.require_auth);
        assert!(!config.open_registration);
        assert_eq!(config.signup_rate_limit_per_ip_per_minute, 7);
        assert_eq!(config.login_rate_limit_per_ip_per_minute, 11);
        assert_eq!(config.min_password_length, 16);

        env::remove_var("MAHO_RELAY_PORT");
        env::remove_var("MAHO_RELAY_DB_PATH");
        env::remove_var("MAHO_RELAY_MAX_ROOM_SIZE_MB");
        env::remove_var("MAHO_RELAY_REQUIRE_AUTH");
        env::remove_var("MAHO_RELAY_OPEN_REGISTRATION");
        env::remove_var("MAHO_RELAY_SIGNUP_RATE_LIMIT_PER_IP_PER_MINUTE");
        env::remove_var("MAHO_RELAY_LOGIN_RATE_LIMIT_PER_IP_PER_MINUTE");
        env::remove_var("MAHO_RELAY_MIN_PASSWORD_LENGTH");
    }

    #[test]
    fn test_default_config_all_fields_nonzero() {
        let config = RelayConfig::default();
        assert!(config.port > 0);
        assert!(!config.db_path.is_empty());
        assert!(config.max_room_size_mb > 0);
        assert!(config.max_connections_per_room > 0);
        assert!(config.max_connections_per_ip > 0);
        assert!(config.max_rooms_per_ip > 0);
        assert!(config.max_messages_per_minute > 0);
        assert!(config.max_message_size_bytes > 0);
        assert!(config.retention_days > 0);
        assert!(config.auth_timeout_secs > 0);
        assert!(config.signup_rate_limit_per_ip_per_minute > 0);
        assert!(config.login_rate_limit_per_ip_per_minute > 0);
        assert!(config.min_password_length > 0);
    }
}
