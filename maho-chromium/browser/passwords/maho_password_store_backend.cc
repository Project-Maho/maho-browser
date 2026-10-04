// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/passwords/maho_password_store_backend.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "base/barrier_callback.h"
#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/pickle.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "components/autofill/core/common/form_data.h"
#include "components/autofill/core/common/unique_ids.h"
#if MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL
#include "components/password_manager/core/browser/password_store/password_form_converters.h"
#endif
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/passwords/maho_password_authorization_service.h"
#include "maho/browser/passwords/maho_password_provider_utils.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"

namespace maho {
namespace passwords {

MahoPasswordForm::MahoPasswordForm() = default;

MahoPasswordForm::~MahoPasswordForm() { Zeroize(); }

MahoPasswordForm::MahoPasswordForm(const MahoPasswordForm &) = default;

MahoPasswordForm &MahoPasswordForm::operator=(const MahoPasswordForm &other) {
  if (this != &other) {
    Zeroize();
    id = other.id;
    revision = other.revision;
    scheme = other.scheme;
    signon_realm = other.signon_realm;
    url = other.url;
    action = other.action;
    federation_origin = other.federation_origin;
    submit_element = other.submit_element;
    username_element = other.username_element;
    password_element = other.password_element;
    username_value = other.username_value;
    password_value = other.password_value;
    all_alternative_usernames = other.all_alternative_usernames;
    date_created = other.date_created;
    date_last_used = other.date_last_used;
    date_last_filled = other.date_last_filled;
    date_password_modified = other.date_password_modified;
    date_received = other.date_received;
    blocked_by_user = other.blocked_by_user;
    type = other.type;
    times_used_in_html_form = other.times_used_in_html_form;
    display_name = other.display_name;
    icon_url = other.icon_url;
    match_type = other.match_type;
    skip_zero_click = other.skip_zero_click;
    in_store = other.in_store;
    form_data_base64 = other.form_data_base64;
    notes = other.notes;
  }
  return *this;
}

MahoPasswordForm::MahoPasswordForm(MahoPasswordForm &&other) noexcept
    : id(std::move(other.id)), revision(other.revision), scheme(other.scheme),
      signon_realm(std::move(other.signon_realm)), url(std::move(other.url)),
      action(std::move(other.action)),
      federation_origin(std::move(other.federation_origin)),
      submit_element(std::move(other.submit_element)),
      username_element(std::move(other.username_element)),
      password_element(std::move(other.password_element)),
      username_value(std::move(other.username_value)),
      password_value(std::move(other.password_value)),
      all_alternative_usernames(std::move(other.all_alternative_usernames)),
      date_created(other.date_created), date_last_used(other.date_last_used),
      date_last_filled(other.date_last_filled),
      date_password_modified(other.date_password_modified),
      date_received(other.date_received),
      blocked_by_user(other.blocked_by_user), type(other.type),
      times_used_in_html_form(other.times_used_in_html_form),
      display_name(std::move(other.display_name)),
      icon_url(std::move(other.icon_url)), match_type(other.match_type),
      skip_zero_click(other.skip_zero_click), in_store(other.in_store),
      form_data_base64(std::move(other.form_data_base64)),
      notes(std::move(other.notes)) {}

MahoPasswordForm &
MahoPasswordForm::operator=(MahoPasswordForm &&other) noexcept {
  if (this != &other) {
    Zeroize();
    id = std::move(other.id);
    revision = other.revision;
    scheme = other.scheme;
    signon_realm = std::move(other.signon_realm);
    url = std::move(other.url);
    action = std::move(other.action);
    federation_origin = std::move(other.federation_origin);
    submit_element = std::move(other.submit_element);
    username_element = std::move(other.username_element);
    password_element = std::move(other.password_element);
    username_value = std::move(other.username_value);
    password_value = std::move(other.password_value);
    all_alternative_usernames = std::move(other.all_alternative_usernames);
    date_created = other.date_created;
    date_last_used = other.date_last_used;
    date_last_filled = other.date_last_filled;
    date_password_modified = other.date_password_modified;
    date_received = other.date_received;
    blocked_by_user = other.blocked_by_user;
    type = other.type;
    times_used_in_html_form = other.times_used_in_html_form;
    display_name = std::move(other.display_name);
    icon_url = std::move(other.icon_url);
    match_type = other.match_type;
    skip_zero_click = other.skip_zero_click;
    in_store = other.in_store;
    form_data_base64 = std::move(other.form_data_base64);
    notes = std::move(other.notes);
  }
  return *this;
}

MahoCredentialIdentityMap::MahoCredentialIdentityMap() = default;
MahoCredentialIdentityMap::~MahoCredentialIdentityMap() = default;

int MahoCredentialIdentityMap::Observe(const std::string &vault_id,
                                       uint64_t revision) {
  vault_id_to_revision_[vault_id] = revision;
  const std::pair<std::string, uint64_t> snapshot(vault_id, revision);
  auto existing = snapshot_to_key_.find(snapshot);
  if (existing != snapshot_to_key_.end()) {
    return existing->second;
  }
  const int key = next_key_++;
  snapshot_to_key_[snapshot] = key;
  key_to_vault_id_[key] = vault_id;
  key_to_revision_[key] = revision;
  return key;
}

std::optional<std::string>
MahoCredentialIdentityMap::VaultIdForPrimaryKey(int primary_key) const {
  auto it = key_to_vault_id_.find(primary_key);
  if (it == key_to_vault_id_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<int> MahoCredentialIdentityMap::PrimaryKeyForVaultIdAndRevision(
    const std::string &vault_id, uint64_t revision) const {
  auto it = snapshot_to_key_.find(std::make_pair(vault_id, revision));
  if (it == snapshot_to_key_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<uint64_t> MahoCredentialIdentityMap::RevisionForVaultId(
    const std::string &vault_id) const {
  auto it = vault_id_to_revision_.find(vault_id);
  if (it == vault_id_to_revision_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<uint64_t>
MahoCredentialIdentityMap::RevisionForPrimaryKey(int primary_key) const {
  auto it = key_to_revision_.find(primary_key);
  if (it == key_to_revision_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void MahoCredentialIdentityMap::Clear() {
  snapshot_to_key_.clear();
  key_to_vault_id_.clear();
  key_to_revision_.clear();
  vault_id_to_revision_.clear();
  next_key_ = 1;
}

namespace {

// Release kill switch. While the local-state pref
// `maho.passwords.native_write_enabled` is false, every native WRITE (add/update)
// and FILL path must no-op without touching the Vault FFI. The pref defaults to
// true — the native provider is the shipped experience; setting it to false is
// the emergency shutoff. Metadata-only reads (GetAllLogins*) are intentionally
// not gated.
bool NativeWriteEnabled() {
  return IsNativePasswordWriteEnabled(GetNativePasswordWritePrefs());
}

SecureZeroizeObserverForTesting &SecureZeroizeObserver() {
  static base::NoDestructor<SecureZeroizeObserverForTesting> observer;
  return *observer;
}

void SecureZeroizeString(std::string &s) {
  if (!s.empty()) {
    std::fill_n(static_cast<volatile char *>(&s[0]), s.size(), 0);
    if (SecureZeroizeObserver()) {
      SecureZeroizeObserver().Run(s);
    }
    s.clear();
  }
}

// Move-only ownership for transient resolver material. Every exit path wipes
// the backing allocation before releasing it.
class ScopedZeroizedString {
public:
  ScopedZeroizedString() = default;
  explicit ScopedZeroizedString(std::string value) : value_(std::move(value)) {}
  ~ScopedZeroizedString() { SecureZeroizeString(value_); }

  ScopedZeroizedString(const ScopedZeroizedString &) = delete;
  ScopedZeroizedString &operator=(const ScopedZeroizedString &) = delete;
  ScopedZeroizedString(ScopedZeroizedString &&other) noexcept
      : value_(std::move(other.value_)) {}
  ScopedZeroizedString &operator=(ScopedZeroizedString &&other) noexcept {
    if (this != &other) {
      SecureZeroizeString(value_);
      value_ = std::move(other.value_);
    }
    return *this;
  }

  std::string &get() { return value_; }
  const std::string &get() const { return value_; }

private:
  std::string value_;
};

base::Time ParseTimestampField(const base::DictValue &dict,
                               const std::string &key) {
  if (std::optional<double> ts = dict.FindDouble(key)) {
    return base::Time::FromSecondsSinceUnixEpoch(*ts);
  }
  if (std::optional<int> ts_int = dict.FindInt(key)) {
    return base::Time::FromSecondsSinceUnixEpoch(*ts_int);
  }
  if (const std::string *ts_str = dict.FindString(key)) {
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

// Serializes `autofill::FormData` exactly the way upstream LoginDatabase does
// (a `base::Pickle`), then base64s it so it can travel through the Vault's JSON
// contract. The Vault stores the string opaquely.
std::string EncodeFormData(const autofill::FormData &form_data) {
  base::Pickle pickle;
  autofill::SerializeFormData(form_data, &pickle);
  if (pickle.size() == 0) {
    return std::string();
  }
  return base::Base64Encode(base::span<const uint8_t>(pickle));
}

autofill::FormData DecodeFormData(const std::string &encoded) {
  autofill::FormData form_data;
  if (encoded.empty()) {
    return form_data;
  }
  std::optional<std::vector<uint8_t>> blob = base::Base64Decode(encoded);
  if (!blob || blob->empty()) {
    return form_data;
  }
  base::PickleIterator iterator = base::PickleIterator::WithData(*blob);
  autofill::DeserializeFormData(&iterator, &form_data);
  return form_data;
}

std::optional<base::Time> TimeFromDictValue(const base::DictValue &dict,
                                            const std::string &key) {
  if (!dict.contains(key) || dict.Find(key)->is_none()) {
    return base::Time();
  }
  const std::string *serialized = dict.FindString(key);
  if (!serialized) {
    return std::nullopt;
  }
  int64_t internal_value = 0;
  if (!base::StringToInt64(*serialized, &internal_value)) {
    return std::nullopt;
  }
  return base::Time::FromInternalValue(internal_value);
}

// A `base::Time()` (null) timestamp is written as JSON null so the Vault stores
// "unset" rather than the epoch, which would be a different value on read-back.
void SetOptionalTime(base::DictValue &dict, const std::string &key,
                     base::Time value) {
  if (value.is_null()) {
    dict.Set(key, base::Value());
    return;
  }
  dict.Set(key, base::NumberToString(value.ToInternalValue()));
}

base::ListValue BuildAlternativeUsernames(
    const std::vector<password_manager::AlternativeElement> &elements) {
  base::ListValue list;
  for (const auto &element : elements) {
    base::DictValue entry;
    entry.Set("value", base::UTF16ToUTF8(element.value));
    entry.Set("fieldRendererId",
              base::NumberToString(element.field_renderer_id.value()));
    entry.Set("name", base::UTF16ToUTF8(element.name));
    list.Append(std::move(entry));
  }
  return list;
}

std::optional<std::vector<password_manager::AlternativeElement>>
ParseAlternativeUsernames(const base::ListValue *list) {
  std::vector<password_manager::AlternativeElement> elements;
  if (!list) {
    return elements;
  }
  for (const auto &item : *list) {
    if (!item.is_dict()) {
      return std::nullopt;
    }
    const base::DictValue &entry = item.GetDict();
    const std::string *value = entry.FindString("value");
    const std::string *name = entry.FindString("name");
    const std::string *serialized_renderer_id =
        entry.FindString("fieldRendererId");
    uint64_t renderer_id = 0;
    if (!value || !name || !serialized_renderer_id ||
        !base::StringToUint64(*serialized_renderer_id, &renderer_id)) {
      return std::nullopt;
    }
    elements.emplace_back(
        password_manager::AlternativeElement::Value(base::UTF8ToUTF16(*value)),
        autofill::FieldRendererId(renderer_id),
        password_manager::AlternativeElement::Name(base::UTF8ToUTF16(*name)));
  }
  return elements;
}

base::ListValue
BuildNotes(const std::vector<password_manager::PasswordNote> &notes) {
  base::ListValue list;
  for (const auto &note : notes) {
    base::DictValue entry;
    entry.Set("uniqueDisplayName", base::UTF16ToUTF8(note.unique_display_name));
    entry.Set("value", base::UTF16ToUTF8(note.value));
    SetOptionalTime(entry, "dateCreated", note.date_created);
    entry.Set("hideByDefault", note.hide_by_default);
    list.Append(std::move(entry));
  }
  return list;
}

std::optional<std::vector<password_manager::PasswordNote>>
ParseNotes(const base::ListValue *list) {
  std::vector<password_manager::PasswordNote> notes;
  if (!list) {
    return notes;
  }
  for (const auto &item : *list) {
    if (!item.is_dict()) {
      return std::nullopt;
    }
    const base::DictValue &entry = item.GetDict();
    const std::string *display_name = entry.FindString("uniqueDisplayName");
    const std::string *value = entry.FindString("value");
    std::optional<base::Time> date_created =
        TimeFromDictValue(entry, "dateCreated");
    const base::Value *hide_by_default = entry.Find("hideByDefault");
    if (!display_name || !value || !date_created ||
        (hide_by_default && !hide_by_default->is_bool())) {
      return std::nullopt;
    }
    notes.emplace_back(base::UTF8ToUTF16(*display_name),
                       base::UTF8ToUTF16(*value), *date_created,
                       entry.FindBool("hideByDefault").value_or(false));
  }
  return notes;
}

// Reads the `formDetails` sub-object the Rust backend session emits alongside a
// credential. LEGACY RECORDS: a record written before Task 4 has no
// `formDetails` at all (the Rust side omits the key). In that case every field
// below keeps the C++ default, which is exactly Chromium's own default for the
// corresponding `PasswordForm` field, and `signon_realm`/`username`/
// `password`/`date_created`/`date_last_used` still come from the top-level
// credential fields that always existed. No read fails because of a missing
// `formDetails`.
bool ApplyFormDetails(const base::DictValue &details, MahoPasswordForm &form) {
  if (std::optional<int> scheme = details.FindInt("scheme")) {
    if (*scheme >= static_cast<int>(
                       password_manager::PasswordForm::Scheme::kMinValue) &&
        *scheme <= static_cast<int>(
                       password_manager::PasswordForm::Scheme::kMaxValue)) {
      form.scheme =
          static_cast<password_manager::PasswordForm::Scheme>(*scheme);
    }
  }
  if (const std::string *realm = details.FindString("signonRealm")) {
    if (!realm->empty()) {
      form.signon_realm = *realm;
    }
  }
  if (const std::string *url = details.FindString("url")) {
    form.url = *url;
  }
  if (const std::string *action = details.FindString("action")) {
    form.action = *action;
  }
  if (const std::string *federation = details.FindString("federationOrigin")) {
    form.federation_origin = *federation;
  }
  if (const std::string *submit = details.FindString("submitElement")) {
    form.submit_element = *submit;
  }
  if (const std::string *username_element =
          details.FindString("usernameElement")) {
    form.username_element = *username_element;
  }
  if (const std::string *password_element =
          details.FindString("passwordElement")) {
    form.password_element = *password_element;
  }
  std::optional<std::vector<password_manager::AlternativeElement>>
      alternative_usernames = ParseAlternativeUsernames(
          details.FindList("allAlternativeUsernames"));
  if (!alternative_usernames) {
    return false;
  }
  form.all_alternative_usernames = std::move(*alternative_usernames);
  // The Vault row's `createdAt` records when Maho PERSISTED the item, which is
  // not the credential's own creation date; the stored form detail is
  // authoritative when present.
  std::optional<base::Time> date_created =
      TimeFromDictValue(details, "dateCreated");
  if (!date_created) {
    return false;
  }
  if (!date_created->is_null()) {
    form.date_created = *date_created;
  }
  // Only override the top-level `lastUsedAt` when the record actually carries a
  // form-level value; a null/absent entry means "never used", which the
  // top-level field already expresses.
  std::optional<base::Time> date_last_used =
      TimeFromDictValue(details, "dateLastUsed");
  if (!date_last_used) {
    return false;
  }
  if (!date_last_used->is_null()) {
    form.date_last_used = *date_last_used;
  }
  std::optional<base::Time> date_password_modified =
      TimeFromDictValue(details, "datePasswordModified");
  std::optional<base::Time> date_last_filled =
      TimeFromDictValue(details, "dateLastFilled");
  std::optional<base::Time> date_received =
      TimeFromDictValue(details, "dateReceived");
  if (!date_password_modified || !date_last_filled || !date_received) {
    return false;
  }
  form.date_password_modified = *date_password_modified;
  form.date_last_filled = *date_last_filled;
  form.date_received = *date_received;
  form.blocked_by_user = details.FindBool("blockedByUser").value_or(false);
  if (std::optional<int> type = details.FindInt("credentialType")) {
    if (*type >=
            static_cast<int>(password_manager::PasswordForm::Type::kMinValue) &&
        *type <=
            static_cast<int>(password_manager::PasswordForm::Type::kMaxValue)) {
      form.type = static_cast<password_manager::PasswordForm::Type>(*type);
    }
  }
  form.times_used_in_html_form =
      details.FindInt("timesUsedInHtmlForm").value_or(0);
  if (std::optional<int> match_type = details.FindInt("matchType")) {
    form.match_type = static_cast<password_manager::PasswordForm::MatchType>(
        static_cast<uint32_t>(*match_type));
  } else {
    form.match_type = std::nullopt;
  }
  if (std::optional<int> in_store = details.FindInt("inStore")) {
    form.in_store =
        static_cast<password_manager::PasswordForm::Store>(*in_store);
  }
  form.skip_zero_click = details.FindBool("skipZeroClick").value_or(false);
  if (const std::string *display_name = details.FindString("displayName")) {
    form.display_name = base::UTF8ToUTF16(*display_name);
  }
  if (const std::string *icon_url = details.FindString("iconUrl")) {
    form.icon_url = *icon_url;
  }
  if (const std::string *form_data = details.FindString("formData")) {
    form.form_data_base64 = *form_data;
  }
  std::optional<std::vector<password_manager::PasswordNote>> notes =
      ParseNotes(details.FindList("notes"));
  if (!notes) {
    return false;
  }
  form.notes = std::move(*notes);
  return true;
}

base::DictValue BuildFormDetails(const MahoPasswordForm &form) {
  base::DictValue details;
  details.Set("scheme", static_cast<int>(form.scheme));
  details.Set("signonRealm", form.signon_realm);
  details.Set("url", form.url);
  details.Set("action", form.action);
  details.Set("federationOrigin", form.federation_origin);
  details.Set("submitElement", form.submit_element);
  details.Set("usernameElement", form.username_element);
  details.Set("passwordElement", form.password_element);
  details.Set("allAlternativeUsernames",
              BuildAlternativeUsernames(form.all_alternative_usernames));
  SetOptionalTime(details, "dateCreated", form.date_created);
  SetOptionalTime(details, "dateLastUsed", form.date_last_used);
  SetOptionalTime(details, "datePasswordModified", form.date_password_modified);
  SetOptionalTime(details, "dateLastFilled", form.date_last_filled);
  SetOptionalTime(details, "dateReceived", form.date_received);
  details.Set("blockedByUser", form.blocked_by_user);
  details.Set("credentialType", static_cast<int>(form.type));
  details.Set("timesUsedInHtmlForm", form.times_used_in_html_form);
  if (form.match_type.has_value()) {
    details.Set("matchType",
                static_cast<int>(static_cast<uint32_t>(*form.match_type)));
  } else {
    details.Set("matchType", base::Value());
  }
  details.Set("inStore", static_cast<int>(form.in_store));
  details.Set("skipZeroClick", form.skip_zero_click);
  details.Set("displayName", base::UTF16ToUTF8(form.display_name));
  details.Set("iconUrl", form.icon_url);
  details.Set("formData", form.form_data_base64);
  details.Set("notes", BuildNotes(form.notes));
  return details;
}

std::optional<MahoPasswordForm>
ParsePasswordFormFromDict(const base::DictValue &dict) {
  // Discovery is an untrusted metadata descriptor. Secret-bearing or legacy
  // identity aliases are rejected rather than interpreted.
  if (dict.contains("password") || dict.contains("secret") ||
      dict.contains("id") || dict.contains("revision")) {
    return std::nullopt;
  }

  const std::string *item_id = dict.FindString("itemId");
  const std::string *observed_revision =
      dict.FindString("observedRevision");
  const std::string *signon_realm = dict.FindString("signonRealm");
  const std::string *username = dict.FindString("username");
  if (!item_id || item_id->empty() || !observed_revision ||
      observed_revision->empty() || !signon_realm || signon_realm->empty() ||
      !username) {
    return std::nullopt;
  }

  const base::Uuid parsed_item_id = base::Uuid::ParseLowercase(*item_id);
  if (!parsed_item_id.is_valid()) {
    return std::nullopt;
  }

  MahoPasswordForm form;
  form.id = parsed_item_id.AsLowercaseString();
  form.signon_realm = *signon_realm;
  form.username_value = *username;
  if (!base::StringToUint64(*observed_revision, &form.revision) ||
      form.revision == 0 ||
      base::NumberToString(form.revision) != *observed_revision) {
    return std::nullopt;
  }
  form.date_created = ParseTimestampField(dict, "createdAt");
  form.date_last_used = ParseTimestampField(dict, "lastUsedAt");
  if (const base::DictValue *details = dict.FindDict("formDetails")) {
    if (!ApplyFormDetails(*details, form)) {
      return std::nullopt;
    }
  }
  // `password_value` deliberately remains empty for every discovery result.
  return form;
}

std::optional<password_manager::PasswordFillResolution>
ResolveBackendPassword(const std::string &serving_profile_key,
                       const std::string &item_id, uint64_t revision) {
  maho::core::VaultBackendSession session =
      maho::CreateVaultBackendSessionForProfileKey(serving_profile_key);
  if (!session.is_valid()) {
    return std::nullopt;
  }

  base::DictValue request;
  request.Set("itemId", item_id);
  request.Set("expectedRevision", base::NumberToString(revision));
  request.Set("action", "fill");
  ScopedZeroizedString request_json;
  base::JSONWriter::Write(request, &request_json.get());

  maho::core::VaultBackendResult result =
      session.ExecuteCredentials(request_json.get());
  if (!result.is_valid() ||
      result.status() != maho::core::VaultBackendResultStatus::kSuccess) {
    return std::nullopt;
  }

  ScopedZeroizedString response_json(result.ConsumeJson());
  std::optional<base::Value> parsed =
      base::JSONReader::Read(response_json.get(), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict() || parsed->GetDict().size() != 1u) {
    return std::nullopt;
  }
  std::optional<base::Value> password_value =
      parsed->GetDict().Extract("password");
  if (!password_value || !password_value->is_string() ||
      password_value->GetString().empty()) {
    return std::nullopt;
  }
  ScopedZeroizedString password(std::move(*password_value).TakeString());
  std::u16string password_utf16 = base::UTF8ToUTF16(password.get());
  if (password_utf16.empty()) {
    return std::nullopt;
  }
  return password_manager::PasswordFillResolution(std::move(password_utf16));
}

std::optional<base::DictValue>
BuildLoginMetadata(const MahoPasswordForm &form) {
  const GURL canonical_url(!form.url.empty() ? form.url : form.signon_realm);
  if (!canonical_url.is_valid()) {
    return std::nullopt;
  }
  const bool is_web_credential = canonical_url.SchemeIsHTTPOrHTTPS();
  const bool is_android_credential = canonical_url.SchemeIs("android");
  if (!is_web_credential && !is_android_credential) {
    return std::nullopt;
  }
  if (is_android_credential) {
    const GURL realm(form.signon_realm);
    if (!realm.is_valid() || !realm.SchemeIs("android") || realm.has_query() ||
        realm.has_ref() || realm.GetContent().size() <= 2 ||
        realm.spec() != canonical_url.spec()) {
      return std::nullopt;
    }
  }

  base::DictValue metadata;
  metadata.Set("title", form.signon_realm);
  metadata.Set("usernameHint", form.username_value);
  metadata.Set("itemKind", "login");

  base::ListValue origins;
  if (is_web_credential) {
    origins.Append(url::Origin::Create(canonical_url).Serialize());
  }
  metadata.Set("origins", std::move(origins));
  metadata.Set("totp", base::Value());
  metadata.Set("passkey", base::Value());
  return metadata;
}

bool SetExpectedRevision(base::DictValue &request, uint64_t revision) {
  request.Set("expectedRevision", base::NumberToString(revision));
  return true;
}

std::optional<MahoMutationResult>
ParseMutationResult(const base::Value &response) {
  if (!response.is_dict() ||
      !response.GetDict().FindBool("ok").value_or(false)) {
    return std::nullopt;
  }
  const base::DictValue *data = response.GetDict().FindDict("data");
  if (!data) {
    return std::nullopt;
  }
  const std::string *id = data->FindString("id");
  if (!id || id->empty()) {
    return std::nullopt;
  }
  uint64_t rev = 0;
  if (const std::string *rev_str = data->FindString("revision")) {
    if (!base::StringToUint64(*rev_str, &rev)) {
      return std::nullopt;
    }
  } else if (std::optional<double> rev_num = data->FindDouble("revision")) {
    if (!std::isfinite(*rev_num) || *rev_num < 0) {
      return std::nullopt;
    }
    rev = static_cast<uint64_t>(*rev_num);
  } else {
    return std::nullopt;
  }
  MahoMutationResult result;
  result.id = *id;
  result.revision = rev;
  return result;
}

bool ReadBackendCredentials(
    const std::optional<std::string> &origin,
    const std::string &serving_profile_key,
    std::optional<password_manager::PasswordForm::Scheme> scheme,
    std::vector<MahoPasswordForm> &forms) {
  maho::core::VaultBackendSession session =
      maho::CreateVaultBackendSessionForProfileKey(serving_profile_key);
  if (!session.is_valid()) {
    return false;
  }

  base::DictValue req;
  if (origin && !origin->empty()) {
    req.Set("origin", *origin);
    // The request scheme tells the vault whether this is an HTML-origin
    // lookup or an HTTP-auth realm lookup; `Basic realm=""` makes both share
    // one signon realm, so the stored records alone cannot decide.
    if (scheme) {
      req.Set("scheme", static_cast<int>(*scheme));
    }
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
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  SecureZeroizeString(json);
  if (!parsed || !parsed->is_list()) {
    return false;
  }

  for (const auto &item : parsed->GetList()) {
    if (!item.is_dict()) {
      return false;
    }
    std::optional<MahoPasswordForm> form =
        ParsePasswordFormFromDict(item.GetDict());
    if (!form) {
      return false;
    }
    forms.push_back(std::move(*form));
  }
  return true;
}

// Task 4: `password_manager::PasswordForm` fields this backend does NOT
// round-trip. They are deliberately FAILED CLOSED: on read they are left at
// Chromium's own default rather than guessed, so no caller can mistake a
// reconstructed value for a stored one. Listing them here is the authoritative
// statement that losslessness is scoped, not blanket.
//
//   affiliated_web_realm, app_display_name, app_icon_url  - injected by the
//       affiliation service on demand; storing a stale copy would be wrong.
//   previously_associated_sync_account_email, moving_blocked_for_list  -
//   account
//       -store/sync concepts; the Maho account store is disabled.
//   password_issues                     - owned by the (not-yet-ported)
//   insecure
//                                         credentials table.
//   generation_upload_status            - votes-upload state, not credential
//   data. keychain_identifier                 - iOS-only. sender_email,
//   sender_name, sender_profile_image_url, sharing_notification_displayed -
//   password-sharing metadata; sharing is
//                                         not supported by the Vault.
//   actor_login_approved                - actor-login state, not persisted.
//
// `primary_key` is not stored either: it is ASSIGNED by
// `MahoCredentialIdentityMap` from the Vault UUID (see the header).
constexpr const char *const kUnsupportedFields[] = {
    "affiliated_web_realm",
    "app_display_name",
    "app_icon_url",
    "previously_associated_sync_account_email",
    "moving_blocked_for_list",
    "password_issues",
    "generation_upload_status",
    "keychain_identifier",
    "sender_email",
    "sender_name",
    "sender_profile_image_url",
    "sharing_notification_displayed",
    "actor_login_approved",
};

PasswordStoreBackendCredential ToBackendCredential(
    const MahoPasswordForm &form,
    std::optional<int> primary_key) {
  PasswordStoreBackendCredential cred;
  if (primary_key.has_value()) {
    cred.primary_key = password_manager::FormPrimaryKey(*primary_key);
  }
  cred.scheme = form.scheme;
  cred.signon_realm = form.signon_realm;
  cred.url = GURL(form.url);
  cred.action = GURL(form.action);
  if (!form.federation_origin.empty()) {
    cred.federation_origin = url::SchemeHostPort(GURL(form.federation_origin));
  }
  cred.submit_element = base::UTF8ToUTF16(form.submit_element);
  cred.username_element = base::UTF8ToUTF16(form.username_element);
  cred.password_element = base::UTF8ToUTF16(form.password_element);
  cred.username_value = base::UTF8ToUTF16(form.username_value);
#if MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL
  cred.password_value =
      password_manager::PasswordString(base::UTF8ToUTF16(form.password_value));
#else
  cred.password_value = base::UTF8ToUTF16(form.password_value);
#endif
  cred.all_alternative_usernames = form.all_alternative_usernames;
  cred.date_created = form.date_created;
  cred.date_last_used = form.date_last_used;
  cred.date_last_filled = form.date_last_filled;
  cred.date_password_modified = form.date_password_modified;
  cred.date_received = form.date_received;
  cred.blocked_by_user = form.blocked_by_user;
  cred.type = form.type;
  cred.times_used_in_html_form = form.times_used_in_html_form;
  cred.display_name = form.display_name;
  cred.icon_url = GURL(form.icon_url);
  if (form.match_type.has_value()) {
    cred.match_type = *form.match_type;
  } else {
    cred.match_type.reset();
  }
  cred.skip_zero_click = form.skip_zero_click;
  cred.in_store = form.in_store;
  cred.form_data = DecodeFormData(form.form_data_base64);
  cred.notes = form.notes;
  return cred;
}

MahoPasswordForm FromBackendCredential(
    const PasswordStoreBackendCredential &cred) {
  MahoPasswordForm form;
  form.scheme = cred.scheme;
  form.signon_realm = cred.signon_realm;
  form.url = cred.url.is_valid() ? cred.url.spec() : std::string();
  form.action = cred.action.is_valid() ? cred.action.spec() : std::string();
  form.federation_origin = cred.federation_origin.IsValid()
                               ? cred.federation_origin.Serialize()
                               : std::string();
  form.submit_element = base::UTF16ToUTF8(cred.submit_element);
  form.username_element = base::UTF16ToUTF8(cred.username_element);
  form.password_element = base::UTF16ToUTF8(cred.password_element);
  form.username_value = base::UTF16ToUTF8(cred.username_value);
#if MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL
  form.password_value = base::UTF16ToUTF8(cred.password_value.value());
#else
  form.password_value = base::UTF16ToUTF8(cred.password_value);
#endif
  form.all_alternative_usernames = cred.all_alternative_usernames;
  form.date_created = cred.date_created;
  form.date_last_used = cred.date_last_used;
  form.date_last_filled = cred.date_last_filled;
  form.date_password_modified = cred.date_password_modified;
  form.date_received = cred.date_received;
  form.blocked_by_user = cred.blocked_by_user;
  form.type = cred.type;
  form.times_used_in_html_form = cred.times_used_in_html_form;
  form.display_name = cred.display_name;
  form.icon_url =
      cred.icon_url.is_valid() ? cred.icon_url.spec() : std::string();
  form.match_type = cred.match_type;
  form.skip_zero_click = cred.skip_zero_click;
  form.in_store = cred.in_store;
  form.form_data_base64 = EncodeFormData(cred.form_data);
  form.notes = cred.notes;
  return form;
}

ChromiumBackendLoginsResult ConvertToBackendCredentials(
    const std::vector<MahoPasswordForm> &forms,
    const MahoCredentialIdentityMap *identity_map) {
  ChromiumBackendLoginsResult results;
  results.reserve(forms.size());
  for (const auto &form : forms) {
    // The map was already populated by ObserveIdentities() on this read, so the
    // key is a pure lookup here.
    std::optional<int> primary_key;
    if (identity_map && !form.id.empty()) {
      primary_key =
          identity_map->PrimaryKeyForVaultIdAndRevision(form.id, form.revision);
    }
    results.push_back(ToBackendCredential(form, primary_key));
  }
  return results;
}

} // namespace

std::vector<std::string> UnsupportedPasswordFormFields() {
  return std::vector<std::string>(std::begin(kUnsupportedFields),
                                  std::end(kUnsupportedFields));
}

void SetSecureZeroizeObserverForTesting(
    SecureZeroizeObserverForTesting observer) {
  SecureZeroizeObserver() = std::move(observer);
}

// static
bool MahoPasswordStoreBackend::IsMahoVaultRoutingActive() {
  // Evaluated per operation on purpose: the effective provider can change at
  // runtime (settings edit, extension install/uninstall, provider registry
  // change) and the password store is never recreated when it does.
  return IsNativePasswordProviderActive();
}

bool IsPasswordOperationAuthorized(const std::string &profile_key,
                                   const std::string &origin_scope,
                                   PasswordAuthorizationAction action) {
  return maho::CreateVaultBackendSessionForProfileKey(profile_key).is_valid() &&
         MahoPasswordAuthorizationService::Get()->ConsumeAuthorization(
             profile_key, origin_scope, action);
}

MahoPasswordStoreBackend::MahoPasswordStoreBackend(bool enabled,
                                                   std::string profile_key)
    : profile_key_(std::move(profile_key)), enabled_(enabled) {}

MahoPasswordStoreBackend::~MahoPasswordStoreBackend() = default;

void MahoPasswordStoreBackend::InitBackend(
    RemoteChangesReceived remote_form_changes_received,
    base::RepeatingClosure sync_enabled_or_disabled_cb,
    base::OnceCallback<void(bool)> completion) {
  InitBackend(std::move(completion));
}

void MahoPasswordStoreBackend::InitBackend(
    base::OnceCallback<void(bool)> completion) {
  identity_map_.Clear();
  fill_context_snapshots_.clear();
  // Task 4: initialization must report REAL failure. Previously this always
  // reported success, so a broken Vault seam looked healthy to
  // PasswordStoreBackend consumers and GetError() stayed kNoError forever.
  //
  // When Maho Native is NOT the effective provider the backend is intentionally
  // dormant (Task 3 keeps every operation gated), so there is nothing to probe
  // and initialization legitimately succeeds. When it IS the effective
  // provider, the Vault backend session is the seam every read/fill depends on:
  // if it cannot be created, initialization fails and GetError() says so.
  bool success = true;
  last_error_ = password_manager::ActionableError::kNoError;
  if (enabled_ && IsMahoVaultRoutingActive()) {
    maho::core::VaultBackendSession session =
        maho::CreateVaultBackendSessionForProfileKey(profile_key_);
    if (!session.is_valid()) {
      success = false;
      last_error_ = password_manager::ActionableError::kInactionable;
    } else {
      // Probe with an EMPTY batch read: it exercises the whole seam (session ->
      // runtime -> storage) without decrypting a single secret.
      maho::core::VaultBackendResult probe =
          session.ExecuteBatch(R"({"schemaVersion":1,"itemIds":[]})");
      switch (probe.status()) {
      case maho::core::VaultBackendResultStatus::kSuccess:
        break;
      case maho::core::VaultBackendResultStatus::kLocked:
        // A locked Vault is not a broken backend: initialization succeeds and
        // the store stays usable, but the error is reported so the UI can ask
        // the user to unlock instead of pretending everything is fine.
        last_error_ = password_manager::ActionableError::kNeedsPassphrase;
        break;
      case maho::core::VaultBackendResultStatus::kInvalidRequest:
      case maho::core::VaultBackendResultStatus::kRuntimeUnavailable:
      case maho::core::VaultBackendResultStatus::kFailed:
        success = false;
        last_error_ = password_manager::ActionableError::kInactionable;
        break;
      }
    }
  }
  initialized_ = success;
  if (completion) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(completion), success));
  }
}

void MahoPasswordStoreBackend::ObserveIdentities(
    const std::vector<MahoPasswordForm> &forms) {
  for (const auto &form : forms) {
    if (!form.id.empty()) {
      identity_map_.Observe(form.id, form.revision);
    }
  }
}

void MahoPasswordStoreBackend::RecordFillContext(
    const password_manager::PasswordFillRequestContext &context,
    const std::vector<MahoPasswordForm> &forms) {
  std::map<int, FillContextSnapshot> snapshots;
  for (const auto &form : forms) {
    std::optional<int> primary_key =
        identity_map_.PrimaryKeyForVaultIdAndRevision(form.id, form.revision);
    if (primary_key) {
      snapshots.emplace(*primary_key,
                        FillContextSnapshot{.revision = form.revision,
                                            .signon_realm = form.signon_realm});
    }
  }
  fill_context_snapshots_[{context.requesting_origin.Serialize(),
                           context.document_token}] = std::move(snapshots);
}

bool MahoPasswordStoreBackend::WasSnapshotDiscoveredForContext(
    const password_manager::PasswordFillRequestContext &context,
    int primary_key, uint64_t revision) const {
  auto context_it = fill_context_snapshots_.find(
      {context.requesting_origin.Serialize(), context.document_token});
  if (context_it == fill_context_snapshots_.end()) {
    return false;
  }
  auto snapshot_it = context_it->second.find(primary_key);
  if (snapshot_it == context_it->second.end() ||
      snapshot_it->second.revision != revision) {
    return false;
  }
  const url::Origin discovered_origin =
      url::Origin::Create(GURL(snapshot_it->second.signon_realm));
  return !discovered_origin.opaque() &&
         context.requesting_origin == discovered_origin;
}

// static
std::optional<MahoPasswordForm> MahoPasswordStoreBackend::ResolveTarget(
    const PasswordStoreBackendCredential &cred,
    const MahoCredentialIdentityMap &identity_map,
    const std::vector<MahoPasswordForm> &candidates) {
  // 1. The primary key this backend handed out on a previous read is
  //    authoritative: it names exactly one Vault UUID even when several stored
  //    credentials share a realm and username.
  if (cred.primary_key.has_value()) {
    std::optional<std::string> vault_id =
        identity_map.VaultIdForPrimaryKey(cred.primary_key->value());
    if (vault_id.has_value()) {
      for (const auto &candidate : candidates) {
        if (candidate.id == *vault_id) {
          return candidate;
        }
      }
      // The key is ours but the item is gone from the Vault: fail closed rather
      // than mutating some other duplicate.
      return std::nullopt;
    }
  }

  // 2. No usable primary key (form parsed from a page, or from another store):
  //    select on the tuple Chromium itself treats as the credential identity.
  //    username_element participates so a duplicate-username-different-field
  //    pair is still distinguishable.
  const std::string username = base::UTF16ToUTF8(cred.username_value);
  const std::string username_element = base::UTF16ToUTF8(cred.username_element);
  const MahoPasswordForm *unique_match = nullptr;
  for (const auto &candidate : candidates) {
    if (candidate.username_value != username) {
      continue;
    }
    if (!username_element.empty() &&
        candidate.username_element != username_element) {
      continue;
    }
    if (unique_match) {
      return std::nullopt;
    }
    unique_match = &candidate;
  }
  if (unique_match) {
    return *unique_match;
  }
  return std::nullopt;
}

void MahoPasswordStoreBackend::Shutdown(base::OnceClosure done) {
  initialized_ = false;
  MahoPasswordAuthorizationService::Get()->RevokeProfile(profile_key_);
  identity_map_.Clear();
  fill_context_snapshots_.clear();
  weak_ptr_factory_.InvalidateWeakPtrs();
  if (done) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                             std::move(done));
  }
}

bool MahoPasswordStoreBackend::IsInitialized() const { return initialized_; }

password_manager::ActionableError MahoPasswordStoreBackend::GetError() {
  return last_error_;
}

void MahoPasswordStoreBackend::OnSyncServiceInitialized(
    syncer::SyncService *sync_service) {}

base::WeakPtr<password_manager::PasswordStoreBackend>
MahoPasswordStoreBackend::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void MahoPasswordStoreBackend::GetAllLoginsAsync(
    ChromiumLoginsOrErrorReply callback) {
  // Task 3: an external provider (or a disabled/fail-closed provider state)
  // means Maho must not read the Vault at all; return the empty result the
  // PasswordStoreBackend contract expects so upstream + the extension keep
  // their own behavior.
  if (!enabled_ || !IsMahoVaultRoutingActive()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  ChromiumBackendLoginsResult()));
    return;
  }
  GetAllLoginsMahoAsync(base::BindOnce(
      [](base::WeakPtr<MahoPasswordStoreBackend> backend,
         ChromiumLoginsOrErrorReply cb,
         std::vector<MahoPasswordForm> forms, bool success) {
        if (!backend || !success) {
          std::move(cb).Run(password_manager::PasswordStoreBackendError(
              password_manager::PasswordStoreBackendErrorType::kUncategorized));
          return;
        }
        backend->ObserveIdentities(forms);
        std::move(cb).Run(
            ConvertToBackendCredentials(forms, &backend->identity_map_));
      },
      weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoPasswordStoreBackend::GetAllLoginsWithAffiliationAndBrandingAsync(
    ChromiumLoginsOrErrorReply callback) {
  GetAllLoginsAsync(std::move(callback));
}

void MahoPasswordStoreBackend::GetAutofillableLoginsAsync(
    ChromiumLoginsOrErrorReply callback) {
  if (!enabled_ || !IsMahoVaultRoutingActive()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  ChromiumBackendLoginsResult()));
    return;
  }
  GetAutofillableLoginsMahoAsync(base::BindOnce(
      [](base::WeakPtr<MahoPasswordStoreBackend> backend,
         ChromiumLoginsOrErrorReply cb,
         std::vector<MahoPasswordForm> forms, bool success) {
        if (!backend || !success) {
          std::move(cb).Run(password_manager::PasswordStoreBackendError(
              password_manager::PasswordStoreBackendErrorType::kUncategorized));
          return;
        }
        backend->ObserveIdentities(forms);
        std::move(cb).Run(
            ConvertToBackendCredentials(forms, &backend->identity_map_));
      },
      weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoPasswordStoreBackend::FillMatchingLoginsAsync(
    ChromiumLoginsOrErrorReply callback, bool include_psl,
    const std::vector<password_manager::PasswordFormDigest> &forms) {
  if (!enabled_ || !NativeWriteEnabled() || !IsMahoVaultRoutingActive()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  ChromiumBackendLoginsResult()));
    return;
  }

  struct RealmSchemeQuery {
    std::string signon_realm;
    password_manager::PasswordForm::Scheme scheme;
  };
  std::vector<RealmSchemeQuery> queries;
  for (const auto &digest : forms) {
    if (digest.signon_realm.empty()) {
      continue;
    }
    const bool already_requested = std::ranges::any_of(
        queries, [&](const RealmSchemeQuery &query) {
          return query.signon_realm == digest.signon_realm &&
                 query.scheme == digest.scheme;
        });
    if (!already_requested) {
      queries.push_back({digest.signon_realm, digest.scheme});
    }
  }

  if (queries.empty()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  ChromiumBackendLoginsResult()));
    return;
  }

  using RealmQueryResult = std::pair<std::vector<MahoPasswordForm>, bool>;
  auto barrier_cb = base::BarrierCallback<RealmQueryResult>(
      queries.size(),
      base::BindOnce(
          [](base::WeakPtr<MahoPasswordStoreBackend> backend,
             ChromiumLoginsOrErrorReply cb,
             std::vector<RealmQueryResult> results) {
            if (!backend) {
              return;
            }
            std::vector<MahoPasswordForm> combined_forms;
            for (auto &[realm_forms, success] : results) {
              if (!success) {
                std::move(cb).Run(password_manager::PasswordStoreBackendError(
                    password_manager::PasswordStoreBackendErrorType::
                        kUncategorized));
                return;
              }
              combined_forms.insert(combined_forms.end(),
                                    std::make_move_iterator(realm_forms.begin()),
                                    std::make_move_iterator(realm_forms.end()));
            }
            backend->ObserveIdentities(combined_forms);
            std::move(cb).Run(
                ConvertToBackendCredentials(combined_forms,
                                            &backend->identity_map_));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));

  for (const auto &query : queries) {
    FillMatchingLoginsMahoAsync(
        query.signon_realm, query.scheme,
        base::BindOnce(
            [](base::RepeatingCallback<void(RealmQueryResult)> barrier,
               std::vector<MahoPasswordForm> forms, bool success) {
              barrier.Run(std::make_pair(std::move(forms), success));
            },
            barrier_cb));
  }
}

void MahoPasswordStoreBackend::GetGroupedMatchingLoginsAsync(
    const password_manager::PasswordFormDigest &form_digest,
    ChromiumLoginsOrErrorReply callback) {
  if (!enabled_ || !NativeWriteEnabled() || !IsMahoVaultRoutingActive()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  ChromiumBackendLoginsResult()));
    return;
  }
  FillMatchingLoginsMahoAsync(
      form_digest.signon_realm, form_digest.scheme,
      base::BindOnce(
          [](base::WeakPtr<MahoPasswordStoreBackend> backend,
             ChromiumLoginsOrErrorReply cb,
             std::vector<MahoPasswordForm> forms, bool success) {
            if (!backend || !success) {
              std::move(cb).Run(password_manager::PasswordStoreBackendError(
                  password_manager::PasswordStoreBackendErrorType::
                      kUncategorized));
              return;
            }
            backend->ObserveIdentities(forms);
            ChromiumBackendLoginsResult creds =
                ConvertToBackendCredentials(forms, &backend->identity_map_);
            for (auto &c : creds) {
              if (!c.match_type.has_value()) {
                c.match_type =
                    password_manager::PasswordForm::MatchType::kExact;
              }
            }
            std::move(cb).Run(std::move(creds));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoPasswordStoreBackend::GetGroupedMatchingLoginsAsync(
    const password_manager::PasswordFormDigest &form_digest,
    const password_manager::PasswordFillRequestContext &context,
    ChromiumLoginsOrErrorReply callback) {
  if (!enabled_ || !NativeWriteEnabled() || !IsMahoVaultRoutingActive()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  ChromiumBackendLoginsResult()));
    return;
  }

  const std::string realm = !context.requesting_origin.opaque()
                                ? context.requesting_origin.Serialize()
                                : form_digest.signon_realm;
  if (realm.empty()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  ChromiumBackendLoginsResult()));
    return;
  }

  FillMatchingLoginsMahoAsync(
      realm, form_digest.scheme,
      base::BindOnce(
          [](base::WeakPtr<MahoPasswordStoreBackend> backend,
             password_manager::PasswordFillRequestContext request_context,
             ChromiumLoginsOrErrorReply cb,
             std::vector<MahoPasswordForm> forms, bool success) {
            if (!backend || !success) {
              std::move(cb).Run(password_manager::PasswordStoreBackendError(
                  password_manager::PasswordStoreBackendErrorType::
                      kUncategorized));
              return;
            }
            backend->ObserveIdentities(forms);
            backend->RecordFillContext(request_context, forms);
            ChromiumBackendLoginsResult creds =
                ConvertToBackendCredentials(forms, &backend->identity_map_);
            for (auto &c : creds) {
              if (!c.match_type.has_value()) {
                c.match_type =
                    password_manager::PasswordForm::MatchType::kExact;
              }
            }
            std::move(cb).Run(std::move(creds));
          },
          weak_ptr_factory_.GetWeakPtr(), context, std::move(callback)));
}

void MahoPasswordStoreBackend::ResolvePasswordFill(
    const password_manager::PasswordFillRequestContext &context,
    const password_manager::PasswordFillSelection &selection,
    password_manager::PasswordFillResolver resolver) {
  if (!enabled_ || !initialized_ || !NativeWriteEnabled() ||
      !IsMahoVaultRoutingActive() || context.requesting_origin.opaque() ||
      !selection.primary_key || selection.observed_revision == 0) {
    LOG(INFO) << "ResolvePasswordFill failed check 1: enabled=" << enabled_
              << " init=" << initialized_ << " nwe=" << NativeWriteEnabled()
              << " routing=" << IsMahoVaultRoutingActive()
              << " opaque=" << context.requesting_origin.opaque()
              << " pk=" << selection.primary_key.has_value()
              << " rev=" << selection.observed_revision;
    std::move(resolver).Run(std::nullopt);
    return;
  }

  const int primary_key = selection.primary_key->value();
  std::optional<std::string> item_id =
      identity_map_.VaultIdForPrimaryKey(primary_key);
  std::optional<uint64_t> revision =
      identity_map_.RevisionForPrimaryKey(primary_key);
  if (!item_id || !revision || *revision != selection.observed_revision ||
      identity_map_.RevisionForVaultId(*item_id) != revision ||
      !WasSnapshotDiscoveredForContext(context, primary_key, *revision)) {
    LOG(INFO) << "ResolvePasswordFill failed check 2: item_id=" << item_id.has_value()
              << " rev_match=" << (revision && *revision == selection.observed_revision)
              << " rev_vault_match=" << (item_id && revision && identity_map_.RevisionForVaultId(*item_id) == revision)
              << " was_snapshot=" << (revision && WasSnapshotDiscoveredForContext(context, primary_key, *revision));
    std::move(resolver).Run(std::nullopt);
    return;
  }

  const std::string origin = context.requesting_origin.Serialize();
  if (!IsPasswordOperationAuthorized(profile_key_, origin,
                                     PasswordAuthorizationAction::kFill) ||
      !NativeWriteEnabled() || !IsMahoVaultRoutingActive()) {
    LOG(INFO) << "ResolvePasswordFill failed check 3: auth="
              << IsPasswordOperationAuthorized(profile_key_, origin, PasswordAuthorizationAction::kFill);
    std::move(resolver).Run(std::nullopt);
    return;
  }

  auto resolution = ResolveBackendPassword(profile_key_, *item_id,
                                           selection.observed_revision);
  LOG(INFO) << "ResolvePasswordFill resolution success=" << resolution.has_value();
  std::move(resolver).Run(std::move(resolution));
}

void MahoPasswordStoreBackend::AddLoginAsync(
#if MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL
    PasswordStoreBackendCredential cred,
#else
    const PasswordStoreBackendCredential &cred,
#endif
    password_manager::PasswordChangesOrErrorReply callback) {
  if (!enabled_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       password_manager::PasswordStoreBackendError(
                           password_manager::PasswordStoreBackendErrorType::
                               kUncategorized)));
    return;
  }
  // Under an external provider the extension owns the save, and under a
  // disabled provider nothing is saved natively: in both cases reporting a
  // successful no-change write is correct, because no Maho prompt was shown and
  // no Maho write was expected.
  //
  // The kill switch is different. The user accepted a Maho save prompt, so
  // reporting success while writing nothing would tell the UI the credential is
  // stored when it is not. That must surface as an error instead.
  if (!NativeWriteEnabled()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       password_manager::PasswordStoreBackendError(
                           password_manager::PasswordStoreBackendErrorType::
                               kUncategorized)));
    return;
  }
  if (!IsMahoVaultRoutingActive()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  password_manager::PasswordStoreChangeList()));
    return;
  }
  // Task 4: the whole credential is carried to the Vault, not just
  // realm/username/password.
  MahoPasswordForm form = FromBackendCredential(cred);
  AddLoginWithResultMahoAsync(
      form,
      base::BindOnce(
          [](base::WeakPtr<MahoPasswordStoreBackend> backend,
             password_manager::PasswordChangesOrErrorReply cb,
             std::optional<MahoPasswordForm> snapshot) {
            if (!backend || !snapshot) {
              std::move(cb).Run(password_manager::PasswordStoreBackendError(
                  password_manager::PasswordStoreBackendErrorType::
                      kUncategorized));
              return;
            }
            const int primary_key = backend->identity_map_.Observe(
                snapshot->id, snapshot->revision);
            PasswordStoreBackendCredential added =
                ToBackendCredential(*snapshot, primary_key);
            snapshot->Zeroize();
            password_manager::PasswordStoreChangeList changes;
            changes.emplace_back(
                password_manager::PasswordStoreChange::ADD,
                std::move(added));
            std::move(cb).Run(std::move(changes));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoPasswordStoreBackend::UpdateLoginAsync(
#if MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL
    PasswordStoreBackendCredential cred,
#else
    const PasswordStoreBackendCredential &cred,
#endif
    password_manager::PasswordChangesOrErrorReply callback) {
  if (!enabled_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       password_manager::PasswordStoreBackendError(
                           password_manager::PasswordStoreBackendErrorType::
                               kUncategorized)));
    return;
  }
  // Same split as AddLoginAsync: a kill-switched write the user explicitly
  // accepted must not be reported as stored, while an external/disabled
  // provider legitimately has nothing to write natively.
  if (!NativeWriteEnabled()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       password_manager::PasswordStoreBackendError(
                           password_manager::PasswordStoreBackendErrorType::
                               kUncategorized)));
    return;
  }
  if (!IsMahoVaultRoutingActive()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  password_manager::PasswordStoreChangeList()));
    return;
  }
  std::optional<std::string> observed_vault_id;
  std::optional<uint64_t> observed_revision;
  if (cred.primary_key.has_value()) {
    observed_vault_id =
        identity_map_.VaultIdForPrimaryKey(cred.primary_key->value());
    if (!observed_vault_id) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(callback),
                         password_manager::PasswordStoreBackendError(
                             password_manager::PasswordStoreBackendErrorType::
                                 kUncategorized)));
      return;
    }
    observed_revision =
        identity_map_.RevisionForPrimaryKey(cred.primary_key->value());
    if (!observed_revision) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(callback),
                         password_manager::PasswordStoreBackendError(
                             password_manager::PasswordStoreBackendErrorType::
                                 kUncategorized)));
      return;
    }
  }
  const std::string realm = cred.signon_realm;
  ReadLoginsForOperationMahoAsync(
      realm, realm, PasswordAuthorizationAction::kUpdate,
      std::nullopt,
      base::BindOnce(
          [](base::WeakPtr<MahoPasswordStoreBackend> backend,
              PasswordStoreBackendCredential cred,
             std::optional<std::string> observed_vault_id,
             std::optional<uint64_t> observed_revision,
             password_manager::PasswordChangesOrErrorReply cb,
             std::vector<MahoPasswordForm> matched_forms, bool success) {
            if (!backend || !success) {
              std::move(cb).Run(password_manager::PasswordStoreBackendError(
                  password_manager::PasswordStoreBackendErrorType::
                      kUncategorized));
              return;
            }
            backend->ObserveIdentities(matched_forms);
            // Task 4: resolve WHICH duplicate to mutate, and carry the revision
            // that read observed so the Vault CAS rejects a stale write instead
            // of clobbering the wrong row.
            std::optional<MahoPasswordForm> target =
                ResolveTarget(cred, backend->identity_map_, matched_forms);
            if (!target.has_value()) {
              std::move(cb).Run(password_manager::PasswordStoreBackendError(
                  password_manager::PasswordStoreBackendErrorType::
                      kUncategorized));
              return;
            }
            if (observed_vault_id && target->id != *observed_vault_id) {
              std::move(cb).Run(password_manager::PasswordStoreBackendError(
                  password_manager::PasswordStoreBackendErrorType::
                      kUncategorized));
              return;
            }
            MahoPasswordForm form = FromBackendCredential(cred);
            form.id = target->id;
            form.revision = observed_revision.value_or(target->revision);
            // Discovery carries no plaintext, so an empty password is a
            // metadata-only patch. Rust preserves the encrypted existing
            // secret because the request omits `password`.
            const bool password_changed = !form.password_value.empty();
            if (form.date_created.is_null()) {
              form.date_created = target->date_created;
            }
            MahoPasswordForm snapshot = form;
            backend->UpdateLoginWithResultMahoAsync(
                form,
                base::BindOnce(
                    [](base::WeakPtr<MahoPasswordStoreBackend> backend,
                       password_manager::PasswordChangesOrErrorReply reply_cb,
                       MahoPasswordForm snapshot, bool password_changed,
                       std::optional<MahoPasswordForm> mutation_result) {
                      if (!backend || !mutation_result) {
                        std::move(reply_cb).Run(
                            password_manager::PasswordStoreBackendError(
                                password_manager::
                                    PasswordStoreBackendErrorType::
                                        kUncategorized));
                        return;
                      }
                      snapshot.id = mutation_result->id;
                      snapshot.revision = mutation_result->revision;
                      const int primary_key = backend->identity_map_.Observe(
                          snapshot.id, snapshot.revision);
                      PasswordStoreBackendCredential updated =
                          ToBackendCredential(snapshot, primary_key);
                      snapshot.Zeroize();
                      password_manager::PasswordStoreChangeList changes;
                      changes.emplace_back(
                          password_manager::PasswordStoreChange::UPDATE,
                          std::move(updated),
                          password_changed);
                      std::move(reply_cb).Run(std::move(changes));
                    },
                    backend, std::move(cb), std::move(snapshot),
                    password_changed));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(cred),
          std::move(observed_vault_id), std::move(observed_revision),
          std::move(callback)));
}

