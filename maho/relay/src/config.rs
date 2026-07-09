use worker::*;

#[derive(Clone, Debug)]
pub struct RelayConfig {
    pub require_auth: bool,
    pub open_registration: bool,
    pub signup_rate_limit_per_ip_per_minute: u32,
    pub login_rate_limit_per_ip_per_minute: u32,
    pub min_password_length: usize,
    pub max_room_size_mb: u32,
    pub max_message_size_bytes: usize,
    pub max_connections_per_room: u32,
    pub max_connections_per_ip: u32,
    pub max_rooms_per_ip: u32,
    pub retention_days: u32,
}

impl RelayConfig {
    pub fn from_env(env: &Env) -> Self {
        let require_auth = env.var("REQUIRE_AUTH")
            .map(|v| v.to_string() == "true")
            .unwrap_or(true);
            
        let open_registration = env.var("OPEN_REGISTRATION")
            .map(|v| v.to_string() == "true")
            .unwrap_or(false);
            
        let signup_rate_limit_per_ip_per_minute = env.var("SIGNUP_RATE_LIMIT_PER_IP_PER_MINUTE")
            .ok()
            .and_then(|v| v.to_string().parse().ok())
            .unwrap_or(3);
            
        let login_rate_limit_per_ip_per_minute = env.var("LOGIN_RATE_LIMIT_PER_IP_PER_MINUTE")
            .ok()
            .and_then(|v| v.to_string().parse().ok())
            .unwrap_or(10);
            
        let min_password_length = env.var("MIN_PASSWORD_LENGTH")
            .ok()
            .and_then(|v| v.to_string().parse().ok())
            .unwrap_or(12);
            
        let max_room_size_mb = env.var("MAX_ROOM_SIZE_MB")
            .ok()
            .and_then(|v| v.to_string().parse().ok())
            .unwrap_or(50);
            
        let max_message_size_bytes = env.var("MAX_MESSAGE_SIZE_BYTES")
            .ok()
            .and_then(|v| v.to_string().parse().ok())
            .unwrap_or(1_048_576);
            
        let max_connections_per_room = env.var("MAX_CONNECTIONS_PER_ROOM")
            .ok()
            .and_then(|v| v.to_string().parse().ok())
            .unwrap_or(5);
            
        let max_connections_per_ip = env.var("MAX_CONNECTIONS_PER_IP")
            .ok()
            .and_then(|v| v.to_string().parse().ok())
            .unwrap_or(20);
            
        let max_rooms_per_ip = env.var("MAX_ROOMS_PER_IP")
            .ok()
            .and_then(|v| v.to_string().parse().ok())
            .unwrap_or(10);
            
        let retention_days = env.var("RETENTION_DAYS")
            .ok()
            .and_then(|v| v.to_string().parse().ok())
            .unwrap_or(30);

        Self {
            require_auth,
            open_registration,
            signup_rate_limit_per_ip_per_minute,
            login_rate_limit_per_ip_per_minute,
            min_password_length,
            max_room_size_mb,
            max_message_size_bytes,
            max_connections_per_room,
            max_connections_per_ip,
            max_rooms_per_ip,
            retention_days,
        }
    }
}
