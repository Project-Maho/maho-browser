// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/passwords/maho_password_store_backend.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho {
namespace passwords {

namespace {

void SecureZeroizeString(std::string& s) {
  if (!s.empty()) {
    std::fill_n(static_cast<volatile char*>(&s[0]), s.size(), 0);
    s.clear();
  }
}

base::Time ParseTimestampField(const base::DictValue& dict, const std::string& key) {
  if (std::optional<double> ts = dict.FindDouble(key)) {
    return base::Time::FromSecondsSinceUnixEpoch(*ts);
  }
  if (std::optional<int> ts_int = dict.FindInt(key)) {
    return base::Time::FromSecondsSinceUnixEpoch(*ts_int);
  }
  if (const std::string* ts_str = dict.FindString(key)) {
    base::Time parsed_time;
    if (base::Time::FromString(ts_str->c_str(), &parsed_time)) {
      return parsed_time;
    }
    double ts = 0;
    if (base::StringToDouble(*ts_str, &ts)) {
      return base::Time::FromSecondsSinceUnixEpoch(ts);
    }
  }
  return base::Time();
}

MahoPasswordForm ParsePasswordFormFromDict(const base::DictValue& dict) {
  MahoPasswordForm form;
  if (const std::string* id = dict.FindString("id")) {
    form.id = *id;
  }
  if (const std::string* realm = dict.FindString("signonRealm")) {
    form.signon_realm = *realm;
  }
  if (const auto* origins = dict.FindList("origins")) {
    for (const auto& origin : *origins) {
      if (origin.is_string()) {
        form.signon_realm = origin.GetString();
        break;
      }
    }
  }
  if (form.signon_realm.empty()) {
    if (const std::string* title = dict.FindString("title")) {
      form.signon_realm = *title;
    }
  }
  if (const std::string* username = dict.FindString("usernameHint")) {
    form.username_value = *username;
  }
  if (const std::string* username = dict.FindString("username")) {
    form.username_value = *username;
  }
  if (const std::string* password = dict.FindString("password")) {
    form.password_value = *password;
  } else if (const std::string* secret = dict.FindString("secret")) {
    form.password_value = *secret;
  }
  if (std::optional<int> revision = dict.FindInt("revision")) {
    form.revision = static_cast<uint64_t>(*revision);
  } else if (std::optional<double> revision_double = dict.FindDouble("revision")) {
    if (*revision_double >= 0) {
      form.revision = static_cast<uint64_t>(*revision_double);
    }
  }
  form.date_created = ParseTimestampField(dict, "createdAt");
  form.date_last_used = ParseTimestampField(dict, "lastUsedAt");
  return form;
}

base::DictValue BuildLoginMetadata(const MahoPasswordForm& form) {
  base::DictValue metadata;
  metadata.Set("title", form.signon_realm);
  metadata.Set("usernameHint", form.username_value);
  metadata.Set("itemKind", "login");

  base::ListValue origins;
  std::string origin_str = form.signon_realm;
  if (!origin_str.starts_with("http://") && !origin_str.starts_with("https://")) {
    origin_str = "https://" + origin_str;
  }
  origins.Append(origin_str);
  metadata.Set("origins", std::move(origins));
  metadata.Set("totp", base::Value());
  metadata.Set("passkey", base::Value());
  return metadata;
}

bool SetExpectedRevision(base::DictValue& request, uint64_t revision) {
  if (revision > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  request.Set("expectedRevision", static_cast<int>(revision));
  return true;
}

bool ReadBackendCredentials(const std::optional<std::string>& origin,
                            std::vector<MahoPasswordForm>& forms) {
  maho::core::VaultBackendSession session = maho::CreateVaultBackendSession();
  if (!session.is_valid()) {
    return false;
  }

  base::DictValue req;
  if (origin && !origin->empty()) {
    req.Set("origin", *origin);
  }
  std::string req_json;
  base::JSONWriter::Write(req, &req_json);
  maho::core::VaultBackendResult result = session.ExecuteCredentials(req_json);
  SecureZeroizeString(req_json);
  if (!result.is_valid() ||
      result.status() != maho::core::VaultBackendResultStatus::kSuccess) {
    return false;
  }

  std::string json = result.ConsumeJson();
  std::optional<base::Value> parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  SecureZeroizeString(json);
  if (!parsed || !parsed->is_list()) {
    return false;
  }

  for (const auto& item : parsed->GetList()) {
    if (item.is_dict()) {
      forms.push_back(ParsePasswordFormFromDict(item.GetDict()));
    }
  }
  return true;
}

std::vector<password_manager::StoredCredential> ConvertToStoredCredentials(
    const std::vector<MahoPasswordForm>& forms) {
  std::vector<password_manager::StoredCredential> results;
  results.reserve(forms.size());
  for (const auto& form : forms) {
    password_manager::StoredCredential cred;
    cred.signon_realm = form.signon_realm;
    cred.username_value = base::UTF8ToUTF16(form.username_value);
    cred.password_value = base::UTF8ToUTF16(form.password_value);
    results.push_back(std::move(cred));
  }
  return results;
}

}  // namespace

MahoPasswordStoreBackend::MahoPasswordStoreBackend(bool enabled)
    : enabled_(enabled) {}

MahoPasswordStoreBackend::~MahoPasswordStoreBackend() = default;

void MahoPasswordStoreBackend::InitBackend(
    password_manager::AffiliatedMatchHelper* affiliated_match_helper,
    RemoteChangesReceived remote_form_changes_received,
    base::RepeatingClosure sync_enabled_or_disabled_cb,
    base::OnceCallback<void(bool)> completion) {
  InitBackend(std::move(completion));
}

void MahoPasswordStoreBackend::InitBackend(base::OnceCallback<void(bool)> completion) {
  initialized_ = true;
  if (completion) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(completion), true));
  }
}