void MahoPasswordStoreBackend::RemoveLoginAsync(
    const base::Location &location,
#if MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL
    PasswordStoreBackendCredential cred,
#else
    const PasswordStoreBackendCredential &cred,
#endif
    password_manager::PasswordChangesOrErrorReply callback) {
  if (!enabled_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       password_manager::PasswordStoreBackendError(
                           password_manager::PasswordStoreBackendErrorType::
                               kUncategorized)));
    return;
  }
  if (!IsMahoVaultRoutingActive()) {
    // External/disabled provider: Maho owns nothing to remove here.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  password_manager::PasswordStoreChangeList()));
    return;
  }
  std::optional<std::string> observed_vault_id;
  std::optional<uint64_t> observed_revision;
  if (cred.primary_key.has_value()) {
    observed_vault_id =
        identity_map_.VaultIdForPrimaryKey(cred.primary_key->value());
    if (!observed_vault_id) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(callback),
                         password_manager::PasswordStoreBackendError(
                             password_manager::PasswordStoreBackendErrorType::
                                 kUncategorized)));
      return;
    }
    observed_revision =
        identity_map_.RevisionForPrimaryKey(cred.primary_key->value());
    if (!observed_revision) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(callback),
                         password_manager::PasswordStoreBackendError(
                             password_manager::PasswordStoreBackendErrorType::
                                 kUncategorized)));
      return;
    }
  }
  const std::string realm = cred.signon_realm;
  ReadLoginsForOperationMahoAsync(
      realm, realm, PasswordAuthorizationAction::kDelete,
      std::nullopt,
      base::BindOnce(
          [](base::WeakPtr<MahoPasswordStoreBackend> backend,
              PasswordStoreBackendCredential cred,
             std::optional<std::string> observed_vault_id,
             std::optional<uint64_t> observed_revision,
             password_manager::PasswordChangesOrErrorReply cb,
             std::vector<MahoPasswordForm> matched_forms, bool success) {
            if (!backend || !success) {
              std::move(cb).Run(password_manager::PasswordStoreBackendError(
                  password_manager::PasswordStoreBackendErrorType::
                      kUncategorized));
              return;
            }
            backend->ObserveIdentities(matched_forms);
            std::optional<MahoPasswordForm> target =
                ResolveTarget(cred, backend->identity_map_, matched_forms);
            if (!target.has_value()) {
              std::move(cb).Run(password_manager::PasswordStoreBackendError(
                  password_manager::PasswordStoreBackendErrorType::
                      kUncategorized));
              return;
            }
            if (observed_vault_id && target->id != *observed_vault_id) {
              std::move(cb).Run(password_manager::PasswordStoreBackendError(
                  password_manager::PasswordStoreBackendErrorType::
                      kUncategorized));
              return;
            }
            if (observed_revision) {
              target->revision = *observed_revision;
            }
            std::optional<int> target_key =
                backend->identity_map_.PrimaryKeyForVaultIdAndRevision(
                    target->id, target->revision);
            PasswordStoreBackendCredential removed =
                ToBackendCredential(*target, target_key);
            backend->RemoveLoginMahoAsync(
                *target,
                base::BindOnce(
                    [](password_manager::PasswordChangesOrErrorReply reply_cb,
                       PasswordStoreBackendCredential removed,
                       bool remove_ok) {
                      if (!remove_ok) {
                        std::move(reply_cb).Run(
                            password_manager::PasswordStoreBackendError(
                                password_manager::
                                    PasswordStoreBackendErrorType::
                                        kUncategorized));
                        return;
                      }
                      password_manager::PasswordStoreChangeList changes;
                      changes.emplace_back(
                          password_manager::PasswordStoreChange::REMOVE,
                          std::move(removed));
                      std::move(reply_cb).Run(std::move(changes));
                    },
                    std::move(cb), std::move(removed)));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(cred),
          std::move(observed_vault_id), std::move(observed_revision),
          std::move(callback)));
}

