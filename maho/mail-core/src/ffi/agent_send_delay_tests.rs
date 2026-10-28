//! Undo-window queue for agent mail. Transport is a counting fake and time is
//! an explicit `now_ms`, so no test depends on wall-clock timing or SMTP.
use super::*;
use std::sync::atomic::{AtomicUsize, Ordering};

const T0: i64 = 1_790_000_000_000;

fn fixture() -> Arc<AppCtx> {
    crate::test_support::ctx_arc(crate::test_support::pool_with_seeded_data())
}

fn request(subject: &str) -> ComposeEmailRequest {
    ComposeEmailRequest {
        account_id: "acc1".to_string(),
        to: vec!["to@example.test".to_string()],
        cc: None,
        bcc: None,
        subject: subject.to_string(),
        body_text: Some("body".to_string()),
        body_html: None,
        read_receipt: None,
        attachments: None,
        in_reply_to: None,
        references: None,
    }
}

fn set_delay(ctx: &AppCtx, value: &str) {
    ctx.with_db(|conn| {
        conn.execute(
            "INSERT INTO app_settings (key, value) VALUES (?1, ?2)
             ON CONFLICT(key) DO UPDATE SET value = excluded.value",
            params![DELAY_SETTING_KEY, value],
        )?;
        Ok(())
    })
    .unwrap();
}

fn status(ctx: &AppCtx, id: &str) -> String {
    ctx.with_db(|conn| {
        Ok(conn.query_row("SELECT status FROM agent_send_queue WHERE id = ?1", [id], |r| r.get(0))?)
    })
    .unwrap()
}

struct FakeTransport {
    sent: AtomicUsize,
    fail: bool,
}

