// Copyright 2026 Maho Browser. All rights reserved.
// Real migrated SQLCipher fixtures; no network or process-global context.
#![allow(clippy::disallowed_methods)]

use super::*;
use serde_json::{json, Value};

fn fixture() -> AppCtx {
    crate::test_support::ctx(crate::test_support::pool_with_seeded_data())
}

fn search(ctx: &AppCtx, input: Value) -> SearchResult {
    let params: SearchParams = serde_json::from_value(input).unwrap();
    search_emails(ctx, &params).unwrap()
}

fn assert_ids(result: SearchResult, expected: &[&str]) {
    assert_eq!(result.total_count, expected.len() as i64);
    assert_eq!(
        result
            .emails
            .iter()
            .map(|e| e.id.as_str())
            .collect::<Vec<_>>(),
        expected
    );
}

#[test]
fn audit_s01_missing_message_id_folder() {
    let ctx = fixture();
    ctx.pool
        .get()
        .unwrap()
        .execute("UPDATE emails SET message_id=NULL WHERE id='em1'", [])
        .unwrap();
    let emails = list_local_emails(&ctx, "acc1", "fold1", 10, 0).unwrap();
    assert_eq!(emails.len(), 2);
    let value = serde_json::to_value(emails.iter().find(|e| e.id == "em1").unwrap()).unwrap();
    assert!(value["message_id"].is_null() || value["message_id"] == "");
}

#[test]
fn audit_s01_missing_message_id_detail() {
    let ctx = fixture();
    ctx.pool
        .get()
        .unwrap()
        .execute("UPDATE emails SET message_id=NULL WHERE id='em1'", [])
        .unwrap();
    let detail = get_email_blocking(&ctx, "em1").unwrap();
    assert_eq!(detail.email.id, "em1");
    let value = serde_json::to_value(detail).unwrap();
    assert!(value["email"]["message_id"].is_null() || value["email"]["message_id"] == "");
}

#[test]
fn audit_s01_missing_message_id_search() {
    let ctx = fixture();
    ctx.pool
        .get()
        .unwrap()
        .execute("UPDATE emails SET message_id=NULL WHERE id='em1'", [])
        .unwrap();
    assert_ids(search(&ctx, json!({"query":"Hello"})), &["em2", "em1"]);
}

macro_rules! structured_filter {
    ($name:ident, $input:expr, $expected:expr) => {
        #[test]
        fn $name() {
            let ctx = fixture();
            ctx.pool
                .get()
                .unwrap()
                .execute("UPDATE emails SET has_attachments=1 WHERE id='em2'", [])
                .unwrap();
            assert_ids(search(&ctx, $input), $expected);
        }
    };
}

structured_filter!(
    audit_s05_unread_view,
    json!({"query":"", "is_unread":true}),
    &["em1"]
);
structured_filter!(
    audit_s05_starred_view,
    json!({"query":"", "is_starred":true}),
    &["em2"]
);
structured_filter!(
    audit_s05_from,
    json!({"query":"Hello", "from":"sender@example.com"}),
    &["em1"]
);
structured_filter!(
    audit_s05_to,
    json!({"query":"Hello", "to":"sender@example.com"}),
    &["em2"]
);
structured_filter!(
    audit_s05_subject,
    json!({"query":"Hello", "subject":"Re:"}),
    &["em2"]
);
structured_filter!(
    audit_s05_attachment,
    json!({"query":"Hello", "has_attachment":true}),
    &["em2"]
);
structured_filter!(
    audit_s05_date_from,
    json!({"query":"Hello", "date_from":"2024-01-16"}),
    &[]
);
structured_filter!(
    audit_s05_date_to,
    json!({"query":"Hello", "date_to":"2024-01-14"}),
    &[]
);
structured_filter!(
    audit_s05_explicit_overrides_operator,
    json!({"query":"from:user@example.com Hello", "from":"sender@example.com"}),
    &["em1"]
);
structured_filter!(
    audit_s05_false_overrides_unread,
    json!({"query":"is:unread Hello", "is_unread":false}),
    &["em2"]
);
structured_filter!(
    audit_s05_false_starred,
    json!({"query":"Hello", "is_starred":false}),
    &["em1"]
);
structured_filter!(
    audit_s05_false_attachment,
    json!({"query":"Hello", "has_attachment":false}),
    &["em1"]
);