void MahoPasswordStoreBackend::RemoveLoginsCreatedBetweenAsync(
    const base::Location &location, base::Time delete_begin,
    base::Time delete_end,
    password_manager::PasswordChangesOrErrorReply callback) {
  if (!enabled_ || !IsMahoVaultRoutingActive()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  password_manager::PasswordStoreChangeList()));
    return;
  }
  // Task 4: snapshot the in-range credentials BEFORE deleting so the change
  // list can name every removed form; observers otherwise never learn what went
  // away.
  ReadLoginsForOperationMahoAsync(
      std::nullopt, MahoPasswordAuthorizationService::kRangeDeleteScope,
      PasswordAuthorizationAction::kRangeDelete, std::nullopt,
      base::BindOnce(
          [](base::WeakPtr<MahoPasswordStoreBackend> backend,
             base::Time delete_begin, base::Time delete_end,
             password_manager::PasswordChangesOrErrorReply cb,
             std::vector<MahoPasswordForm> forms, bool read_ok) {
            if (!backend || !read_ok) {
              std::move(cb).Run(password_manager::PasswordStoreBackendError(
                  password_manager::PasswordStoreBackendErrorType::
                      kUncategorized));
              return;
            }
            backend->ObserveIdentities(forms);
            std::vector<MahoPasswordForm> in_range;
            for (auto &form : forms) {
              if ((delete_begin.is_null() ||
                   form.date_created >= delete_begin) &&
                  (delete_end.is_null() || form.date_created <= delete_end)) {
                in_range.push_back(std::move(form));
              }
            }
            backend->RemoveLoginsCreatedBetweenMahoAsync(
                delete_begin, delete_end,
                base::BindOnce(
                    [](base::WeakPtr<MahoPasswordStoreBackend> backend,
                       std::vector<MahoPasswordForm> removed,
                       password_manager::PasswordChangesOrErrorReply cb2,
                       bool success) {
                      if (!backend || !success) {
                        std::move(cb2).Run(
                            password_manager::PasswordStoreBackendError(
                                password_manager::
                                    PasswordStoreBackendErrorType::
                                        kUncategorized));
                        return;
                      }
                      password_manager::PasswordStoreChangeList changes;
                      for (const auto &form : removed) {
                        std::optional<int> primary_key =
                            backend->identity_map_
                                .PrimaryKeyForVaultIdAndRevision(form.id,
                                                                 form.revision);
                        if (!primary_key) {
                          std::move(cb2).Run(
                              password_manager::PasswordStoreBackendError(
                                  password_manager::
                                      PasswordStoreBackendErrorType::
                                          kUncategorized));
                          return;
                        }
                        changes.emplace_back(
                            password_manager::PasswordStoreChange::REMOVE,
                            ToBackendCredential(form, primary_key));
                      }
                      std::move(cb2).Run(std::move(changes));
                    },
                    backend, std::move(in_range), std::move(cb)));
          },
          weak_ptr_factory_.GetWeakPtr(), delete_begin, delete_end,
          std::move(callback)));
}

