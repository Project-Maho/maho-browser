// Copyright 2026 Maho Browser. All rights reserved.

//! Delete must move to the account's Trash folder, and only expunge once the
//! message is already there. `uid = 0` clears the remote identity so
//! `mutate_location` performs its local step without an IMAP server.

use super::*;

const TRASH_ID: &str = "fold-trash";

fn seed_trash_folder(conn: &rusqlite::Connection) {
    conn.execute(
        "INSERT INTO folders (id, account_id, name, path, folder_type)
         VALUES (?1, 'acc1', 'Trash', '[Gmail]/Trash', 'trash')",
        [TRASH_ID],
    )
    .expect("seed trash folder");
}

fn clear_remote_identity(conn: &rusqlite::Connection, email_id: &str) {
    conn.execute("UPDATE emails SET uid = 0 WHERE id = ?1", [email_id])
        .expect("clear uid");
}

fn folder_of(conn: &rusqlite::Connection, email_id: &str) -> Option<String> {
    conn.query_row("SELECT folder_id FROM emails WHERE id = ?1", [email_id], |r| {
        r.get(0)
    })
    .optional()
    .expect("folder lookup")
}

#[test]
fn delete_outside_trash_moves_to_the_trash_folder() {
    let pool = crate::test_support::pool_with_seeded_data();
    let ctx = crate::test_support::ctx(pool);
    let conn = ctx.pool.get().unwrap();
    seed_trash_folder(&conn);
    clear_remote_identity(&conn, "em1");

    mutate_location(&ctx, &conn, "em1", None).expect("delete should succeed");

    assert_eq!(
        folder_of(&conn, "em1").as_deref(),
        Some(TRASH_ID),
        "Delete from the Inbox must move the message into Trash, not expunge it"
    );
}

#[test]
fn delete_inside_trash_expunges_permanently() {
    let pool = crate::test_support::pool_with_seeded_data();
    let ctx = crate::test_support::ctx(pool);
    let conn = ctx.pool.get().unwrap();
    seed_trash_folder(&conn);

    maho_core::services::email::move_email(&conn, "em1", TRASH_ID).expect("move into trash");
    clear_remote_identity(&conn, "em1");

    mutate_location(&ctx, &conn, "em1", None).expect("delete should succeed");

    assert_eq!(
        folder_of(&conn, "em1"),
        None,
        "Delete inside Trash must remove the message for good"
    );
}

#[test]
fn delete_without_a_trash_folder_still_expunges() {
    let pool = crate::test_support::pool_with_seeded_data();
    let ctx = crate::test_support::ctx(pool);
    let conn = ctx.pool.get().unwrap();
    clear_remote_identity(&conn, "em1");

    mutate_location(&ctx, &conn, "em1", None).expect("delete should succeed");

    assert_eq!(
        folder_of(&conn, "em1"),
        None,
        "With no Trash folder the previous expunge behaviour must be preserved"
    );
}

#[test]
fn explicit_target_ignores_trash_resolution() {
    let pool = crate::test_support::pool_with_seeded_data();
    let ctx = crate::test_support::ctx(pool);
    let conn = ctx.pool.get().unwrap();
    seed_trash_folder(&conn);
    clear_remote_identity(&conn, "em1");

    mutate_location(&ctx, &conn, "em1", Some("fold2")).expect("move should succeed");

    assert_eq!(
        folder_of(&conn, "em1").as_deref(),
        Some("fold2"),
        "An explicit target folder must win over Trash resolution"
    );
}

#[test]
fn trash_folder_for_skips_the_message_own_folder() {
    let pool = crate::test_support::pool_with_seeded_data();
    let conn = pool.get().unwrap();
    seed_trash_folder(&conn);
    maho_core::services::email::move_email(&conn, "em1", TRASH_ID).expect("move into trash");

    let resolved = trash_folder_for(&conn, "em1").expect("trash lookup");

    assert_eq!(
        resolved, None,
        "A message already in Trash must not be re-targeted at Trash"
    );
}

#[test]
fn trash_folder_for_is_scoped_to_the_message_account() {
    let pool = crate::test_support::pool_with_seeded_data();
    let conn = pool.get().unwrap();
    conn.execute(
        "INSERT INTO folders (id, account_id, name, path, folder_type)
         VALUES ('other-trash', 'acc2', 'Trash', 'Trash', 'trash')",
        [],
    )
    .unwrap();

    let resolved = trash_folder_for(&conn, "em1").expect("trash lookup");

    assert_eq!(
        resolved, None,
        "Another account's Trash must never be used as the target"
    );
}