void MahoPasswordStoreBackend::Shutdown(base::OnceClosure done) {
  initialized_ = false;
  weak_ptr_factory_.InvalidateWeakPtrs();
  if (done) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, std::move(done));
  }
}

bool MahoPasswordStoreBackend::IsInitialized() const {
  return initialized_;
}

password_manager::ActionableError MahoPasswordStoreBackend::GetError() {
  return password_manager::ActionableError();
}

void MahoPasswordStoreBackend::OnSyncServiceInitialized(syncer::SyncService* sync_service) {}

base::WeakPtr<password_manager::PasswordStoreBackend> MahoPasswordStoreBackend::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void MahoPasswordStoreBackend::GetAllLoginsAsync(
    password_manager::BackendLoginsOrErrorReply callback) {
  if (!enabled_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  password_manager::BackendLoginsResult()));
    return;
  }
  GetAllLoginsMahoAsync(base::BindOnce(
      [](password_manager::BackendLoginsOrErrorReply cb, std::vector<MahoPasswordForm> forms, bool success) {
        if (!success) {
          std::move(cb).Run(password_manager::PasswordStoreBackendError(
              password_manager::PasswordStoreBackendErrorType::kUncategorized));
          return;
        }
        std::move(cb).Run(ConvertToStoredCredentials(forms));
      },
      std::move(callback)));
}

void MahoPasswordStoreBackend::GetAllLoginsWithAffiliationAndBrandingAsync(
    password_manager::BackendLoginsOrErrorReply callback) {
  GetAllLoginsAsync(std::move(callback));
}

void MahoPasswordStoreBackend::GetAutofillableLoginsAsync(
    password_manager::BackendLoginsOrErrorReply callback) {
  GetAllLoginsAsync(std::move(callback));
}

void MahoPasswordStoreBackend::FillMatchingLoginsAsync(
    password_manager::BackendLoginsOrErrorReply callback,
    bool include_psl,
    const std::vector<password_manager::PasswordFormDigest>& forms) {
  if (!enabled_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  password_manager::BackendLoginsResult()));
    return;
  }
  std::string realm = forms.empty() ? "" : forms[0].signon_realm;
  FillMatchingLoginsMahoAsync(
      realm,
      base::BindOnce(
          [](password_manager::BackendLoginsOrErrorReply cb,
             std::vector<MahoPasswordForm> forms, bool success) {
            if (!success) {
              std::move(cb).Run(password_manager::PasswordStoreBackendError(
                  password_manager::PasswordStoreBackendErrorType::kUncategorized));
              return;
            }
            std::move(cb).Run(ConvertToStoredCredentials(forms));
          },
          std::move(callback)));
}

void MahoPasswordStoreBackend::GetGroupedMatchingLoginsAsync(
    const password_manager::PasswordFormDigest& form_digest,
    password_manager::BackendLoginsOrErrorReply callback) {
  if (!enabled_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  password_manager::BackendLoginsResult()));
    return;
  }
  FillMatchingLoginsMahoAsync(
      form_digest.signon_realm,
      base::BindOnce(
          [](password_manager::BackendLoginsOrErrorReply cb,
             std::vector<MahoPasswordForm> forms, bool success) {
            if (!success) {
              std::move(cb).Run(password_manager::PasswordStoreBackendError(
                  password_manager::PasswordStoreBackendErrorType::kUncategorized));
              return;
            }
            std::move(cb).Run(ConvertToStoredCredentials(forms));
          },
          std::move(callback)));
}