void MahoPasswordStoreBackend::DisableAutoSignInForOriginsAsync(
    const base::RepeatingCallback<bool(const GURL &)> &origin_filter,
    base::OnceClosure completion) {
  if (completion) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, std::move(completion));
  }
}

password_manager::SmartBubbleStatsStore *
MahoPasswordStoreBackend::GetSmartBubbleStatsStore() {
  return nullptr;
}

std::unique_ptr<syncer::DataTypeControllerDelegate>
MahoPasswordStoreBackend::CreateSyncControllerDelegate() {
  return nullptr;
}

// Convenience Maho-specific API implementations:
void MahoPasswordStoreBackend::EnsureAuthorized(
    const std::string &origin_scope, PasswordAuthorizationAction action,
    const std::u16string &message, base::OnceCallback<void(bool)> callback) {
  if (IsPasswordOperationAuthorized(profile_key_, origin_scope, action)) {
    std::move(callback).Run(true);
    return;
  }
  MahoPasswordAuthorizationService::Get()->AuthorizeForProfile(
      profile_key_, origin_scope, action, message, std::move(callback));
}

void MahoPasswordStoreBackend::ReadLoginsForOperationMahoAsync(
    const std::optional<std::string> &origin,
    const std::string &authorization_scope, PasswordAuthorizationAction action,
    std::optional<password_manager::PasswordForm::Scheme> scheme,
    LoginsOrErrorReply callback) {
  if (!enabled_ || !initialized_ || !callback) {
    if (callback) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback),
                                    std::vector<MahoPasswordForm>{}, false));
    }
    return;
  }
  if (!IsMahoVaultRoutingActive()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  std::vector<MahoPasswordForm>{}, true));
    return;
  }
  EnsureAuthorized(
      authorization_scope, action, u"Authenticate to access passwords",
      base::BindOnce(
          [](base::WeakPtr<MahoPasswordStoreBackend> backend,
             std::optional<std::string> read_origin, std::string scope,
             PasswordAuthorizationAction read_action,
             std::optional<password_manager::PasswordForm::Scheme> read_scheme,
             LoginsOrErrorReply cb, bool authorized) {
            if (!backend || !authorized ||
                !IsPasswordOperationAuthorized(backend->profile_key_, scope,
                                               read_action)) {
              std::move(cb).Run(std::vector<MahoPasswordForm>{}, false);
              return;
            }
            std::vector<MahoPasswordForm> results;
            if (!ReadBackendCredentials(read_origin, backend->profile_key_,
                                        read_scheme, results)) {
              std::move(cb).Run(std::vector<MahoPasswordForm>{}, false);
              return;
            }
            std::move(cb).Run(std::move(results), true);
          },
          weak_ptr_factory_.GetWeakPtr(), origin, authorization_scope, action,
          scheme, std::move(callback)));
}

