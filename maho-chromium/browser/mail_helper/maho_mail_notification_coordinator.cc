// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_notification_coordinator.h"

#include <algorithm>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"

namespace maho {
namespace {

constexpr char kWatermarksPref[] = "maho.mail.notification_watermarks";
constexpr char kDedupePref[] = "maho.mail.notification_message_ids";
constexpr char kMutedThreadsPref[] = "maho.mail.notification_muted_threads";
constexpr char kNotificationsEnabledPref[] = "maho.mail.notifications_enabled";
constexpr size_t kMaxDurableMessageIds = 512;

std::string NotificationId(const MahoMailNotificationEvent& event) {
  return "maho-mail-new-" +
         base::Uuid::GenerateRandomV4().AsLowercaseString();
}

}  // namespace

MahoMailNotificationEvent::MahoMailNotificationEvent() = default;

MahoMailNotificationEvent::MahoMailNotificationEvent(
    std::string account_id,
    std::string email_id,
    std::string message_id,
    std::string sender,
    std::string subject,
    uint64_t cursor,
    uint64_t epoch,
    uint64_t generation)
    : account_id(std::move(account_id)),
      email_id(std::move(email_id)),
      message_id(std::move(message_id)),
      sender(std::move(sender)),
      subject(std::move(subject)),
      cursor(cursor),
      epoch(epoch),
      generation(generation) {}

MahoMailNotificationEvent::~MahoMailNotificationEvent() = default;

MahoMailNotificationEvent::MahoMailNotificationEvent(
    const MahoMailNotificationEvent&) = default;

MahoMailNotificationEvent& MahoMailNotificationEvent::operator=(
    const MahoMailNotificationEvent&) = default;

MahoMailNotificationEvent::MahoMailNotificationEvent(
    MahoMailNotificationEvent&&) = default;

MahoMailNotificationEvent& MahoMailNotificationEvent::operator=(
    MahoMailNotificationEvent&&) = default;

MahoMailNotificationPayload::MahoMailNotificationPayload() = default;

MahoMailNotificationPayload::~MahoMailNotificationPayload() = default;

MahoMailNotificationPayload::MahoMailNotificationPayload(
    const MahoMailNotificationPayload&) = default;

MahoMailNotificationPayload& MahoMailNotificationPayload::operator=(
    const MahoMailNotificationPayload&) = default;

MahoMailNotificationPayload::MahoMailNotificationPayload(
    MahoMailNotificationPayload&&) = default;

MahoMailNotificationPayload& MahoMailNotificationPayload::operator=(
    MahoMailNotificationPayload&&) = default;

MahoMailNotificationCoordinator::PendingNotification::PendingNotification(
    std::string account_id,
    std::string email_id,
    std::string message_key,
    uint64_t epoch,
    uint64_t cursor,
    uint64_t generation)
    : account_id(std::move(account_id)),
      email_id(std::move(email_id)),
      message_key(std::move(message_key)),
      epoch(epoch),
      cursor(cursor),
      generation(generation) {}

MahoMailNotificationCoordinator::PendingNotification::~PendingNotification() =
    default;

MahoMailNotificationCoordinator::PendingNotification::PendingNotification(
    PendingNotification&&) = default;

MahoMailNotificationCoordinator::PendingNotification&
MahoMailNotificationCoordinator::PendingNotification::operator=(
    PendingNotification&&) = default;

MahoMailNotificationCoordinator::MahoMailNotificationCoordinator(
    Profile* profile,
    MahoMailService* service,
    DisplayCallback display_callback,
    WithdrawCallback withdraw_callback,
    RouteCallback route_callback,
    BehaviorCallback behavior_callback,
    PermissionCallback permission_callback)
    : MahoMailNotificationCoordinator(
          profile ? profile->GetPrefs() : nullptr,
          service,
          std::move(display_callback),
          std::move(withdraw_callback),
          std::move(route_callback),
          std::move(behavior_callback),
          std::move(permission_callback)) {
  profile_ = profile;
}

MahoMailNotificationCoordinator::MahoMailNotificationCoordinator(
    PrefService* prefs,
    MahoMailService* service,
    DisplayCallback display_callback,
    WithdrawCallback withdraw_callback,
    RouteCallback route_callback,
    BehaviorCallback behavior_callback,
    PermissionCallback permission_callback)
    : prefs_(prefs),
      service_(service),
      display_callback_(std::move(display_callback)),
      withdraw_callback_(std::move(withdraw_callback)),
      route_callback_(std::move(route_callback)),
      behavior_callback_(std::move(behavior_callback)),
      permission_callback_(std::move(permission_callback)) {
  LoadState();
  if (service_) {
    service_->AddObserver(this);
  }
  if (prefs_) {
    pref_registrar_.Init(prefs_);
    if (prefs_->FindPreference(sidebar_prefs::kMahoMailEnabled)) {
      pref_registrar_.Add(
          sidebar_prefs::kMahoMailEnabled,
          base::BindRepeating(
              &MahoMailNotificationCoordinator::OnGatePrefChanged,
              weak_ptr_factory_.GetWeakPtr()));
    }
    if (prefs_->FindPreference(kNotificationsEnabledPref)) {
      pref_registrar_.Add(
          kNotificationsEnabledPref,
          base::BindRepeating(
              &MahoMailNotificationCoordinator::OnGatePrefChanged,
              weak_ptr_factory_.GetWeakPtr()));
    }
  }
}

MahoMailNotificationCoordinator::~MahoMailNotificationCoordinator() {
  destroyed_ = true;
  weak_ptr_factory_.InvalidateWeakPtrs();
  pref_registrar_.RemoveAll();
  if (service_) {
    service_->RemoveObserver(this);
  }
  WithdrawAll();
}

// static
void MahoMailNotificationCoordinator::RegisterProfilePrefs(
    PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kWatermarksPref);
  registry->RegisterListPref(kDedupePref);
  registry->RegisterListPref(kMutedThreadsPref);
}

