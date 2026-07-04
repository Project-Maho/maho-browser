mod auth;
mod config;
mod handler;
mod room;
mod server;
mod storage;
mod sync;
mod billing;
mod shortcuts;

use config::RelayConfig;

use sea_orm::{ConnectionTrait, Database};
use sea_orm_migration::MigratorTrait;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    tracing_subscriber::fmt::init();

    let config = RelayConfig::from_env();

    if config.service_token.trim().is_empty() {
        eprintln!(
            "FATAL: MAHO_RELAY_SERVICE_TOKEN env var must be set to a non-empty value before startup. \
             This token authorizes the billing-proxy ↔ relay private RPCs and must be a cryptographically random secret."
        );
        std::process::exit(2);
    }

    let db_url = format!("sqlite:{}?mode=rwc", config.db_path);
    let db = Database::connect(&db_url).await?;

    maho_relay_migration::Migrator::up(&db, None).await?;

    db.execute_unprepared("PRAGMA journal_mode=WAL;").await?;

    tracing::info!(
        port = config.port,
        db = %config.db_path,
        max_connections_per_ip = config.max_connections_per_ip,
        max_rooms_per_ip = config.max_rooms_per_ip,
        retention_days = config.retention_days,
        "starting maho-relay",
    );

    let prune_db = db.clone();
    let retention_secs = i64::from(config.retention_days) * 86400;
    tokio::spawn(async move {
        let mut interval = tokio::time::interval(std::time::Duration::from_secs(3600));
        loop {
            interval.tick().await;

            let _ = storage::delete_expired_sessions(&prune_db).await;

            let rooms = match storage::list_room_ids(&prune_db).await {
                Ok(r) => r,
                Err(_) => continue,
            };
            for rid in rooms {
                let _ = storage::prune_room(&prune_db, &rid, retention_secs).await;
            }
        }
    });

    server::run(config, db).await
}
