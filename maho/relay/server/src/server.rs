use std::net::{IpAddr, SocketAddr};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;

use axum::extract::ws::WebSocketUpgrade;
use axum::extract::{ConnectInfo, State};
use axum::http::StatusCode;
use axum::response::IntoResponse;
use axum::routing::get;
use axum::Router;
use dashmap::mapref::entry::Entry;
use dashmap::{DashMap, DashSet};
use sea_orm::{ConnectionTrait, DatabaseConnection};
use tokio::net::TcpListener;

use crate::auth;
use crate::config::RelayConfig;
use crate::handler;
use crate::room::RoomManager;
use crate::sync;

#[derive(Debug)]
pub struct FixedWindowRateLimiter {
    limit_per_minute: u32,
    windows: DashMap<IpAddr, RateLimitWindow>,
}

#[derive(Clone, Copy, Debug)]
struct RateLimitWindow {
    window_minute: i64,
    count: u32,
}

impl FixedWindowRateLimiter {
    pub fn new(limit_per_minute: u32) -> Self {
        Self {
            limit_per_minute,
            windows: DashMap::new(),
        }
    }

    pub fn allow(&self, ip: IpAddr) -> bool {
        self.allow_at(ip, chrono::Utc::now().timestamp())
    }

    fn allow_at(&self, ip: IpAddr, now: i64) -> bool {
        let minute = now / 60;
        match self.windows.entry(ip) {
            Entry::Occupied(mut entry) => {
                let window = entry.get_mut();
                if window.window_minute != minute {
                    window.window_minute = minute;
                    window.count = 0;
                }

                if window.count >= self.limit_per_minute {
                    false
                } else {
                    window.count += 1;
                    true
                }
            }
            Entry::Vacant(entry) => {
                entry.insert(RateLimitWindow {
                    window_minute: minute,
                    count: 1,
                });
                true
            }
        }
    }
}

pub struct AppState {
    pub db: DatabaseConnection,
    pub config: RelayConfig,
    pub rooms: RoomManager,
    pub ip_connections: DashMap<IpAddr, AtomicU32>,
    pub ip_rooms: DashMap<IpAddr, DashSet<String>>,
    pub signup_rate_limiter: FixedWindowRateLimiter,
    pub login_rate_limiter: FixedWindowRateLimiter,
    pub waitlist_rate_limiter: FixedWindowRateLimiter,
}

async fn health() -> &'static str {
    "ok"
}

async fn ws_upgrade(
    ws: WebSocketUpgrade,
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
    State(state): State<Arc<AppState>>,
) -> impl IntoResponse {
    let ip = addr.ip();
    {
        let count = state
            .ip_connections
            .entry(ip)
            .or_insert_with(|| AtomicU32::new(0));
        let current = count.load(Ordering::Relaxed);
        if current >= state.config.max_connections_per_ip {
            return StatusCode::TOO_MANY_REQUESTS.into_response();
        }
        count.fetch_add(1, Ordering::Relaxed);
    }

    ws.on_upgrade(move |socket| handler::handle_socket(socket, state, ip))
        .into_response()
}

pub fn router(state: Arc<AppState>) -> Router {
    let db = state.db.clone();
    tokio::spawn(async move {
        let _ = db.execute_unprepared("CREATE TABLE IF NOT EXISTS shortcuts (room_id TEXT, action TEXT, key_combo_json TEXT, enabled INTEGER NOT NULL DEFAULT 1, updated_at INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (room_id, action));").await;
    });

    Router::new()
        .route("/health", get(health))
        .nest("/auth", auth::routes::router())
        .nest("/sync", sync::routes::router())
        .nest("/billing", crate::billing::router())
        .nest("/api/v1/shortcuts", crate::shortcuts::router())
        .nest("/waitlist", crate::waitlist::router())
        .route("/ws", get(ws_upgrade))
        .with_state(state)
}


pub async fn run(config: RelayConfig, db: DatabaseConnection) -> Result<(), Box<dyn std::error::Error>> {
    let addr = format!("0.0.0.0:{}", config.port);
    let state = Arc::new(AppState {
        rooms: RoomManager::new(config.max_connections_per_room),
        signup_rate_limiter: FixedWindowRateLimiter::new(config.signup_rate_limit_per_ip_per_minute),
        login_rate_limiter: FixedWindowRateLimiter::new(config.login_rate_limit_per_ip_per_minute),
        waitlist_rate_limiter: FixedWindowRateLimiter::new(crate::waitlist::RATE_LIMIT_PER_MINUTE),
        config,
        db,
        ip_connections: DashMap::new(),
        ip_rooms: DashMap::new(),
    });

    let app = router(state);

    tracing::info!("relay listening on {}", addr);
    let listener = TcpListener::bind(&addr).await?;

    axum::serve(listener, app.into_make_service_with_connect_info::<SocketAddr>())
        .with_graceful_shutdown(shutdown_signal())
        .await?;

    Ok(())
}

async fn shutdown_signal() {
    let _ = tokio::signal::ctrl_c().await;
    tracing::info!("shutdown signal received");
}