void MahoMailNotificationCoordinator::OnNewMail(
    const std::string& account_id,
    const std::string& email_id,
    const std::string& message_id,
    const std::string& sender,
    const std::string& subject,
    uint64_t cursor,
    uint64_t epoch) {
  HandleEvent({account_id, email_id, message_id, sender, subject, cursor,
               epoch, service_ ? service_->generation() : 0});
}

void MahoMailNotificationCoordinator::OnAccountsChanged() {
  // Account identity cannot be inferred from this untyped lifecycle event.
  // The account-removal owner calls RemoveAccount with
  // the exact id; a general refresh must not withdraw another account's mail.
}

void MahoMailNotificationCoordinator::OnLifecycleChanged(
    MahoMailService::LifecycleState state,
    uint64_t generation) {
  ++lifecycle_revision_;
  lifecycle_observed_ = true;
  lifecycle_state_ = state;
  lifecycle_generation_ = generation;
  if (state != MahoMailService::LifecycleState::kReady) {
    WithdrawAll();
    return;
  }
  for (auto it = active_.begin(); it != active_.end();) {
    if (it->second.generation == generation) {
      ++it;
      continue;
    }
    if (withdraw_callback_) {
      withdraw_callback_.Run(it->first);
    }
    it = active_.erase(it);
  }
}

void MahoMailNotificationCoordinator::OnAccountRemoved(
    const std::string& account_id) {
  ++account_revisions_[account_id];
  RemoveAccount(account_id);
}

void MahoMailNotificationCoordinator::OnThreadMuteChanged(
    const std::string& account_id,
    const std::string& message_id,
    bool muted) {
  const std::string key = DedupeKey(account_id, message_id);
  if (muted) {
    muted_thread_keys_.insert(key);
  } else {
    muted_thread_keys_.erase(key);
  }
  PersistState();
}

void MahoMailNotificationCoordinator::HandleEventForTesting(
    const MahoMailNotificationEvent& event) {
  HandleEvent(event);
}

void MahoMailNotificationCoordinator::HandleEvent(
    const MahoMailNotificationEvent& event) {
  if (destroyed_ || event.account_id.empty() || event.email_id.empty() ||
      event.message_id.empty() || !FeatureEnabled()) {
    return;
  }
  const MailBehaviorSnapshot behavior = CurrentBehavior();

  if (!behavior.desktop_notifications ||
      IsMuted(event.account_id, event.message_id, behavior)) {
    return;
  }
  auto callback = base::BindOnce(
      &MahoMailNotificationCoordinator::OnPermissionForEvent,
      weak_ptr_factory_.GetWeakPtr(), event, lifecycle_revision_,
      gate_revision_, account_revisions_[event.account_id]);
  if (permission_callback_) {
    permission_callback_.Run(std::move(callback));
  } else {
    GetMahoMailNotificationPermission(std::move(callback));
  }
}