#[test]
fn audit_s12_in_inbox_without_text() {
    assert_ids(
        search(&fixture(), json!({"query":"in:inbox"})),
        &["em2", "em1"],
    );
}

#[test]
fn audit_s12_in_inbox_with_text() {
    assert_ids(
        search(&fixture(), json!({"query":"in:inbox Hello"})),
        &["em2", "em1"],
    );
}

#[test]
fn audit_s12_is_read() {
    assert_ids(
        search(&fixture(), json!({"query":"is:read Hello"})),
        &["em2"],
    );
}

#[test]
fn audit_s12_is_read_without_text() {
    assert_ids(search(&fixture(), json!({"query":"is:read"})), &["em2"]);
}

fn raw_dates(ctx: &AppCtx) {
    // Offset makes em1 older despite its later local calendar day and lexical weekday.
    let conn = ctx.pool.get().unwrap();
    conn.execute(
        "UPDATE emails SET date='Sun, 06 Sep 2026 00:30:00 +0900' WHERE id='em1'",
        [],
    )
    .unwrap();
    conn.execute(
        "UPDATE emails SET date='Sat, 05 Sep 2026 20:00:00 +0000' WHERE id='em2'",
        [],
    )
    .unwrap();
}

#[test]
fn audit_s11_rfc2822_before() {
    let ctx = fixture();
    raw_dates(&ctx);
    assert_ids(
        search(&ctx, json!({"query":"before:2026-09-06"})),
        &["em2", "em1"],
    );
}

#[test]
fn audit_s11_rfc2822_after_excludes_older() {
    let ctx = fixture();
    raw_dates(&ctx);
    assert_ids(search(&ctx, json!({"query":"after:2026-09-06"})), &[]);
}

#[test]
fn audit_s11_rfc2822_search_order_and_pagination() {
    let ctx = fixture();
    raw_dates(&ctx);
    for (offset, expected) in [(0, "em2"), (1, "em1")] {
        let result = search(&ctx, json!({"query":"Hello", "limit":1, "offset":offset}));
        assert_eq!(result.total_count, 2);
        assert_eq!(result.emails.len(), 1);
        assert_eq!(result.emails[0].id, expected);
    }
}

#[test]
fn audit_s11_rfc2822_thread_order() {
    let ctx = fixture();
    raw_dates(&ctx);
    let emails = list_thread(&ctx, "acc1", "<msg1@example.com>").unwrap();
    assert_eq!(
        emails.iter().map(|e| e.id.as_str()).collect::<Vec<_>>(),
        ["em1", "em2"]
    );
}

#[test]
fn audit_s10_future_snooze_hidden_from_folder_before_pagination() {
    let ctx = fixture();
    ctx.pool
        .get()
        .unwrap()
        .execute(
            "UPDATE emails SET snoozed_until='9999-01-01T00:00:00.000Z' WHERE id='em2'",
            [],
        )
        .unwrap();
    let emails = list_local_emails(&ctx, "acc1", "fold1", 1, 0).unwrap();
    assert_eq!(
        emails.iter().map(|e| e.id.as_str()).collect::<Vec<_>>(),
        ["em1"]
    );
}

#[test]
fn audit_s10_future_snooze_hidden_from_search_and_count() {
    let ctx = fixture();
    ctx.pool
        .get()
        .unwrap()
        .execute(
            "UPDATE emails SET snoozed_until='9999-01-01T00:00:00.000Z' WHERE id='em2'",
            [],
        )
        .unwrap();
    assert_ids(search(&ctx, json!({"query":"Hello", "limit":1})), &["em1"]);
}

#[test]
fn audit_s10_expired_snooze_visible_without_scheduler_tick() {
    let ctx = fixture();
    ctx.pool
        .get()
        .unwrap()
        .execute(
            "UPDATE emails SET snoozed_until='2000-01-01T00:00:00.000Z' WHERE id='em2'",
            [],
        )
        .unwrap();
    assert_eq!(
        list_local_emails(&ctx, "acc1", "fold1", 10, 0)
            .unwrap()
            .len(),
        2
    );
    assert_ids(search(&ctx, json!({"query":"Hello"})), &["em2", "em1"]);
}