void MahoPasswordStoreBackend::AddLoginAsync(
    password_manager::StoredCredential cred,
    password_manager::PasswordChangesOrErrorReply callback) {
  if (!enabled_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       password_manager::PasswordStoreBackendError(
                           password_manager::PasswordStoreBackendErrorType::kUncategorized)));
    return;
  }
  MahoPasswordForm form;
  form.username_value = base::UTF16ToUTF8(cred.username_value);
  form.password_value = base::UTF16ToUTF8(cred.password_value);
  form.signon_realm = cred.signon_realm;
  AddLoginMahoAsync(form, base::BindOnce(
                               [](password_manager::PasswordChangesOrErrorReply cb, bool success) {
                                 if (!success) {
                                   std::move(cb).Run(password_manager::PasswordStoreBackendError(
                                       password_manager::PasswordStoreBackendErrorType::kUncategorized));
                                   return;
                                 }
                                 std::move(cb).Run(password_manager::PasswordStoreChangeList());
                               },
                               std::move(callback)));
}

void MahoPasswordStoreBackend::UpdateLoginAsync(
    password_manager::StoredCredential cred,
    password_manager::PasswordChangesOrErrorReply callback) {
  if (!enabled_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       password_manager::PasswordStoreBackendError(
                           password_manager::PasswordStoreBackendErrorType::kUncategorized)));
    return;
  }
  MahoPasswordForm target_form;
  target_form.username_value = base::UTF16ToUTF8(cred.username_value);
  target_form.password_value = base::UTF16ToUTF8(cred.password_value);
  target_form.signon_realm = cred.signon_realm;

  FillMatchingLoginsMahoAsync(
      target_form.signon_realm,
      base::BindOnce(
          [](base::WeakPtr<MahoPasswordStoreBackend> backend, MahoPasswordForm form,
             password_manager::PasswordChangesOrErrorReply cb,
             std::vector<MahoPasswordForm> matched_forms, bool success) {
            if (!backend || !success) {
              std::move(cb).Run(password_manager::PasswordStoreBackendError(
                  password_manager::PasswordStoreBackendErrorType::kUncategorized));
              return;
            }
            for (const auto& matched : matched_forms) {
              if (matched.username_value == form.username_value) {
                form.id = matched.id;
                break;
              }
            }
            backend->UpdateLoginMahoAsync(
                form,
                base::BindOnce(
                    [](password_manager::PasswordChangesOrErrorReply reply_cb, bool update_ok) {
                      if (!update_ok) {
                        std::move(reply_cb).Run(password_manager::PasswordStoreBackendError(
                            password_manager::PasswordStoreBackendErrorType::kUncategorized));
                        return;
                      }
                      std::move(reply_cb).Run(password_manager::PasswordStoreChangeList());
                    },
                    std::move(cb)));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(target_form), std::move(callback)));
}

void MahoPasswordStoreBackend::RemoveLoginAsync(
    const base::Location& location,
    password_manager::StoredCredential cred,
    password_manager::PasswordChangesOrErrorReply callback) {
  if (!enabled_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       password_manager::PasswordStoreBackendError(
                           password_manager::PasswordStoreBackendErrorType::kUncategorized)));
    return;
  }
  MahoPasswordForm target_form;
  target_form.username_value = base::UTF16ToUTF8(cred.username_value);
  target_form.password_value = base::UTF16ToUTF8(cred.password_value);
  target_form.signon_realm = cred.signon_realm;

  FillMatchingLoginsMahoAsync(
      target_form.signon_realm,
      base::BindOnce(
          [](base::WeakPtr<MahoPasswordStoreBackend> backend, MahoPasswordForm form,
             password_manager::PasswordChangesOrErrorReply cb,
             std::vector<MahoPasswordForm> matched_forms, bool success) {
            if (!backend || !success) {
              std::move(cb).Run(password_manager::PasswordStoreBackendError(
                  password_manager::PasswordStoreBackendErrorType::kUncategorized));
              return;
            }
            for (const auto& matched : matched_forms) {
              if (matched.username_value == form.username_value) {
                form.id = matched.id;
                break;
              }
            }
            backend->RemoveLoginMahoAsync(
                form,
                base::BindOnce(
                    [](password_manager::PasswordChangesOrErrorReply reply_cb, bool remove_ok) {
                      if (!remove_ok) {
                        std::move(reply_cb).Run(password_manager::PasswordStoreBackendError(
                            password_manager::PasswordStoreBackendErrorType::kUncategorized));
                        return;
                      }
                      std::move(reply_cb).Run(password_manager::PasswordStoreChangeList());
                    },
                    std::move(cb)));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(target_form), std::move(callback)));
}

