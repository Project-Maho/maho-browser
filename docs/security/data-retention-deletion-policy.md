# Data Retention & Deletion Policy

## 1. Purpose & Scope

This policy governs the retention periods, automated cleanup, and secure deletion mechanisms of all data collected, cached, or processed by the Maho Browser Agent subsystem under SOC2 Confidentiality Criterion **C1.2**.

---

## 2. Retention Schedule

| Data Category | Classification | Maximum Retention Period | Disposal Method |
|---|---|---|---|
| **BYOK Keys** | Level 3 (Restricted) | Indefinite (until user deletion) | Memory zeroization + Secure Ring delete |
| **Chat Conversations** | Level 2 (Private) | 30 days (default) | Cascade database purge |
| **Agent Workspace Files** | Level 2 (Private) | Active session lifecycle only | Recursive directory wipe on session exit |
| **Audit Logs** | Level 2 (Private) | 365 days | Rollover and cloud-backup archiving |

---

## 3. Enforcement Mechanisms

1. **Automated Session Garbage Collection:**
   - The native shell and Chromium adapter monitor active agent sessions. When `cancel` is called or a session completes:
     - The temporary workspace folders associated with the session are wiped from disk.
     - The database connection triggers a cascading database purge.
2. **Scheduled Database Pruning:**
   - A daily background job running on the native shell prunes all conversation entries older than 30 days.

---

## 4. User-Initiated Deletion Procedures & SLA

Users can request immediate deletion of their data at any time. The following procedures must be supported:

1. **BYOK Key Removal:**
   - **Procedure:** User navigates to `chrome://maho-settings/ai` and clears the API Key field or clicks "Clear Key".
   - **Technical Flow:** This invokes Mojo `ClearBYOKKey(provider)` which sets the encrypted preference to empty and invokes memory zeroization on the running agent session.
   - **SLA:** Key deletion takes effect in memory and on disk within **1 second** of request.

2. **Chat History Deletion:**
   - **Procedure:** User clicks "Delete Session" or "Clear History" in the chat interface.
   - **Technical Flow:** Calls local FFI binding `maho_core_delete_session`. The Sqlite storage runs `DELETE FROM conversation_turns WHERE session_id = ?`.
   - **SLA:** Database purge complete and locked within **2 seconds** of request.

---

## 5. Audit Trail for Deletion Events

To demonstrate compliance to auditors, all user-initiated or automated deletion events must write a non-repudiable audit log to `maho_agent::audit`:

- **Example Log Payload (BYOK Key Cleared):**
  ```json
  {
    "target": "maho_agent::audit",
    "event": "byok_key_deleted",
    "provider": "openai",
    "timestamp": "2026-06-23T15:16:16Z"
  }
  ```

- **Example Log Payload (Session History Pruned):**
  ```json
  {
    "target": "maho_agent::audit",
    "event": "session_history_purged",
    "session_id": "unified_agent_session_123",
    "timestamp": "2026-06-23T15:16:17Z"
  }
  ```
Auditors will reconcile these logs against database schemas to verify that expired or user-deleted items are fully removed.
