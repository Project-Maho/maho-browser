#![allow(clippy::disallowed_methods)]

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

    prune(&db, "messages", "DELETE FROM messages WHERE created_at < ?1", messages_cutoff).await;

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

    prune(&db, "sessions", "DELETE FROM sessions WHERE refresh_expires_at < ?1", now).await;
    prune(&db, "consumed_bypass_tokens", "DELETE FROM consumed_bypass_tokens WHERE consumed_at < ?1", bypass_cutoff).await;
    prune(&db, "processed_events", "DELETE FROM processed_events WHERE processed_at < ?1", events_cutoff).await;
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
    room_id: Option<String>,
}

#[durable_object]
impl DurableObject for RoomDO {
    fn new(state: State, env: Env) -> Self {
        Self {
            state,
            env,
            room_id: None,
        }
    }

    async fn fetch(&mut self, req: Request) -> Result<Response> {
        room::handle_do_fetch(self, req).await
    }

    async fn alarm(&mut self) -> Result<Response> {
        room::handle_alarm(self).await
    }

    async fn websocket_message(
        &mut self,
        ws: WebSocket,
        message: WebSocketIncomingMessage,
    ) -> Result<()> {
        room::handle_ws_message(self, ws, message).await
    }

    async fn websocket_close(
        &mut self,
        ws: WebSocket,
        _code: usize,
        _reason: String,
        _was_clean: bool,
    ) -> Result<()> {
        room::handle_ws_close(self, ws).await
    }

    async fn websocket_error(&mut self, ws: WebSocket, _error: Error) -> Result<()> {
        room::handle_ws_close(self, ws).await
    }
}
