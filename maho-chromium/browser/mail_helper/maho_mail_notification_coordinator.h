// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_NOTIFICATION_COORDINATOR_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_NOTIFICATION_COORDINATOR_H_

#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/prefs/pref_change_registrar.h"
#include "maho/browser/mail_helper/maho_mail_service.h"

class PrefService;
class PrefRegistrySimple;
class Profile;

namespace maho {

struct MahoMailNotificationEvent {
  MahoMailNotificationEvent();
  MahoMailNotificationEvent(std::string account_id,
                            std::string email_id,
                            std::string message_id,
                            std::string sender,
                            std::string subject,
                            uint64_t cursor,
                            uint64_t epoch,
                            uint64_t generation);
  ~MahoMailNotificationEvent();
  MahoMailNotificationEvent(const MahoMailNotificationEvent&);
  MahoMailNotificationEvent& operator=(const MahoMailNotificationEvent&);
  MahoMailNotificationEvent(MahoMailNotificationEvent&&);
  MahoMailNotificationEvent& operator=(MahoMailNotificationEvent&&);

  std::string account_id;
  std::string email_id;
  std::string message_id;
  std::string sender;
  std::string subject;
  uint64_t cursor = 0;
  uint64_t epoch = 0;
  uint64_t generation = 0;
};

struct MahoMailNotificationPayload {
  MahoMailNotificationPayload();
  ~MahoMailNotificationPayload();
  MahoMailNotificationPayload(const MahoMailNotificationPayload&);
  MahoMailNotificationPayload& operator=(
      const MahoMailNotificationPayload&);
  MahoMailNotificationPayload(MahoMailNotificationPayload&&);
  MahoMailNotificationPayload& operator=(MahoMailNotificationPayload&&);

  std::string notification_id;
  std::string account_id;
  std::string email_id;
  std::string title;
  std::string body;
  base::RepeatingClosure click_callback;
};

class MahoMailNotificationCoordinator : public MahoMailService::Observer {
 public:
  using DisplayCallback =
      base::RepeatingCallback<void(const MahoMailNotificationPayload&,
                                   base::OnceCallback<void(bool)>)>;
  using WithdrawCallback = base::RepeatingCallback<void(const std::string&)>;
  using RouteCallback =
      base::RepeatingCallback<void(const std::string&, const std::string&)>;
  using BehaviorCallback = base::RepeatingCallback<MailBehaviorSnapshot()>;
  using PermissionCallback =
      base::RepeatingCallback<void(
          base::OnceCallback<void(MailOsNotificationPermission)>)>;

  MahoMailNotificationCoordinator(Profile* profile,
                                  MahoMailService* service,
                                  DisplayCallback display_callback,
                                  WithdrawCallback withdraw_callback,
                                  RouteCallback route_callback,
                                  BehaviorCallback behavior_callback = {},
                                  PermissionCallback permission_callback = {});
  MahoMailNotificationCoordinator(PrefService* prefs,
                                  MahoMailService* service,
                                  DisplayCallback display_callback,
                                  WithdrawCallback withdraw_callback,
                                  RouteCallback route_callback,
                                  BehaviorCallback behavior_callback,
                                  PermissionCallback permission_callback = {});
  ~MahoMailNotificationCoordinator() override;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  void OnNewMail(const std::string& account_id,
                 const std::string& email_id,
                 const std::string& message_id,
                 const std::string& sender,
                 const std::string& subject,
                 uint64_t cursor,
                 uint64_t epoch) override;
  void OnAccountsChanged() override;
  void OnLifecycleChanged(MahoMailService::LifecycleState state,
                          uint64_t generation) override;
  void OnAccountRemoved(const std::string& account_id) override;
  void OnThreadMuteChanged(const std::string& account_id,
                           const std::string& message_id,
                           bool muted) override;

  void RemoveAccount(const std::string& account_id);
  void HandleEventForTesting(const MahoMailNotificationEvent& event);
  void WithdrawAll();

 private:
  struct ActiveNotification {
    std::string account_id;
    std::string email_id;
    uint64_t generation = 0;
  };

  struct OrderingWatermark {
    uint64_t epoch = 0;
    uint64_t cursor = 0;
  };

  struct PendingNotification {
    PendingNotification(std::string account_id,
                        std::string email_id,
                        std::string message_key,
                        uint64_t epoch,
                        uint64_t cursor,
                        uint64_t generation);
    ~PendingNotification();
    PendingNotification(PendingNotification&&);
    PendingNotification& operator=(PendingNotification&&);

    std::string account_id;
    std::string email_id;
    std::string message_key;
    uint64_t epoch = 0;
    uint64_t cursor = 0;
    uint64_t generation = 0;
  };

  void HandleEvent(const MahoMailNotificationEvent& event);
  void OnPermissionForEvent(MahoMailNotificationEvent event,
                            uint64_t lifecycle_revision,
                            uint64_t gate_revision,
                            uint64_t account_revision,
                            MailOsNotificationPermission permission);
  void OnDisplayComplete(const std::string& notification_id, bool displayed);
  void OnGatePrefChanged();
  void HandleClick(const std::string& notification_id);
  void OnPermissionForClick(const std::string& notification_id,
                            MailOsNotificationPermission permission);
  MailBehaviorSnapshot CurrentBehavior() const;
  bool FeatureEnabled() const;
  bool IsReadyForGeneration(uint64_t generation) const;
  void PersistState();
  void LoadState();
  static std::string DedupeKey(const std::string& account_id,
                               const std::string& message_id);
  bool IsMuted(const std::string& account_id,
               const std::string& message_id,
               const MailBehaviorSnapshot& behavior) const;

  raw_ptr<Profile> profile_ = nullptr;
  raw_ptr<PrefService> prefs_ = nullptr;
  raw_ptr<MahoMailService> service_ = nullptr;
  DisplayCallback display_callback_;
  WithdrawCallback withdraw_callback_;
  RouteCallback route_callback_;
  BehaviorCallback behavior_callback_;
  PermissionCallback permission_callback_;
  PrefChangeRegistrar pref_registrar_;
  std::map<std::string, OrderingWatermark> ordering_watermarks_;
  std::set<std::string> message_keys_;
  std::set<std::string> muted_thread_keys_;
  std::map<std::string, ActiveNotification> active_;
  std::map<std::string, PendingNotification> pending_;
  std::set<std::string> pending_message_keys_;
  bool lifecycle_observed_ = false;
  MahoMailService::LifecycleState lifecycle_state_ =
      MahoMailService::LifecycleState::kStopped;
  uint64_t lifecycle_generation_ = 0;
  uint64_t lifecycle_revision_ = 0;
  uint64_t gate_revision_ = 0;
  std::map<std::string, uint64_t> account_revisions_;
  bool destroyed_ = false;
  base::WeakPtrFactory<MahoMailNotificationCoordinator> weak_ptr_factory_{this};
};

class MahoMailNotificationCoordinatorRegistry {
 public:
  MahoMailNotificationCoordinatorRegistry();
  ~MahoMailNotificationCoordinatorRegistry();

  using Factory = base::RepeatingCallback<
      std::unique_ptr<MahoMailNotificationCoordinator>(Profile*)>;

  bool Ensure(Profile* profile, const Factory& factory);
  void Clear();
  bool Contains(Profile* profile) const {
    return coordinators_.contains(profile);
  }
  size_t size() const { return coordinators_.size(); }

 private:
  std::map<Profile*, std::unique_ptr<MahoMailNotificationCoordinator>>
      coordinators_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_NOTIFICATION_COORDINATOR_H_