void MahoPasswordStoreBackend::GetAllLoginsMahoAsync(
    LoginsOrErrorReply callback) {
  ReadLoginsForOperationMahoAsync(
      std::nullopt, MahoPasswordAuthorizationService::kAllOriginsScope,
      PasswordAuthorizationAction::kReadAll, std::nullopt,
      std::move(callback));
}

void MahoPasswordStoreBackend::GetAutofillableLoginsMahoAsync(
    LoginsOrErrorReply callback) {
  GetAllLoginsMahoAsync(base::BindOnce(
      [](LoginsOrErrorReply cb, std::vector<MahoPasswordForm> forms, bool success) {
        if (success) {
          std::erase_if(forms, [](const MahoPasswordForm &form) {
            return form.blocked_by_user;
          });
        }
        std::move(cb).Run(std::move(forms), success);
      },
      std::move(callback)));
}

void MahoPasswordStoreBackend::FillMatchingLoginsMahoAsync(
    const std::string &signon_realm,
    password_manager::PasswordForm::Scheme scheme,
    LoginsOrErrorReply callback) {
  if (!NativeWriteEnabled()) {
    if (callback) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback),
                                    std::vector<MahoPasswordForm>{}, true));
    }
    return;
  }
  ReadLoginsForOperationMahoAsync(signon_realm, signon_realm,
                                  PasswordAuthorizationAction::kFill, scheme,
                                  std::move(callback));
}

