# Copyright 2026 Maho Browser. All rights reserved.
import sys

target_file = "/Users/indo/code/project/maho-workspace/maho-chromium/browser/mail_helper/maho_mail_helper_main.cc"

with open(target_file, "r") as f:
    content = f.read()

# Define the helper functions and CallBackend C++ code
cb_impl = """
  void CallBackend(const std::string& command, const std::string& args_json, CallBackendCallback callback) override {
    std::optional<base::Value> parsed = base::JSONReader::Read(args_json);
    const base::Value::Dict* dict = parsed ? parsed->GetIfDict() : nullptr;

    auto get_str = [dict](const std::string& key) -> std::string {
      if (!dict) return "";
      const std::string* val = dict->FindString(key);
      return val ? *val : "";
    };

    auto get_int = [dict](const std::string& key, int default_val) -> int {
      if (!dict) return default_val;
      std::optional<int> val = dict->FindInt(key);
      return val ? *val : default_val;
    };

    auto get_bool = [dict](const std::string& key, bool default_val) -> bool {
      if (!dict) return default_val;
      std::optional<bool> val = dict->FindBool(key);
      return val ? *val : default_val;
    };

    auto get_json = [dict](const std::string& key) -> std::string {
      if (!dict) return "[]";
      const base::Value* val = dict->Find(key);
      if (!val) return "[]";
      if (val->is_string()) return val->GetString();
      std::string json;
      base::JSONWriter::Write(*val, &json);
      return json;
    };

    // --- Wave D: SMTP / Compose / Outbox ---
    if (command == "SendEmail") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailSendEmail(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SaveDraft") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailSaveDraft(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "UpdateDraft") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string did, std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailUpdateDraft(did.c_str(), req.c_str(), cb, ud);
          }, get_str("draft_id"), args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "GetReplyContext") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string eid, MahoMailReadCallback cb, void* ud) {
            return MahoMailGetReplyContext(eid.c_str(), cb, ud);
          }, get_str("email_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "QueueEmail") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailQueueEmail(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListOutbox") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailListOutbox(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "RetryOutboxItem") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string iid, MahoMailReadCallback cb, void* ud) {
            return MahoMailRetryOutboxItem(iid.c_str(), cb, ud);
          }, get_str("item_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DeleteOutboxItem") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string iid, MahoMailReadCallback cb, void* ud) {
            return MahoMailDeleteOutboxItem(iid.c_str(), cb, ud);
          }, get_str("item_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "FlushOutbox") {
      MahoMailReadBridge::Start(
          base::BindOnce([](MahoMailReadCallback cb, void* ud) {
            return MahoMailFlushOutbox(cb, ud);
          }),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DownloadAttachment") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, int64_t uid, std::string fid, std::string pid, std::string name, MahoMailReadCallback cb, void* ud) {
            return MahoMailDownloadAttachment(acc.c_str(), uid, fid.c_str(), pid.c_str(), name.c_str(), cb, ud);
          }, get_str("account_id"), get_int("email_uid", 0), get_str("folder_id"), get_str("part_id"), get_str("filename")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }

    // --- Wave E: Snooze / Reminders / Rules / Labels ---
    if (command == "SnoozeEmail") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string eid, std::string until, MahoMailReadCallback cb, void* ud) {
            return MahoMailSnoozeEmail(eid.c_str(), until.c_str(), cb, ud);
          }, get_str("email_id"), get_str("snooze_until")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "UnsnoozeEmail") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string eid, MahoMailReadCallback cb, void* ud) {
            return MahoMailUnsnoozeEmail(eid.c_str(), cb, ud);
          }, get_str("email_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListSnoozedEmails") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailListSnoozedEmails(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "PinEmail") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string eid, MahoMailReadCallback cb, void* ud) {
            return MahoMailPinEmail(eid.c_str(), cb, ud);
          }, get_str("email_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "UnpinEmail") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string eid, MahoMailReadCallback cb, void* ud) {
            return MahoMailUnpinEmail(eid.c_str(), cb, ud);
          }, get_str("email_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListPinnedEmails") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailListPinnedEmails(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SetReminder") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailSetReminder(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ClearReminder") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string eid, MahoMailReadCallback cb, void* ud) {
            return MahoMailClearReminder(eid.c_str(), cb, ud);
          }, get_str("email_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListReminders") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailListReminders(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "MuteThread") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string mid, MahoMailReadCallback cb, void* ud) {
            return MahoMailMuteThread(acc.c_str(), mid.c_str(), cb, ud);
          }, get_str("account_id"), get_str("message_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "UnmuteThread") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string mid, MahoMailReadCallback cb, void* ud) {
            return MahoMailUnmuteThread(acc.c_str(), mid.c_str(), cb, ud);
          }, get_str("account_id"), get_str("message_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "IsThreadMuted") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string mid, MahoMailReadCallback cb, void* ud) {
            return MahoMailIsThreadMuted(acc.c_str(), mid.c_str(), cb, ud);
          }, get_str("account_id"), get_str("message_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListMutedThreads") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailListMutedThreads(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "FilterMutedMessageIds") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string ids, MahoMailReadCallback cb, void* ud) {
            return MahoMailFilterMutedMessageIds(acc.c_str(), ids.c_str(), cb, ud);
          }, get_str("account_id"), get_json("message_ids")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "CreateMailRule") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailCreateMailRule(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "UpdateMailRule") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailUpdateMailRule(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DeleteMailRule") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string rid, MahoMailReadCallback cb, void* ud) {
            return MahoMailDeleteMailRule(rid.c_str(), cb, ud);
          }, get_str("rule_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListMailRules") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailListMailRules(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ReorderMailRules") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string ids, MahoMailReadCallback cb, void* ud) {
            return MahoMailReorderMailRules(acc.c_str(), ids.c_str(), cb, ud);
          }, get_str("account_id"), get_json("rule_ids")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListLabels") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailListLabels(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "CreateLabel") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailCreateLabel(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DeleteLabel") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string lid, MahoMailReadCallback cb, void* ud) {
            return MahoMailDeleteLabel(lid.c_str(), cb, ud);
          }, get_str("label_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "AddLabelToEmail") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string eid, std::string lid, MahoMailReadCallback cb, void* ud) {
            return MahoMailAddLabelToEmail(eid.c_str(), lid.c_str(), cb, ud);
          }, get_str("email_id"), get_str("label_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "RemoveLabelFromEmail") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string eid, std::string lid, MahoMailReadCallback cb, void* ud) {
            return MahoMailRemoveLabelFromEmail(eid.c_str(), lid.c_str(), cb, ud);
          }, get_str("email_id"), get_str("label_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListEmailLabels") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string eid, MahoMailReadCallback cb, void* ud) {
            return MahoMailListEmailLabels(eid.c_str(), cb, ud);
          }, get_str("email_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SaveSearch") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailSaveSearch(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListSavedSearches") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailListSavedSearches(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DeleteSavedSearch") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string sid, MahoMailReadCallback cb, void* ud) {
            return MahoMailDeleteSavedSearch(sid.c_str(), cb, ud);
          }, get_str("search_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ScheduleSend") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailScheduleSend(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "CancelScheduledSend") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string iid, MahoMailReadCallback cb, void* ud) {
            return MahoMailCancelScheduledSend(iid.c_str(), cb, ud);
          }, get_str("item_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListScheduledSends") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailListScheduledSends(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "StartScheduler") {
      MahoMailReadBridge::Start(
          base::BindOnce([](MahoMailReadCallback cb, void* ud) {
            return MahoMailStartScheduler(cb, ud);
          }),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "StopScheduler") {
      MahoMailReadBridge::Start(
          base::BindOnce([](MahoMailReadCallback cb, void* ud) {
            return MahoMailStopScheduler(cb, ud);
          }),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }

    // --- Wave F: Contacts / Signatures / Templates ---
    if (command == "SearchContacts") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string query, int limit, MahoMailReadCallback cb, void* ud) {
            return MahoMailSearchContacts(acc.c_str(), query.c_str(), limit, cb, ud);
          }, get_str("account_id"), get_str("query"), get_int("limit", 20)),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ToggleVip") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string cid, MahoMailReadCallback cb, void* ud) {
            return MahoMailToggleVip(cid.c_str(), cb, ud);
          }, get_str("contact_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListVipContacts") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailListVipContacts(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListContactGroups") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailListContactGroups(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SearchContactGroups") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string query, MahoMailReadCallback cb, void* ud) {
            return MahoMailSearchContactGroups(acc.c_str(), query.c_str(), cb, ud);
          }, get_str("account_id"), get_str("query")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "CreateContactGroup") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string name, std::string members, MahoMailReadCallback cb, void* ud) {
            return MahoMailCreateContactGroup(acc.c_str(), name.c_str(), members.c_str(), cb, ud);
          }, get_str("account_id"), get_str("name"), get_json("members")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "UpdateContactGroup") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string gid, std::string name, std::string members, MahoMailReadCallback cb, void* ud) {
            return MahoMailUpdateContactGroup(gid.c_str(), name.c_str(), members.c_str(), cb, ud);
          }, get_str("group_id"), get_str("name"), get_json("members")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DeleteContactGroup") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string gid, MahoMailReadCallback cb, void* ud) {
            return MahoMailDeleteContactGroup(gid.c_str(), cb, ud);
          }, get_str("group_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListSignatures") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailListSignatures(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "CreateSignature") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailCreateSignature(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "UpdateSignature") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string sid, std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailUpdateSignature(sid.c_str(), req.c_str(), cb, ud);
          }, get_str("id"), args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DeleteSignature") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string sid, MahoMailReadCallback cb, void* ud) {
            return MahoMailDeleteSignature(sid.c_str(), cb, ud);
          }, get_str("id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListTemplates") {
      MahoMailReadBridge::Start(
          base::BindOnce([](MahoMailReadCallback cb, void* ud) {
            return MahoMailListTemplates(cb, ud);
          }),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "CreateTemplate") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailCreateTemplate(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "UpdateTemplate") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string tid, std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailUpdateTemplate(tid.c_str(), req.c_str(), cb, ud);
          }, get_str("id"), args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DeleteTemplate") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string tid, MahoMailReadCallback cb, void* ud) {
            return MahoMailDeleteTemplate(tid.c_str(), cb, ud);
          }, get_str("id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }

    // --- Wave G: AI / Translation ---
    if (command == "GetEmailSummary") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailGetEmailSummary(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "GetReplyDraft") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailGetReplyDraft(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "AdjustTone") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailAdjustTone(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ClassifyEmail") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailClassifyEmail(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "NaturalLanguageSearch") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailNaturalLanguageSearch(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "GetAiActionHistory") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, int limit, MahoMailReadCallback cb, void* ud) {
            return MahoMailGetAiActionHistory(acc.c_str(), limit, cb, ud);
          }, get_str("account_id"), get_int("limit", 20)),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SaveAiConfig") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailSaveAiConfig(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "GetAiConfig") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string feat, MahoMailReadCallback cb, void* ud) {
            return MahoMailGetAiConfig(feat.c_str(), cb, ud);
          }, get_str("feature")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DeleteAiConfig") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string feat, MahoMailReadCallback cb, void* ud) {
            return MahoMailDeleteAiConfig(feat.c_str(), cb, ud);
          }, get_str("feature")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "TestAiConnection") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailTestAiConnection(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListAgentSessions") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailListAgentSessions(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "GetAgentMessages") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string sid, MahoMailReadCallback cb, void* ud) {
            return MahoMailGetAgentMessages(acc.c_str(), sid.c_str(), cb, ud);
          }, get_str("account_id"), get_str("session_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "CreateAgentSession") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailCreateAgentSession(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DeleteAgentSession") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string sid, MahoMailReadCallback cb, void* ud) {
            return MahoMailDeleteAgentSession(acc.c_str(), sid.c_str(), cb, ud);
          }, get_str("account_id"), get_str("session_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SendAgentMessage") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string sid, std::string content, MahoMailReadCallback cb, void* ud) {
            return MahoMailSendAgentMessage(acc.c_str(), sid.c_str(), content.c_str(), cb, ud);
          }, get_str("account_id"), get_str("session_id"), get_str("content")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "CancelAgentMessage") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string sid, MahoMailReadCallback cb, void* ud) {
            return MahoMailCancelAgentMessage(acc.c_str(), sid.c_str(), cb, ud);
          }, get_str("account_id"), get_str("session_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "GetAutoDraftForEmail") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string eid, MahoMailReadCallback cb, void* ud) {
            return MahoMailGetAutoDraftForEmail(eid.c_str(), cb, ud);
          }, get_str("email_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "UpdateAutoDraftStatus") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string did, std::string status, MahoMailReadCallback cb, void* ud) {
            return MahoMailUpdateAutoDraftStatus(did.c_str(), status.c_str(), cb, ud);
          }, get_str("draft_id"), get_str("status")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "TranslateText") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailTranslateText(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }

    // --- Wave H: Cryptography (PGP / S-MIME) ---
    if (command == "GeneratePgpKey") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string email, std::string name, std::string ktype, MahoMailReadCallback cb, void* ud) {
            return MahoMailGeneratePgpKey(acc.c_str(), email.c_str(), name.c_str(), ktype.c_str(), cb, ud);
          }, get_str("account_id"), get_str("email"), get_str("name"), get_str("key_type")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ImportPgpKey") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string key, MahoMailReadCallback cb, void* ud) {
            return MahoMailImportPgpKey(acc.c_str(), key.c_str(), cb, ud);
          }, get_str("account_id"), get_str("key_data")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ExportPgpKey") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string kid, bool priv, MahoMailReadCallback cb, void* ud) {
            return MahoMailExportPgpKey(kid.c_str(), priv, cb, ud);
          }, get_str("key_id"), get_bool("include_private", false)),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListPgpKeys") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailListPgpKeys(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DeletePgpKey") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string kid, MahoMailReadCallback cb, void* ud) {
            return MahoMailDeletePgpKey(kid.c_str(), cb, ud);
          }, get_str("key_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SetDefaultPgpKey") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string kid, MahoMailReadCallback cb, void* ud) {
            return MahoMailSetDefaultPgpKey(acc.c_str(), kid.c_str(), cb, ud);
          }, get_str("account_id"), get_str("key_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "EncryptEmailPgp") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string recs, std::string plain, MahoMailReadCallback cb, void* ud) {
            return MahoMailEncryptEmailPgp(acc.c_str(), recs.c_str(), plain.c_str(), cb, ud);
          }, get_str("account_id"), get_json("recipient_emails"), get_str("plaintext")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "EncryptAttachmentPgp") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string recs, std::string bytes, MahoMailReadCallback cb, void* ud) {
            return MahoMailEncryptAttachmentPgp(acc.c_str(), recs.c_str(), bytes.c_str(), cb, ud);
          }, get_str("account_id"), get_json("recipient_emails"), get_json("plaintext")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DecryptEmailPgp") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string cipher, MahoMailReadCallback cb, void* ud) {
            return MahoMailDecryptEmailPgp(acc.c_str(), cipher.c_str(), cb, ud);
          }, get_str("account_id"), get_str("ciphertext")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SignEmailPgp") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string msg, MahoMailReadCallback cb, void* ud) {
            return MahoMailSignEmailPgp(acc.c_str(), msg.c_str(), cb, ud);
          }, get_str("account_id"), get_str("message")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "VerifyEmailPgp") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string sender, std::string msg, std::string sig, MahoMailReadCallback cb, void* ud) {
            return MahoMailVerifyEmailPgp(sender.c_str(), msg.c_str(), sig.c_str(), cb, ud);
          }, get_str("sender_email"), get_str("message"), get_str("signature")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ImportSmimeIdentity") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string p12, std::string pass, MahoMailReadCallback cb, void* ud) {
            return MahoMailImportSmimeIdentity(acc.c_str(), p12.c_str(), pass.c_str(), cb, ud);
          }, get_str("account_id"), get_json("p12_data"), get_str("password")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListSmimeIdentities") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailListSmimeIdentities(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DeleteSmimeIdentity") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string iid, MahoMailReadCallback cb, void* ud) {
            return MahoMailDeleteSmimeIdentity(iid.c_str(), cb, ud);
          }, get_str("id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SetDefaultSmimeIdentity") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string iid, MahoMailReadCallback cb, void* ud) {
            return MahoMailSetDefaultSmimeIdentity(acc.c_str(), iid.c_str(), cb, ud);
          }, get_str("account_id"), get_str("identity_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ExportSmimeCert") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string iid, MahoMailReadCallback cb, void* ud) {
            return MahoMailExportSmimeCert(iid.c_str(), cb, ud);
          }, get_str("id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SignEmailSmime") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string body, MahoMailReadCallback cb, void* ud) {
            return MahoMailSignEmailSmime(acc.c_str(), body.c_str(), cb, ud);
          }, get_str("account_id"), get_str("body")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "EncryptEmailSmime") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string certs, std::string body, MahoMailReadCallback cb, void* ud) {
            return MahoMailEncryptEmailSmime(acc.c_str(), certs.c_str(), body.c_str(), cb, ud);
          }, get_str("account_id"), get_json("recipient_certs_pem"), get_str("body")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DecryptEmailSmime") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string cipher, MahoMailReadCallback cb, void* ud) {
            return MahoMailDecryptEmailSmime(acc.c_str(), cipher.c_str(), cb, ud);
          }, get_str("account_id"), get_str("encrypted_body")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "VerifyEmailSmime") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string msg, MahoMailReadCallback cb, void* ud) {
            return MahoMailVerifyEmailSmime(msg.c_str(), cb, ud);
          }, get_str("signed_body")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "CleanupSmimeForAccount") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailCleanupSmimeForAccount(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }

    // --- Wave I: Calendar ---
    if (command == "ListCalendarEvents") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string from, std::string to, MahoMailReadCallback cb, void* ud) {
            return MahoMailListCalendarEvents(acc.c_str(), from.c_str(), to.c_str(), cb, ud);
          }, get_str("account_id"), get_str("from_date"), get_str("to_date")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SearchCalendarEvents") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string query, int limit, MahoMailReadCallback cb, void* ud) {
            return MahoMailSearchCalendarEvents(acc.c_str(), query.c_str(), limit, cb, ud);
          }, get_str("account_id"), get_str("query"), get_int("limit", 50)),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "GetCalendarEvent") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string eid, MahoMailReadCallback cb, void* ud) {
            return MahoMailGetCalendarEvent(eid.c_str(), cb, ud);
          }, get_str("event_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "CreateCalendarEvent") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailCreateCalendarEvent(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "UpdateCalendarEvent") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailUpdateCalendarEvent(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DeleteCalendarEvent") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string eid, std::string scope, MahoMailReadCallback cb, void* ud) {
            return MahoMailDeleteCalendarEvent(eid.c_str(), scope.c_str(), cb, ud);
          }, get_str("event_id"), get_str("delete_scope")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "UpdateRsvp") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string eid, std::string status, MahoMailReadCallback cb, void* ud) {
            return MahoMailUpdateRsvp(eid.c_str(), status.c_str(), cb, ud);
          }, get_str("event_id"), get_str("rsvp_status")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "GenerateRsvpReply") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string eid, std::string status, std::string email, MahoMailReadCallback cb, void* ud) {
            return MahoMailGenerateRsvpReply(eid.c_str(), status.c_str(), email.c_str(), cb, ud);
          }, get_str("event_id"), get_str("rsvp_status"), get_str("account_email")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "AutoImportCalendarEvents") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string eid, std::string html, std::string text, MahoMailReadCallback cb, void* ud) {
            return MahoMailAutoImportCalendarEvents(acc.c_str(), eid.c_str(), html.c_str(), text.c_str(), cb, ud);
          }, get_str("account_id"), get_str("email_id"), get_str("body_html"), get_str("body_text")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SyncGoogleCalendar") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailSyncGoogleCalendar(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "GoogleCalendarMoveEvent") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string cid, std::string eid, std::string dcid, MahoMailReadCallback cb, void* ud) {
            return MahoMailGoogleCalendarMoveEvent(acc.c_str(), cid.c_str(), eid.c_str(), dcid.c_str(), cb, ud);
          }, get_str("account_id"), get_str("calendar_id"), get_str("event_id"), get_str("destination_calendar_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }

    // --- Wave J: Import/Export/Settings ---
    if (command == "ImportEml") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string path, std::string acc, std::string fid, MahoMailReadCallback cb, void* ud) {
            return MahoMailImportEml(path.c_str(), acc.c_str(), fid.c_str(), cb, ud);
          }, get_str("file_path"), get_str("account_id"), get_str("folder_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ImportMbox") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string path, std::string acc, std::string fid, MahoMailReadCallback cb, void* ud) {
            return MahoMailImportMbox(path.c_str(), acc.c_str(), fid.c_str(), cb, ud);
          }, get_str("file_path"), get_str("account_id"), get_str("folder_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ExportEmailsEml") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string ids, std::string dir, MahoMailReadCallback cb, void* ud) {
            return MahoMailExportEmailsEml(ids.c_str(), dir.c_str(), cb, ud);
          }, get_json("email_ids"), get_str("output_dir")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ExportEmailsMbox") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string ids, std::string path, MahoMailReadCallback cb, void* ud) {
            return MahoMailExportEmailsMbox(ids.c_str(), path.c_str(), cb, ud);
          }, get_json("email_ids"), get_str("output_path")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ExportFolderMbox") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string fid, std::string path, MahoMailReadCallback cb, void* ud) {
            return MahoMailExportFolderMbox(acc.c_str(), fid.c_str(), path.c_str(), cb, ud);
          }, get_str("account_id"), get_str("folder_id"), get_str("output_path")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "GetAppSetting") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string key, MahoMailReadCallback cb, void* ud) {
            return MahoMailGetAppSetting(key.c_str(), cb, ud);
          }, get_str("key")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SetAppSetting") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string key, std::string val, MahoMailReadCallback cb, void* ud) {
            return MahoMailSetAppSetting(key.c_str(), val.c_str(), cb, ud);
          }, get_str("key"), get_str("value")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "Md5Hash") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string input, MahoMailReadCallback cb, void* ud) {
            return MahoMailMd5Hash(input.c_str(), cb, ud);
          }, get_str("input")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }

    // Default fallback
    std::move(callback).Run(false, "{\\"error\\": \\"Unknown command or not implemented\\"}");
  }
"""

# Insert cb_impl before "void BindClient("
bind_client_str = "  void BindClient(mojo::PendingRemote<mojom::MahoMailHelperClient> client) override {"
if bind_client_str in content:
    new_content = content.replace(bind_client_str, cb_impl + "\n" + bind_client_str)
    with open(target_file, "w") as f:
        f.write(new_content)
    print("CallBackend successfully applied to maho_mail_helper_main.cc!")
else:
    print("Error: Could not find BindClient in maho_mail_helper_main.cc")
    sys.exit(1)
