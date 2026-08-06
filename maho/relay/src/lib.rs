#![allow(clippy::disallowed_methods)]

use std::cell::RefCell;

use worker::*;

pub mod auth;
pub mod billing;
pub mod config;
pub mod db;
pub mod errors;
pub mod jobs;
pub mod jobs_routes;
pub mod metrics;
pub mod ratelimit;
pub mod room;
pub mod sync;
pub mod updates;
pub mod waitlist;

#[allow(clippy::disallowed_methods)]
#[event(fetch)]
pub async fn fetch(req: Request, env: Env, _ctx: Context) -> Result<Response> {
    console_error_panic_hook::set_once();

    let router = Router::new();

    router
        .get("/health", |_, _| Response::ok("ok"))
        .get_async("/", |_, _| async { Response::ok("maho-relay worker") })
        .post_async("/waitlist", |req, ctx| async move {
            match waitlist::handler(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/auth/signup", |req, ctx| async move {
            match auth::routes::signup(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/auth/login", |req, ctx| async move {
            match auth::routes::login(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/auth/oauth/google", |req, ctx| async move {
            match auth::routes::oauth_google(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/auth/oauth/google/exchange", |req, ctx| async move {
            match auth::routes::oauth_google_exchange(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/auth/logout", |req, ctx| async move {
            match auth::routes::logout(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/auth/refresh", |req, ctx| async move {
            match auth::routes::refresh(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .get_async("/auth/me", |req, ctx| async move {
            match auth::routes::me(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .get_async("/auth/subscription", |req, ctx| async move {
            match auth::routes::subscription(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .delete_async("/auth/account", |req, ctx| async move {
            match auth::routes::delete_account_route(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .delete_async("/auth/devices/:id", |req, ctx| async move {
            let id_str = ctx.param("id").map(|s| s.to_string()).unwrap_or_default();
            match auth::routes::delete_device(req, ctx.env, &id_str).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/auth/introspect", |req, ctx| async move {
            match auth::routes::introspect(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/auth/request_verify", |req, ctx| async move {
            match auth::routes::request_verify(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/auth/verify", |req, ctx| async move {
            match auth::routes::verify(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/auth/consume_bypass", |req, ctx| async move {
            match auth::routes::consume_bypass(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/auth/admin/force_verify_email", |req, ctx| async move {
            match auth::routes::admin_force_verify_email(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/auth/admin/set_credit_balance", |req, ctx| async move {
            match auth::routes::admin_set_credit_balance(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .get_async("/admin/metrics/funnel", |req, ctx| async move {
            match metrics::funnel(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/billing/lemonsqueezy", |req, ctx| async move {
            match billing::lemonsqueezy_webhook(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/billing/portal_session", |req, ctx| async move {
            match billing::portal_session(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .get_async("/billing/invoices", |req, ctx| async move {
            match billing::invoices(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .get_async("/billing/credit_balance", |req, ctx| async move {
            match billing::get_credit_balance(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/billing/credit_balance", |req, ctx| async move {
            match billing::get_credit_balance(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/billing/consume_credit", |req, ctx| async move {
            match billing::consume_credit(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/jobs/create", |req, ctx| async move {
            match jobs_routes::create_job(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .get_async("/jobs/:id", |req, ctx| async move {
            let id = ctx.param("id").map(|s| s.to_string()).unwrap_or_default();
            match jobs_routes::get_job(req, ctx.env, &id).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/jobs/complete", |req, ctx| async move {
            match jobs_routes::complete_job(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/jobs/progress", |req, ctx| async move {
            match jobs_routes::progress_job(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/jobs/fail", |req, ctx| async move {
            match jobs_routes::fail_job(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/sync/snapshot/upload", |req, ctx| async move {
            match sync::routes::upload_snapshot(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .get_async("/sync/snapshot/latest", |req, ctx| async move {
            match sync::routes::latest_snapshot(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .get_async("/updates/config", |req, ctx| async move {
            match updates::get_config(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .post_async("/updates/config", |req, ctx| async move {
            match updates::post_config(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .on_async("/ws", |req, ctx| async move {
            match room::ws_upgrade(req, ctx.env).await {
                Ok(res) => Ok(res),
                Err(e) => e.to_response(),
            }
        })
        .run(req, env)
        .await
}

const OAUTH_NONCE_REPLAY_RETENTION_SECS: i64 = 30 * 86_400;
const PRUNE_CONSUMED_OAUTH_NONCES_SQL: &str =
    "DELETE FROM consumed_oauth_nonces WHERE consumed_at < ?1";

fn oauth_nonce_cutoff(now: i64) -> i64 {
    now - OAUTH_NONCE_REPLAY_RETENTION_SECS
}

#[event(scheduled)]
pub async fn scheduled(_event: ScheduledEvent, env: Env, _ctx: ScheduleContext) {
    console_log!("scheduled maintenance start");
    let Ok(db) = env.d1("MAHO_DB") else {
        console_error!("scheduled: no D1 binding");
        return;
    };
    let now = chrono::Utc::now().timestamp();
    let retention_days = config::RelayConfig::from_env(&env).retention_days as i64;
    let messages_cutoff = now - (retention_days * 86_400);
    let bypass_cutoff = now - (30 * 86_400);
    // processed_events must outlive the webhook retry window so claim-first
    // idempotency still recognizes a retried event. admin_audit is never pruned.
    let events_cutoff = now - (90 * 86_400);
    let jobs_cutoff = now - (90 * 86_400);

    prune(
        &db,
        "messages",
        "DELETE FROM messages WHERE created_at < ?1",
        messages_cutoff,
    )
    .await;

    // Messages just pruned no longer count toward room_meta.total_bytes; recompute
    // so a room cannot stay permanently locked at room_size_limit_exceeded.
    if let Err(e) = db
        .prepare(
            "UPDATE room_meta SET total_bytes = COALESCE(
                (SELECT SUM(LENGTH(blob)) FROM messages WHERE messages.room_id = room_meta.room_id), 0)",
        )
        .run()
        .await
    {
        console_error!("scheduled: recompute total_bytes err: {e}");
    }

    prune(
        &db,
        "sessions",
        "DELETE FROM sessions WHERE refresh_expires_at < ?1",
        now,
    )
    .await;
    prune(
        &db,
        "consumed_bypass_tokens",
        "DELETE FROM consumed_bypass_tokens WHERE consumed_at < ?1",
        bypass_cutoff,
    )
    .await;
    prune(
        &db,
        "consumed_oauth_nonces",
        PRUNE_CONSUMED_OAUTH_NONCES_SQL,
        oauth_nonce_cutoff(now),
    )
    .await;
    prune(
        &db,
        "processed_events",
        "DELETE FROM processed_events WHERE processed_at < ?1",
        events_cutoff,
    )
    .await;
    prune(
        &db,
        "handoff_jobs",
        "DELETE FROM handoff_jobs WHERE status IN ('completed','failed') AND updated_at < ?1",
        jobs_cutoff,
    )
    .await;

    prune(
        &db,
        "user_activity_days",
        "DELETE FROM user_activity_days WHERE day < ?1",
        db::day_index(now) - 90,
    )
    .await;

    console_log!("scheduled maintenance done");
}

async fn prune(db: &D1Database, label: &str, sql: &str, cutoff: i64) {
    match db.prepare(sql).bind(&[cutoff.into()]) {
        Ok(stmt) => {
            if let Err(e) = stmt.run().await {
                console_error!("scheduled: prune {label} err: {e}");
            }
        }
        Err(e) => console_error!("scheduled: bind {label} err: {e}"),
    }
}

#[durable_object]
pub struct RoomDO {
    state: State,
    env: Env,
    room_id: RefCell<Option<String>>,
}

impl DurableObject for RoomDO {
    fn new(state: State, env: Env) -> Self {
        Self {
            state,
            env,
            room_id: RefCell::new(None),
        }
    }

    async fn fetch(&self, req: Request) -> Result<Response> {
        room::handle_do_fetch(self, req).await
    }

    async fn alarm(&self) -> Result<Response> {
        room::handle_alarm(self).await
    }

    async fn websocket_message(
        &self,
        ws: WebSocket,
        message: WebSocketIncomingMessage,
    ) -> Result<()> {
        room::handle_ws_message(self, ws, message).await
    }

    async fn websocket_close(
        &self,
        ws: WebSocket,
        _code: usize,
        _reason: String,
        _was_clean: bool,
    ) -> Result<()> {
        room::handle_ws_close(self, ws).await
    }

    async fn websocket_error(&self, ws: WebSocket, _error: Error) -> Result<()> {
        room::handle_ws_close(self, ws).await
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn oauth_nonce_pruning_keeps_the_replay_window_boundary() {
        use rusqlite::Connection;

        let db = Connection::open_in_memory().unwrap();
        db.execute_batch(include_str!("../migrations/0021_add_oauth_identities.sql"))
            .unwrap();
        let now = 40 * 86_400;
        let cutoff = oauth_nonce_cutoff(now);
        db.execute(
            "INSERT INTO consumed_oauth_nonces (nonce_hash, consumed_at) VALUES (x'01', ?1), (x'02', ?2), (x'03', ?3)",
            [cutoff - 1, cutoff, cutoff + 1],
        )
        .unwrap();

        db.execute(PRUNE_CONSUMED_OAUTH_NONCES_SQL, [cutoff])
            .unwrap();

        let remaining: Vec<i64> = db
            .prepare("SELECT consumed_at FROM consumed_oauth_nonces ORDER BY consumed_at")
            .unwrap()
            .query_map([], |row| row.get(0))
            .unwrap()
            .map(std::result::Result::unwrap)
            .collect();
        assert_eq!(remaining, vec![cutoff, cutoff + 1]);
    }

    #[test]
    fn room_do_uses_the_workers_rs_shared_callback_contract() {
        // Given: workers-rs 0.6 invokes Durable Object callbacks through shared references.
        fn requires_durable_object<T: DurableObject>() {}

        // When: RoomDO is checked against the runtime trait.
        requires_durable_object::<RoomDO>();

        // Then: callback receiver changes remain covered by the trait contract.
    }
}