void MahoPasswordStoreBackend::AddLoginMahoAsync(
    const MahoPasswordForm &form, PasswordChangesOrErrorReply callback) {
  if (!enabled_ || !initialized_ || !callback) {
    if (callback) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), false));
    }
    return;
  }
  if (!NativeWriteEnabled() || !IsMahoVaultRoutingActive()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), true));
    return;
  }
  AddLoginWithResultMahoAsync(form,
                              base::BindOnce(
                                  [](PasswordChangesOrErrorReply cb,
                                     std::optional<MahoPasswordForm> snapshot) {
                                    if (snapshot) {
                                      snapshot->Zeroize();
                                    }
                                    std::move(cb).Run(snapshot.has_value());
                                  },
                                  std::move(callback)));
}

void MahoPasswordStoreBackend::AddLoginWithResultMahoAsync(
    const MahoPasswordForm &form, MutationResultReply callback) {
  if (!enabled_ || !initialized_ || !callback) {
    if (callback) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), std::nullopt));
    }
    return;
  }

  if (!NativeWriteEnabled() || !IsMahoVaultRoutingActive()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback), std::optional<MahoPasswordForm>()));
    return;
  }

  if (!IsPasswordOperationAuthorized(profile_key_, form.signon_realm,
                                     PasswordAuthorizationAction::kAdd)) {
        EnsureAuthorized(
        form.signon_realm, PasswordAuthorizationAction::kAdd,
        u"Authenticate to save this password",
        base::BindOnce(
            [](base::WeakPtr<MahoPasswordStoreBackend> backend,
               MahoPasswordForm pending_form, MutationResultReply cb,
               bool authorized) {
              if (!backend || !authorized) {
                                std::move(cb).Run(std::nullopt);
                return;
              }
              backend->AddLoginWithResultMahoAsync(pending_form, std::move(cb));
            },
            weak_ptr_factory_.GetWeakPtr(), form, std::move(callback)));
    return;
  }

  MahoCore *core = maho::GetCore();
  if (!core) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), std::nullopt));
    return;
  }

  MahoPasswordForm mutable_form = form;
  std::optional<MahoMutationResult> result;
  std::optional<base::DictValue> metadata = BuildLoginMetadata(mutable_form);
  if (!metadata) {
        mutable_form.Zeroize();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), std::nullopt));
    return;
  }

  base::DictValue req;
  req.Set("metadata", std::move(*metadata));
  req.Set("username", mutable_form.username_value);
  req.Set("password", mutable_form.password_value);
  // Task 4: everything the public metadata cannot express travels in
  // `formDetails`, inside the encrypted record.
  req.Set("formDetails", BuildFormDetails(mutable_form));

  std::string req_json;
  base::JSONWriter::Write(req, &req_json);
  // `req` owns its OWN copy of the plaintext password, separate from both
  // `req_json` and the form. Wipe it here, while the dictionary is still
  // alive, so the allocation is not released with the secret intact.
  if (std::string *req_password = req.FindString("password")) {
    SecureZeroizeString(*req_password);
  }
  if (!IsMahoVaultRoutingActive() ||
      !IsPasswordOperationAuthorized(profile_key_, mutable_form.signon_realm,
                                     PasswordAuthorizationAction::kAdd)) {
    SecureZeroizeString(req_json);
    mutable_form.Zeroize();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), std::nullopt));
    return;
  }
  char *add_res = maho_vault_add_login_json(core, req_json.c_str());
  SecureZeroizeString(req_json);

  if (add_res) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(add_res, base::JSON_PARSE_RFC);
    maho_string_free(add_res);
    if (parsed) {
      result = ParseMutationResult(*parsed);
    }
  }

  if (result) {
    mutable_form.id = result->id;
    mutable_form.revision = result->revision;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback), std::move(mutable_form)));
    return;
  }
  mutable_form.Zeroize();
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::nullopt));
}

