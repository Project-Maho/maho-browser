//! Server-derived funnel/retention metrics (admin-gated, aggregate-only).
//!
//! Built entirely from `users.created_at` and `user_activity_days`; no client
//! ping and no client-generated identifier is involved. Only aggregate counts
//! are returned — never raw user ids or activity rows.

use crate::auth::routes::require_admin_token;
use crate::db;
use crate::errors::Result;
use serde::Serialize;
use worker::*;

#[derive(Serialize)]
struct FunnelMetrics {
    from_day: i64,
    to_day: i64,
    signups: i64,
    activated: i64,
    d1_retained: i64,
    d7_retained: i64,
}

pub async fn funnel(req: Request, env: Env) -> Result<Response> {
    require_admin_token(&req, &env)?;
    let db = env.d1("MAHO_DB")?;
    let today = db::day_index(chrono::Utc::now().timestamp());

    let url = req.url()?;
    let mut from_day = today - 30;
    let mut to_day = today;
    for (k, v) in url.query_pairs() {
        match k.as_ref() {
            "from" => {
                if let Ok(d) = v.parse::<i64>() {
                    from_day = d;
                }
            }
            "to" => {
                if let Ok(d) = v.parse::<i64>() {
                    to_day = d;
                }
            }
            _ => {}
        }
    }

    let signups = scalar_count(
        &db,
        "SELECT COUNT(*) AS c FROM users WHERE (created_at / 86400) BETWEEN ?1 AND ?2",
        from_day,
        to_day,
    )
    .await?;
    let activated = scalar_count(
        &db,
        "SELECT COUNT(*) AS c FROM users u WHERE (u.created_at / 86400) BETWEEN ?1 AND ?2 \
         AND EXISTS (SELECT 1 FROM user_activity_days a \
                     WHERE a.user_id = u.id AND a.day > u.created_at / 86400)",
        from_day,
        to_day,
    )
    .await?;
    let d1_retained = scalar_count(
        &db,
        "SELECT COUNT(*) AS c FROM users u WHERE (u.created_at / 86400) BETWEEN ?1 AND ?2 \
         AND EXISTS (SELECT 1 FROM user_activity_days a \
                     WHERE a.user_id = u.id AND a.day = u.created_at / 86400 + 1)",
        from_day,
        to_day,
    )
    .await?;
    let d7_retained = scalar_count(
        &db,
        "SELECT COUNT(*) AS c FROM users u WHERE (u.created_at / 86400) BETWEEN ?1 AND ?2 \
         AND EXISTS (SELECT 1 FROM user_activity_days a \
                     WHERE a.user_id = u.id AND a.day = u.created_at / 86400 + 7)",
        from_day,
        to_day,
    )
    .await?;

    Ok(Response::from_json(&FunnelMetrics {
        from_day,
        to_day,
        signups,
        activated,
        d1_retained,
        d7_retained,
    })?)
}

async fn scalar_count(db: &D1Database, sql: &str, from_day: i64, to_day: i64) -> Result<i64> {
    #[derive(serde::Deserialize)]
    struct CountRow {
        c: i64,
    }
    let row = db
        .prepare(sql)
        .bind(&[db::js(&from_day)?, db::js(&to_day)?])?
        .first::<CountRow>(None)
        .await?;
    Ok(row.map(|r| r.c).unwrap_or(0))
}