void MahoPasswordStoreBackend::RemoveLoginsCreatedBetweenAsync(
    const base::Location& location,
    base::Time delete_begin,
    base::Time delete_end,
    base::OnceCallback<void(bool)> sync_completion,
    password_manager::PasswordChangesOrErrorReply callback) {
  if (!enabled_) {
    if (sync_completion) {
      std::move(sync_completion).Run(true);
    }
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  password_manager::PasswordStoreChangeList()));
    return;
  }
  RemoveLoginsCreatedBetweenMahoAsync(
      delete_begin, delete_end,
      base::BindOnce(
          [](base::OnceCallback<void(bool)> sync_cb,
             password_manager::PasswordChangesOrErrorReply cb, bool success) {
            if (sync_cb) {
              std::move(sync_cb).Run(success);
            }
            if (!success) {
              std::move(cb).Run(password_manager::PasswordStoreBackendError(
                  password_manager::PasswordStoreBackendErrorType::kUncategorized));
              return;
            }
            std::move(cb).Run(password_manager::PasswordStoreChangeList());
          },
          std::move(sync_completion), std::move(callback)));
}

void MahoPasswordStoreBackend::DisableAutoSignInForOriginsAsync(
    const base::RepeatingCallback<bool(const GURL&)>& origin_filter,
    base::OnceClosure completion) {
  if (completion) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, std::move(completion));
  }
}

password_manager::SmartBubbleStatsStore* MahoPasswordStoreBackend::GetSmartBubbleStatsStore() {
  return nullptr;
}

std::unique_ptr<syncer::DataTypeControllerDelegate> MahoPasswordStoreBackend::CreateSyncControllerDelegate() {
  return nullptr;
}

// Convenience Maho-specific API implementations:
void MahoPasswordStoreBackend::GetAllLoginsMahoAsync(LoginsOrErrorReply callback) {
  if (!enabled_ || !initialized_ || !callback) {
    if (callback) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), std::vector<MahoPasswordForm>{}, false));
    }
    return;
  }

  std::vector<MahoPasswordForm> results;
  if (!ReadBackendCredentials(std::nullopt, results)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), std::vector<MahoPasswordForm>{}, false));
    return;
  }

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::move(results), true));
}

void MahoPasswordStoreBackend::GetAutofillableLoginsMahoAsync(LoginsOrErrorReply callback) {
  GetAllLoginsMahoAsync(std::move(callback));
}

void MahoPasswordStoreBackend::FillMatchingLoginsMahoAsync(const std::string& signon_realm, LoginsOrErrorReply callback) {
  if (!enabled_ || !initialized_ || !callback) {
    if (callback) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), std::vector<MahoPasswordForm>{}, false));
    }
    return;
  }

  std::vector<MahoPasswordForm> results;
  if (!ReadBackendCredentials(signon_realm, results)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), std::vector<MahoPasswordForm>{}, false));
    return;
  }

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::move(results), true));
}

void MahoPasswordStoreBackend::AddLoginMahoAsync(const MahoPasswordForm& form, PasswordChangesOrErrorReply callback) {
  if (!enabled_ || !initialized_ || !callback) {
    if (callback) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), false));
    }
    return;
  }

  MahoCore* core = maho::GetCore();
  if (!core) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), false));
    return;
  }

  MahoPasswordForm mutable_form = form;
  bool success = false;

  base::DictValue req;
  req.Set("metadata", BuildLoginMetadata(mutable_form));
  req.Set("username", mutable_form.username_value);
  req.Set("password", mutable_form.password_value);

  std::string req_json;
  base::JSONWriter::Write(req, &req_json);
  char* add_res = maho_vault_add_login_json(core, req_json.c_str());
  SecureZeroizeString(req_json);

  if (add_res) {
    std::optional<base::Value> parsed = base::JSONReader::Read(add_res, base::JSON_PARSE_RFC);
    maho_string_free(add_res);
    if (parsed && parsed->is_dict() && parsed->GetDict().FindBool("ok").value_or(false)) {
      success = true;
    }
  }

  mutable_form.Zeroize();
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), success));
}