void MahoPasswordStoreBackend::UpdateLoginMahoAsync(
    const MahoPasswordForm &form, PasswordChangesOrErrorReply callback) {
  if (!enabled_ || !initialized_ || !callback) {
    if (callback) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), false));
    }
    return;
  }
  if (!NativeWriteEnabled() || !IsMahoVaultRoutingActive()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), true));
    return;
  }
  UpdateLoginWithResultMahoAsync(
      form, base::BindOnce(
                [](PasswordChangesOrErrorReply cb,
                   std::optional<MahoPasswordForm> snapshot) {
                  if (snapshot) {
                    snapshot->Zeroize();
                  }
                  std::move(cb).Run(snapshot.has_value());
                },
                std::move(callback)));
}

void MahoPasswordStoreBackend::UpdateLoginWithResultMahoAsync(
    const MahoPasswordForm &form, MutationResultReply callback) {
  if (!enabled_ || !initialized_ || !callback) {
    if (callback) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), std::nullopt));
    }
    return;
  }

  if (!NativeWriteEnabled() || !IsMahoVaultRoutingActive()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback), std::optional<MahoPasswordForm>()));
    return;
  }

  if (!IsPasswordOperationAuthorized(profile_key_, form.signon_realm,
                                     PasswordAuthorizationAction::kUpdate)) {
    EnsureAuthorized(
        form.signon_realm, PasswordAuthorizationAction::kUpdate,
        u"Authenticate to update this password",
        base::BindOnce(
            [](base::WeakPtr<MahoPasswordStoreBackend> backend,
               MahoPasswordForm pending_form, MutationResultReply cb,
               bool authorized) {
              if (!backend || !authorized) {
                std::move(cb).Run(std::nullopt);
                return;
              }
              backend->UpdateLoginWithResultMahoAsync(pending_form,
                                                      std::move(cb));
            },
            weak_ptr_factory_.GetWeakPtr(), form, std::move(callback)));
    return;
  }

  MahoCore *core = maho::GetCore();
  if (!core || form.id.empty()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), std::nullopt));
    return;
  }

  MahoPasswordForm mutable_form = form;
  // Task 4: a caller that knows the UUID but not the revision (e.g. a form that
  // never came back through a read on THIS call) still mutates the right row at
  // the revision this backend last observed, instead of sending 0 and losing
  // the CAS.
  if (mutable_form.revision == 0) {
    if (std::optional<uint64_t> revision =
            identity_map_.RevisionForVaultId(mutable_form.id)) {
      mutable_form.revision = *revision;
    }
  }
  std::optional<MahoMutationResult> result;

  base::DictValue req;
  req.Set("id", mutable_form.id);
  if (!SetExpectedRevision(req, mutable_form.revision)) {
    mutable_form.Zeroize();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), std::nullopt));
    return;
  }
  std::optional<base::DictValue> metadata = BuildLoginMetadata(mutable_form);
  if (!metadata) {
    mutable_form.Zeroize();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), std::nullopt));
    return;
  }
  req.Set("metadata", std::move(*metadata));
  req.Set("username", mutable_form.username_value);
  if (!mutable_form.password_value.empty()) {
    req.Set("password", mutable_form.password_value);
  }
  req.Set("formDetails", BuildFormDetails(mutable_form));
  std::string req_json;
  base::JSONWriter::Write(req, &req_json);
  if (!IsMahoVaultRoutingActive() ||
      !IsPasswordOperationAuthorized(profile_key_, mutable_form.signon_realm,
                                     PasswordAuthorizationAction::kUpdate)) {
    SecureZeroizeString(req_json);
    mutable_form.Zeroize();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), std::nullopt));
    return;
  }
  char *update_res = maho_vault_update_login_json(core, req_json.c_str());
  SecureZeroizeString(req_json);
  if (update_res) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(update_res, base::JSON_PARSE_RFC);
    maho_string_free(update_res);
    if (parsed) {
      result = ParseMutationResult(*parsed);
    }
  }

  if (result) {
    mutable_form.id = result->id;
    mutable_form.revision = result->revision;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback), std::move(mutable_form)));
    return;
  }
  mutable_form.Zeroize();
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::nullopt));
}