void MahoMailNotificationCoordinator::OnPermissionForEvent(
    MahoMailNotificationEvent event,
    uint64_t lifecycle_revision,
    uint64_t gate_revision,
    uint64_t account_revision,
    MailOsNotificationPermission permission) {
  if (permission != MailOsNotificationPermission::kGranted) {
    return;
  }
  if (destroyed_ || !FeatureEnabled() ||
      lifecycle_revision != lifecycle_revision_ ||
      gate_revision != gate_revision_ ||
      account_revision != account_revisions_[event.account_id]) {
    return;
  }
  const MailBehaviorSnapshot behavior = CurrentBehavior();
  if (!behavior.desktop_notifications ||
      IsMuted(event.account_id, event.message_id, behavior)) {
    return;
  }
  if (!IsReadyForGeneration(event.generation)) {
    return;
  }
  const auto watermark = ordering_watermarks_.find(event.account_id);
  if (watermark != ordering_watermarks_.end()) {
    if (event.epoch < watermark->second.epoch ||
        (event.epoch == watermark->second.epoch &&
         event.cursor <= watermark->second.cursor)) {
      return;
    }
  }
  const std::string message_key = event.account_id + "\n" +
                                  base::NumberToString(event.epoch) + "\n" +
                                  event.message_id;
  if (message_keys_.contains(message_key) ||
      pending_message_keys_.contains(message_key)) {
    return;
  }

  MahoMailNotificationPayload payload;
  payload.notification_id = NotificationId(event);
  payload.account_id = event.account_id;
  payload.email_id = event.email_id;
  switch (behavior.notification_preview) {
    case MailNotificationPreview::kSenderSubject:
      payload.title = event.sender.empty() ? "New mail" : event.sender;
      payload.body = event.subject.empty() ? "(no subject)" : event.subject;
      break;
    case MailNotificationPreview::kSenderOnly:
      payload.title = event.sender.empty() ? "New mail" : event.sender;
      payload.body = "New message";
      break;
    case MailNotificationPreview::kGeneric:
      payload.title = "New mail";
      payload.body = "You have a new message";
      break;
  }

  payload.click_callback = base::BindRepeating(
      &MahoMailNotificationCoordinator::HandleClick,
      weak_ptr_factory_.GetWeakPtr(), payload.notification_id);
  if (!display_callback_) {
    return;
  }
  pending_.insert_or_assign(
      payload.notification_id,
      PendingNotification(event.account_id, event.email_id, message_key,
                          event.epoch, event.cursor, event.generation));
  pending_message_keys_.insert(message_key);
  display_callback_.Run(
      payload,
      base::BindOnce(
          [](base::WeakPtr<MahoMailNotificationCoordinator> coordinator,
             WithdrawCallback withdraw_callback,
             std::string notification_id, bool displayed) {
            if (coordinator) {
              coordinator->OnDisplayComplete(notification_id, displayed);
            } else if (displayed && withdraw_callback) {
              withdraw_callback.Run(notification_id);
            }
          },
          weak_ptr_factory_.GetWeakPtr(), withdraw_callback_,
          payload.notification_id));
}