void MahoPasswordStoreBackend::UpdateLoginMahoAsync(const MahoPasswordForm& form, PasswordChangesOrErrorReply callback) {
  if (!enabled_ || !initialized_ || !callback) {
    if (callback) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), false));
    }
    return;
  }

  MahoCore* core = maho::GetCore();
  if (!core || form.id.empty()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), false));
    return;
  }

  MahoPasswordForm mutable_form = form;
  bool success = false;

  base::DictValue req;
  req.Set("id", mutable_form.id);
  if (!SetExpectedRevision(req, mutable_form.revision)) {
    mutable_form.Zeroize();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), false));
    return;
  }
  req.Set("metadata", BuildLoginMetadata(mutable_form));
  req.Set("username", mutable_form.username_value);
  if (!mutable_form.password_value.empty()) {
    req.Set("password", mutable_form.password_value);
  }
  std::string req_json;
  base::JSONWriter::Write(req, &req_json);
  char* update_res = maho_vault_update_login_json(core, req_json.c_str());
  SecureZeroizeString(req_json);
  if (update_res) {
    std::optional<base::Value> parsed = base::JSONReader::Read(update_res, base::JSON_PARSE_RFC);
    maho_string_free(update_res);
    if (parsed && parsed->is_dict() && parsed->GetDict().FindBool("ok").value_or(false)) {
      success = true;
    }
  }

  mutable_form.Zeroize();
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), success));
}

void MahoPasswordStoreBackend::RemoveLoginMahoAsync(const MahoPasswordForm& form, PasswordChangesOrErrorReply callback) {
  if (!enabled_ || !initialized_ || !callback) {
    if (callback) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), false));
    }
    return;
  }

  MahoCore* core = maho::GetCore();
  if (!core || form.id.empty()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), false));
    return;
  }

  bool success = false;
  base::DictValue req;
  req.Set("id", form.id);
  if (!SetExpectedRevision(req, form.revision)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), false));
    return;
  }
  std::string req_json;
  base::JSONWriter::Write(req, &req_json);
  char* del_res = maho_vault_delete_item_json(core, req_json.c_str());
  SecureZeroizeString(req_json);

  if (del_res) {
    std::optional<base::Value> parsed = base::JSONReader::Read(del_res, base::JSON_PARSE_RFC);
    maho_string_free(del_res);
    if (parsed && parsed->is_dict() && parsed->GetDict().FindBool("ok").value_or(false)) {
      success = true;
    }
  }

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), success));
}

void MahoPasswordStoreBackend::RemoveLoginsCreatedBetweenMahoAsync(base::Time delete_begin,
                                                                    base::Time delete_end,
                                                                    PasswordChangesOrErrorReply callback) {
  if (!enabled_ || !initialized_ || !callback) {
    if (callback) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), false));
    }
    return;
  }

  GetAllLoginsMahoAsync(base::BindOnce(
      [](base::WeakPtr<MahoPasswordStoreBackend> backend, base::Time delete_begin,
         base::Time delete_end, PasswordChangesOrErrorReply cb,
         std::vector<MahoPasswordForm> forms, bool success) {
        if (!backend || !success) {
          std::move(cb).Run(false);
          return;
        }

        bool all_removed = true;
        for (const auto& form : forms) {
          if ((delete_begin.is_null() || form.date_created >= delete_begin) &&
              (delete_end.is_null() || form.date_created <= delete_end)) {
            if (!form.id.empty()) {
              MahoCore* core = maho::GetCore();
              if (core) {
                base::DictValue req;
                req.Set("id", form.id);
                if (!SetExpectedRevision(req, form.revision)) {
                  all_removed = false;
                  continue;
                }
                std::string req_json;
                base::JSONWriter::Write(req, &req_json);
                char* del_res = maho_vault_delete_item_json(core, req_json.c_str());
                SecureZeroizeString(req_json);
                if (del_res) {
                  std::optional<base::Value> parsed = base::JSONReader::Read(del_res, base::JSON_PARSE_RFC);
                  maho_string_free(del_res);
                  if (!parsed || !parsed->is_dict() || !parsed->GetDict().FindBool("ok").value_or(false)) {
                    all_removed = false;
                  }
                } else {
                  all_removed = false;
                }
              } else {
                all_removed = false;
              }
            }
          }
        }
        std::move(cb).Run(all_removed);
      },
      weak_ptr_factory_.GetWeakPtr(), delete_begin, delete_end, std::move(callback)));
}

}  // namespace passwords
}  // namespace maho