void MahoPasswordStoreBackend::RemoveLoginMahoAsync(
    const MahoPasswordForm &form, PasswordChangesOrErrorReply callback) {
  if (!enabled_ || !initialized_ || !callback) {
    if (callback) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), false));
    }
    return;
  }

  if (!IsMahoVaultRoutingActive()) {
    // External/disabled provider: no-op removal, no Vault FFI call.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), true));
    return;
  }

  if (!IsPasswordOperationAuthorized(profile_key_, form.signon_realm,
                                     PasswordAuthorizationAction::kDelete)) {
    EnsureAuthorized(
        form.signon_realm, PasswordAuthorizationAction::kDelete,
        u"Authenticate to delete this password",
        base::BindOnce(
            [](base::WeakPtr<MahoPasswordStoreBackend> backend,
               MahoPasswordForm pending_form, PasswordChangesOrErrorReply cb,
               bool authorized) {
              if (!backend || !authorized) {
                std::move(cb).Run(false);
                return;
              }
              backend->RemoveLoginMahoAsync(pending_form, std::move(cb));
            },
            weak_ptr_factory_.GetWeakPtr(), form, std::move(callback)));
    return;
  }

  MahoCore *core = maho::GetCore();
  if (!core || form.id.empty()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), false));
    return;
  }

  uint64_t revision = form.revision;
  if (revision == 0) {
    if (std::optional<uint64_t> observed =
            identity_map_.RevisionForVaultId(form.id)) {
      revision = *observed;
    }
  }

  bool success = false;
  base::DictValue req;
  req.Set("id", form.id);
  if (!SetExpectedRevision(req, revision)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), false));
    return;
  }
  std::string req_json;
  base::JSONWriter::Write(req, &req_json);
  if (!IsMahoVaultRoutingActive() ||
      !IsPasswordOperationAuthorized(profile_key_, form.signon_realm,
                                     PasswordAuthorizationAction::kDelete)) {
    SecureZeroizeString(req_json);
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), false));
    return;
  }
  char *del_res = maho_vault_delete_item_json(core, req_json.c_str());
  SecureZeroizeString(req_json);

  if (del_res) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(del_res, base::JSON_PARSE_RFC);
    maho_string_free(del_res);
    if (parsed && parsed->is_dict() &&
        parsed->GetDict().FindBool("ok").value_or(false)) {
      success = true;
    }
  }

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), success));
}

void MahoPasswordStoreBackend::RemoveLoginsCreatedBetweenMahoAsync(
    base::Time delete_begin, base::Time delete_end,
    PasswordChangesOrErrorReply callback) {
  if (!enabled_ || !initialized_ || !callback) {
    if (callback) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), false));
    }
    return;
  }

  if (!IsMahoVaultRoutingActive()) {
    // External/disabled provider: nothing of Maho's to delete.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), true));
    return;
  }

  ReadLoginsForOperationMahoAsync(
      std::nullopt, MahoPasswordAuthorizationService::kRangeDeleteScope,
      PasswordAuthorizationAction::kRangeDelete, std::nullopt,
      base::BindOnce(
          [](base::WeakPtr<MahoPasswordStoreBackend> backend,
             base::Time delete_begin, base::Time delete_end,
             PasswordChangesOrErrorReply cb,
             std::vector<MahoPasswordForm> forms, bool read_ok) {
            if (!backend || !read_ok) {
              std::move(cb).Run(false);
              return;
            }
            bool all_removed = true;
            for (const auto &form : forms) {
              if ((delete_begin.is_null() ||
                   form.date_created >= delete_begin) &&
                  (delete_end.is_null() || form.date_created <= delete_end)) {
                if (!form.id.empty()) {
                  if (!IsPasswordOperationAuthorized(
                          backend->profile_key_,
                          MahoPasswordAuthorizationService::kRangeDeleteScope,
                          PasswordAuthorizationAction::kRangeDelete)) {
                    all_removed = false;
                    break;
                  }
                  MahoCore *core = maho::GetCore();
                  if (core) {
                    base::DictValue req;
                    req.Set("id", form.id);
                    if (!SetExpectedRevision(req, form.revision)) {
                      all_removed = false;
                      continue;
                    }
                    std::string req_json;
                    base::JSONWriter::Write(req, &req_json);
                    if (!backend->IsMahoVaultRoutingActive() ||
                        !IsPasswordOperationAuthorized(
                            backend->profile_key_,
                            MahoPasswordAuthorizationService::kRangeDeleteScope,
                            PasswordAuthorizationAction::kRangeDelete)) {
                      SecureZeroizeString(req_json);
                      all_removed = false;
                      break;
                    }
                    char *del_res =
                        maho_vault_delete_item_json(core, req_json.c_str());
                    SecureZeroizeString(req_json);
                    if (del_res) {
                      std::optional<base::Value> parsed =
                          base::JSONReader::Read(del_res, base::JSON_PARSE_RFC);
                      maho_string_free(del_res);
                      if (!parsed || !parsed->is_dict() ||
                          !parsed->GetDict().FindBool("ok").value_or(false)) {
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
          weak_ptr_factory_.GetWeakPtr(), delete_begin, delete_end,
          std::move(callback)));
}

} // namespace passwords
} // namespace maho