void MahoMailNotificationCoordinator::OnDisplayComplete(
    const std::string& notification_id,
    bool displayed) {
  auto pending = pending_.find(notification_id);
  if (pending == pending_.end()) {
    if (displayed && withdraw_callback_) {
      withdraw_callback_.Run(notification_id);
    }
    return;
  }
  PendingNotification notification = std::move(pending->second);
  pending_.erase(pending);
  pending_message_keys_.erase(notification.message_key);
  if (!displayed) {
    if (withdraw_callback_) {
      withdraw_callback_.Run(notification_id);
    }
    return;
  }
  if (!IsReadyForGeneration(notification.generation)) {
    if (withdraw_callback_) {
      withdraw_callback_.Run(notification_id);
    }
    return;
  }

  active_[notification_id] = {notification.account_id, notification.email_id,
                              notification.generation};
  const auto watermark = ordering_watermarks_.find(notification.account_id);
  if (watermark == ordering_watermarks_.end() ||
      notification.epoch > watermark->second.epoch ||
      (notification.epoch == watermark->second.epoch &&
       notification.cursor > watermark->second.cursor)) {
    ordering_watermarks_[notification.account_id] = {notification.epoch,
                                                     notification.cursor};
  }
  message_keys_.insert(notification.message_key);
  while (message_keys_.size() > kMaxDurableMessageIds) {
    message_keys_.erase(message_keys_.begin());
  }
  PersistState();
}

void MahoMailNotificationCoordinator::RemoveAccount(
    const std::string& account_id) {
  for (auto it = active_.begin(); it != active_.end();) {
    if (it->second.account_id != account_id) {
      ++it;
      continue;
    }
    if (withdraw_callback_) {
      withdraw_callback_.Run(it->first);
    }
    it = active_.erase(it);
  }
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (it->second.account_id != account_id) {
      ++it;
      continue;
    }
    pending_message_keys_.erase(it->second.message_key);
    it = pending_.erase(it);
  }
  ordering_watermarks_.erase(account_id);
  const std::string prefix = account_id + "\n";
  std::erase_if(message_keys_, [&prefix](const std::string& key) {
    return key.starts_with(prefix);
  });
  std::erase_if(muted_thread_keys_, [&prefix](const std::string& key) {
    return key.starts_with(prefix);
  });
  PersistState();
}

void MahoMailNotificationCoordinator::WithdrawAll() {
  for (const auto& [notification_id, ignored] : active_) {
    if (withdraw_callback_) {
      withdraw_callback_.Run(notification_id);
    }
  }
  active_.clear();
  pending_.clear();
  pending_message_keys_.clear();
}

void MahoMailNotificationCoordinator::OnGatePrefChanged() {
  ++gate_revision_;
  const bool notifications_enabled =
      !prefs_ || !prefs_->FindPreference(kNotificationsEnabledPref) ||
      prefs_->GetBoolean(kNotificationsEnabledPref);
  if (!FeatureEnabled() || !notifications_enabled ||
      !CurrentBehavior().desktop_notifications) {
    WithdrawAll();
  }
}

void MahoMailNotificationCoordinator::HandleClick(
    const std::string& notification_id) {
  const auto it = active_.find(notification_id);
  if (destroyed_ || it == active_.end() || !FeatureEnabled() ||
      !CurrentBehavior().desktop_notifications) {
    return;
  }
  auto callback = base::BindOnce(
      &MahoMailNotificationCoordinator::OnPermissionForClick,
      weak_ptr_factory_.GetWeakPtr(), notification_id);
  if (permission_callback_) {
    permission_callback_.Run(std::move(callback));
  } else {
    GetMahoMailNotificationPermission(std::move(callback));
  }
}

void MahoMailNotificationCoordinator::OnPermissionForClick(
    const std::string& notification_id,
    MailOsNotificationPermission permission) {
  const auto it = active_.find(notification_id);
  if (permission != MailOsNotificationPermission::kGranted || destroyed_ ||
      it == active_.end() || !FeatureEnabled() ||
      !CurrentBehavior().desktop_notifications ||
      !IsReadyForGeneration(it->second.generation)) {
    return;
  }
  if (route_callback_) {
    route_callback_.Run(it->second.account_id, it->second.email_id);
  }
}

MailBehaviorSnapshot MahoMailNotificationCoordinator::CurrentBehavior() const {
  MailBehaviorSnapshot snapshot =
      behavior_callback_ ? behavior_callback_.Run() : MailBehaviorSnapshot();
  if (prefs_ && prefs_->FindPreference(kNotificationsEnabledPref)) {
    // The service snapshot may still have defaults before its first behavior
    // read. A persisted opt-out must also gate notification delivery.
    snapshot.desktop_notifications =
        snapshot.desktop_notifications &&
        prefs_->GetBoolean(kNotificationsEnabledPref);
  }
  return snapshot;
}