impl FakeTransport {
    fn new(fail: bool) -> Arc<Self> {
        Arc::new(Self { sent: AtomicUsize::new(0), fail })
    }
    fn send(
        self: &Arc<Self>,
    ) -> impl Fn(Arc<AppCtx>, ComposeEmailRequest) -> std::pin::Pin<Box<dyn Future<Output = Result<String>> + Send>> {
        let me = self.clone();
        move |_ctx, _req| {
            let me = me.clone();
            Box::pin(async move {
                me.sent.fetch_add(1, Ordering::SeqCst);
                if me.fail {
                    Err(MailFfiError::Internal("554 rejected".to_string()))
                } else {
                    Ok(r#"{"status":"sent"}"#.to_string())
                }
            })
        }
    }
    fn count(&self) -> usize {
        self.sent.load(Ordering::SeqCst)
    }
}

fn block_on<T>(f: impl Future<Output = T>) -> T {
    crate::runtime::runtime().unwrap().block_on(f)
}

#[test]
fn agent_send_delay_defaults_to_zero_and_rejects_non_options() {
    let ctx = fixture();
    assert_eq!(ctx.with_db(|c| Ok(configured_delay_seconds(c))).unwrap(), 0);
    for (raw, expected) in [("10", 10), ("20", 20), ("\"30\"", 30), ("0", 0), ("15", 0), ("-5", 0), ("abc", 0), ("600", 0)] {
        set_delay(&ctx, raw);
        assert_eq!(ctx.with_db(|c| Ok(configured_delay_seconds(c))).unwrap(), expected, "{raw}");
    }
}

#[test]
fn agent_send_delay_cancel_within_window_is_never_sent() {
    let ctx = fixture();
    let transport = FakeTransport::new(false);
    let queued = ctx.with_db(|c| enqueue(c, &request("cancel me"), 10, T0)).unwrap();
    assert_eq!(queued.status, "queued");
    assert_eq!(queued.send_at_ms, T0 + 10_000);

    assert!(ctx.with_db(|c| cancel(c, &queued.id)).unwrap(), "cancel inside window succeeds");
    assert!(!ctx.with_db(|c| cancel(c, &queued.id)).unwrap(), "second cancel is a no-op");

    let delivered = block_on(run_due_with(&ctx, T0 + 60_000, transport.send())).unwrap();
    assert_eq!(delivered, 0);
    assert_eq!(transport.count(), 0);
    assert_eq!(status(&ctx, &queued.id), "cancelled");
    assert!(ctx.with_db(list_pending).unwrap().is_empty());
}

#[test]
fn agent_send_delay_no_cancel_sends_once_after_window_never_twice() {
    let ctx = fixture();
    let transport = FakeTransport::new(false);
    let queued = ctx.with_db(|c| enqueue(c, &request("send me"), 20, T0)).unwrap();
    assert_eq!(ctx.with_db(list_pending).unwrap().len(), 1);

    // Before the deadline nothing goes out.
    assert_eq!(block_on(run_due_with(&ctx, T0 + 19_999, transport.send())).unwrap(), 0);
    assert!(!block_on(deliver_if_due(&ctx, &queued.id, T0 + 19_999, transport.send())).unwrap());
    assert_eq!(transport.count(), 0);
    assert_eq!(status(&ctx, &queued.id), "pending");

    // At the deadline: exactly one delivery, then every other path is a no-op.
    assert_eq!(block_on(run_due_with(&ctx, T0 + 20_000, transport.send())).unwrap(), 1);
    assert_eq!(block_on(run_due_with(&ctx, T0 + 90_000, transport.send())).unwrap(), 0);
    assert!(!block_on(deliver_if_due(&ctx, &queued.id, T0 + 90_000, transport.send())).unwrap());
    assert_eq!(transport.count(), 1);
    assert_eq!(status(&ctx, &queued.id), "sent");
    assert!(!ctx.with_db(|c| cancel(c, &queued.id)).unwrap(), "cannot cancel after send");
}

#[test]
fn agent_send_delay_concurrent_sweeps_deliver_exactly_once() {
    let ctx = fixture();
    let transport = FakeTransport::new(false);
    for i in 0..5 {
        ctx.with_db(|c| enqueue(c, &request(&format!("m{i}")), 10, T0)).unwrap();
    }
    let rt = crate::runtime::runtime().unwrap();
    let handles: Vec<_> = (0..4)
        .map(|_| {
            let ctx = ctx.clone();
            let send = transport.send();
            rt.spawn(async move { run_due_with(&ctx, T0 + 10_000, send).await.unwrap() })
        })
        .collect();
    let total: usize = block_on(async {
        let mut sum = 0;
        for h in handles {
            sum += h.await.unwrap();
        }
        sum
    });
    assert_eq!(total, 5);
    assert_eq!(transport.count(), 5);
}

#[test]
fn agent_send_delay_failed_send_is_recorded_not_retried() {
    let ctx = fixture();
    let transport = FakeTransport::new(true);
    let queued = ctx.with_db(|c| enqueue(c, &request("fails"), 10, T0)).unwrap();
    assert!(block_on(deliver_if_due(&ctx, &queued.id, T0 + 10_000, transport.send())).unwrap());
    assert_eq!(status(&ctx, &queued.id), "failed");
    assert_eq!(block_on(run_due_with(&ctx, T0 + 99_000, transport.send())).unwrap(), 0);
    assert_eq!(transport.count(), 1);
}

#[test]
fn agent_send_delay_restart_resends_pending_but_never_replays_interrupted() {
    let ctx = fixture();
    let transport = FakeTransport::new(false);
    let pending = ctx.with_db(|c| enqueue(c, &request("survives crash"), 30, T0)).unwrap();
    let interrupted = ctx.with_db(|c| enqueue(c, &request("mid-smtp"), 10, T0)).unwrap();
    // Simulate a crash during SMTP for `interrupted`: it was claimed, never finished.
    ctx.with_db(|c| {
        c.execute("UPDATE agent_send_queue SET status = 'sending' WHERE id = ?1", [&interrupted.id])?;
        Ok(())
    })
    .unwrap();

    let rearm = ctx.with_db(recover_rows).unwrap();
    assert_eq!(rearm, vec![(pending.id.clone(), T0 + 30_000)]);
    assert_eq!(status(&ctx, &interrupted.id), "uncertain");

    // Restarted after the deadline already passed: sent once, immediately.
    assert_eq!(block_on(run_due_with(&ctx, T0 + 3_600_000, transport.send())).unwrap(), 1);
    assert_eq!(block_on(run_due_with(&ctx, T0 + 3_600_001, transport.send())).unwrap(), 0);
    assert_eq!(transport.count(), 1);
    assert_eq!(status(&ctx, &pending.id), "sent");
    assert_eq!(status(&ctx, &interrupted.id), "uncertain");
}

#[test]
fn agent_send_delay_enabled_returns_queued_and_toast_cancel_stops_it() {
    let ctx = fixture();
    set_delay(&ctx, "10");
    let json = block_on(agent_send(ctx.clone(), request("agent mail"), None)).unwrap();
    let value: serde_json::Value = serde_json::from_str(&json).unwrap();
    assert_eq!(value["status"], "queued");
    assert_eq!(value["delay_seconds"], 10);
    let id = value["id"].as_str().unwrap().to_string();
    assert_eq!(status(&ctx, &id), "pending");
    // The armed timer fires after 10s and must then find nothing to claim.
    assert!(ctx.with_db(|c| cancel(c, &id)).unwrap());
    assert_eq!(status(&ctx, &id), "cancelled");
}

#[test]
fn agent_send_delay_pref_override_wins_and_is_validated() {
    assert_eq!(delay_override_from_json(r#"{"agent_send_delay_seconds":30}"#), Some(30));
    assert_eq!(delay_override_from_json(r#"{"to":[]}"#), None);
    let ctx = fixture();
    // DB says 0, browser pref says 20 => queued for 20s.
    let json = block_on(agent_send(ctx.clone(), request("pref"), Some(20))).unwrap();
    let value: serde_json::Value = serde_json::from_str(&json).unwrap();
    assert_eq!(value["status"], "queued");
    assert_eq!(value["delay_seconds"], 20);
    assert!(ctx.with_db(|c| cancel(c, value["id"].as_str().unwrap())).unwrap());
}

#[test]
fn agent_send_delay_outbox_fallback_is_not_recorded_as_sent() {
    let ctx = fixture();
    let queued = ctx.with_db(|c| enqueue(c, &request("offline"), 10, T0)).unwrap();
    let send = |_ctx, _req| async { Ok::<_, MailFfiError>(r#"{"status":"queued"}"#.to_string()) };
    assert!(block_on(deliver_if_due(&ctx, &queued.id, T0 + 10_000, send)).unwrap());
    assert_eq!(status(&ctx, &queued.id), "outboxed");
}