bool MahoMailNotificationCoordinator::FeatureEnabled() const {
  if (!prefs_ || !prefs_->FindPreference(sidebar_prefs::kMahoMailEnabled)) {
    return true;
  }
  return sidebar_prefs::IsMahoMailEnabled(prefs_);
}

bool MahoMailNotificationCoordinator::IsReadyForGeneration(
    uint64_t generation) const {
  if (service_) {
    return service_->lifecycle_state() ==
               MahoMailService::LifecycleState::kReady &&
           generation == service_->generation();
  }
  return !lifecycle_observed_ ||
         (lifecycle_state_ == MahoMailService::LifecycleState::kReady &&
          generation == lifecycle_generation_);
}

void MahoMailNotificationCoordinator::PersistState() {
  if (!prefs_) {
    return;
  }
  base::DictValue watermarks;
  for (const auto& [account_id, watermark] : ordering_watermarks_) {
    base::DictValue value;
    value.Set("epoch", base::NumberToString(watermark.epoch));
    value.Set("cursor", base::NumberToString(watermark.cursor));
    watermarks.Set(account_id, std::move(value));
  }
  prefs_->SetDict(kWatermarksPref, std::move(watermarks));
  base::ListValue ids;
  for (const std::string& message_key : message_keys_) {
    ids.Append(message_key);
  }
  prefs_->SetList(kDedupePref, std::move(ids));
  base::ListValue muted;
  for (const std::string& thread_key : muted_thread_keys_) {
    muted.Append(thread_key);
  }
  prefs_->SetList(kMutedThreadsPref, std::move(muted));
}

void MahoMailNotificationCoordinator::LoadState() {
  if (!prefs_) {
    return;
  }
  if (prefs_->FindPreference(kWatermarksPref)) {
    for (const auto [account_id, value] : prefs_->GetDict(kWatermarksPref)) {
      if (!value.is_dict()) {
        continue;
      }
      uint64_t epoch = 0;
      uint64_t cursor = 0;
      const std::string* epoch_text = value.GetDict().FindString("epoch");
      const std::string* cursor_text = value.GetDict().FindString("cursor");
      if (epoch_text && cursor_text &&
          base::StringToUint64(*epoch_text, &epoch) &&
          base::StringToUint64(*cursor_text, &cursor)) {
        ordering_watermarks_[account_id] = {epoch, cursor};
      }
    }
  }
  if (prefs_->FindPreference(kDedupePref)) {
    for (const base::Value& value : prefs_->GetList(kDedupePref)) {
      if (value.is_string()) {
        message_keys_.insert(value.GetString());
      }
    }
  }
  if (prefs_->FindPreference(kMutedThreadsPref)) {
    for (const base::Value& value : prefs_->GetList(kMutedThreadsPref)) {
      if (value.is_string()) {
        muted_thread_keys_.insert(value.GetString());
      }
    }
  }
}

// static
std::string MahoMailNotificationCoordinator::DedupeKey(
    const std::string& account_id,
    const std::string& message_id) {
  return account_id + "\n" + message_id;
}

bool MahoMailNotificationCoordinator::IsMuted(
    const std::string& account_id,
    const std::string& message_id,
    const MailBehaviorSnapshot& behavior) const {
  if (muted_thread_keys_.contains(DedupeKey(account_id, message_id))) {
    return true;
  }
  return std::find(behavior.muted_thread_ids.begin(),
                   behavior.muted_thread_ids.end(), message_id) !=
         behavior.muted_thread_ids.end();
}

bool MahoMailNotificationCoordinatorRegistry::Ensure(
    Profile* profile,
    const Factory& factory) {
  if (!profile || coordinators_.contains(profile) || !factory) {
    return false;
  }
  std::unique_ptr<MahoMailNotificationCoordinator> coordinator =
      factory.Run(profile);
  if (!coordinator) {
    return false;
  }
  coordinators_.emplace(profile, std::move(coordinator));
  return true;
}

MahoMailNotificationCoordinatorRegistry::
    MahoMailNotificationCoordinatorRegistry() = default;

MahoMailNotificationCoordinatorRegistry::
    ~MahoMailNotificationCoordinatorRegistry() = default;

void MahoMailNotificationCoordinatorRegistry::Clear() {
  coordinators_.clear();
}

}  // namespace maho
