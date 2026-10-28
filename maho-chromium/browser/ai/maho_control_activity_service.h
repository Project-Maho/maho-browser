// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_CONTROL_ACTIVITY_SERVICE_H_
#define MAHO_BROWSER_AI_MAHO_CONTROL_ACTIVITY_SERVICE_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/sequence_checker.h"
#include "base/time/default_tick_clock.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "components/keyed_service/core/keyed_service.h"
#include "url/origin.h"

class Profile;

namespace maho::ai {

void EnsureMahoControlActivityServiceFactoryBuilt();

enum class ControlActivityState {
  kReading,
  kActing,
  kWaitingApproval,
  kPaused,
  kDisconnected,
  kCompleted,
  kFailed,
};

// True for states after which an activity no longer controls its target
// (kDisconnected, kCompleted, kFailed). Activities are kept in the service
// after they end, so control surfaces must filter with this predicate.
bool IsTerminalControlActivityState(ControlActivityState state);

enum class ControlPlane {
  kLocalAgent,
  kMcp,
  kEnterprise,
};

enum class ControllerType {
  kUserAgent,
  kRemoteClient,
  kAutomation,
};

enum class ActivityCategory {
  kBrowsing,
  kFormEntry,
  kAccountChange,
  kPurchase,
  kOther,
};

enum class ActivitySensitivity {
  kLow,
  kMedium,
  kHigh,
};

enum class ApprovalOutcome {
  kNotRequested,
  kPending,
  kApproved,
  kDenied,
};

enum class ActivityReceiptKind {
  kStart,
  kResult,
  kFailure,
};

struct ControlTarget {
  int64_t window_id = 0;
  int64_t tab_id = 0;
  url::Origin origin;
  std::u16string title;

  bool operator==(const ControlTarget &) const = default;
};

struct ActivityReceipt {
  ActivityReceiptKind kind = ActivityReceiptKind::kStart;
  std::string code;
  std::string summary;

  bool operator==(const ActivityReceipt&) const = default;
};

struct ControlActivity {
  ControlActivity();
  ControlActivity(const ControlActivity&);
  ControlActivity& operator=(const ControlActivity&);
  ControlActivity(ControlActivity&&);
  ControlActivity& operator=(ControlActivity&&);
  ~ControlActivity();

  std::string session_id;
  std::string controller_session_id;
  std::string controller_display_name;
  ControllerType controller_type = ControllerType::kUserAgent;
  ControlPlane control_plane = ControlPlane::kLocalAgent;
  std::optional<ControlTarget> target;
  ControlActivityState state = ControlActivityState::kReading;
  ActivityCategory category = ActivityCategory::kOther;
  ActivitySensitivity sensitivity = ActivitySensitivity::kLow;
  ApprovalOutcome approval_outcome = ApprovalOutcome::kNotRequested;
  uint64_t event_revision = 0;
  uint64_t observer_revision = 0;
  base::TimeTicks created_at;
  base::TimeTicks updated_at;
  base::TimeTicks lease_expires_at;
  std::vector<ActivityReceipt> receipts;

  bool operator==(const ControlActivity&) const = default;
};

struct StartControlActivityParams {
  StartControlActivityParams();
  StartControlActivityParams(const StartControlActivityParams&);
  StartControlActivityParams& operator=(const StartControlActivityParams&);
  StartControlActivityParams(StartControlActivityParams&&);
  StartControlActivityParams& operator=(StartControlActivityParams&&);
  ~StartControlActivityParams();

  std::string session_id;
  std::string controller_session_id;
  std::string controller_display_name;
  ControllerType controller_type = ControllerType::kUserAgent;
  ControlPlane control_plane = ControlPlane::kLocalAgent;
  std::optional<ControlTarget> target;
  ActivityCategory category = ActivityCategory::kOther;
  ActivitySensitivity sensitivity = ActivitySensitivity::kLow;
  base::TimeDelta lease_duration = base::Minutes(1);
  ActivityReceipt start_receipt;
};

struct ControlActivityUpdate {
  ControlActivityUpdate();
  ControlActivityUpdate(const ControlActivityUpdate&);
  ControlActivityUpdate& operator=(const ControlActivityUpdate&);
  ControlActivityUpdate(ControlActivityUpdate&&);
  ControlActivityUpdate& operator=(ControlActivityUpdate&&);
  ~ControlActivityUpdate();

  uint64_t event_revision = 0;
  ControlActivityState state = ControlActivityState::kReading;
  ApprovalOutcome approval_outcome = ApprovalOutcome::kNotRequested;
  base::TimeDelta lease_duration = base::Minutes(1);
  std::optional<ActivityReceipt> receipt;
};

// Consequential agent action persisted to the local-only agent action journal
// (profile SQLite `agent_action_journal`, 30-day retention, never synced).
// Journaling is after-the-fact review only: it never prompts or waits.
struct AgentActionJournalRecord {
  int64_t timestamp_seconds = 0;
  std::string session_id;
  std::string actor;   // "cli" | "mcp" | "panel"
  std::string action;  // see JournalActionForTool()
  std::string origin;
  std::string summary;
  std::string outcome;  // "completed" | "failed"

  bool operator==(const AgentActionJournalRecord&) const = default;
};

// Inputs for classifying one tool execution into a journal action kind.
// Mirrors `classify_journal_action` in maho-agent/src/approval_audit.rs; keep
// both tables in sync with maho_browser_capability_catalog.def.
struct JournalActionInput {
  std::string_view tool_name;
  // Tool arguments; nullptr = no argument signals.
  const base::DictValue* arguments = nullptr;
  // Browser-minted consequence (e.g. "submit" for a click classified as a
  // form submission). Never model- or MCP-argument-supplied.
  std::string_view action_consequence;
  // Committed origin of the page before the action; opaque = unknown.
  url::Origin current_origin;
};

// Journal kinds that exist in storage but have no producing tool in the
// current catalog ("purchase", "mail_delete"). Reserved so a future tool can
// journal without a schema change; nothing maps to them today.
bool IsReservedJournalAction(std::string_view action);

// Classifies a tool execution into its journal action kind, or nullopt when
// it is not consequential (reads, scrolls, snapshots, same-origin nav, ...).
//   mail_send, mail_queue_email                         -> mail_send
//   vault_fill_*, browser_type{allow_credentials}       -> credential_typing
//   browser_type / act_and_observe{type} with submit,
//     or a click whose browser-minted consequence is
//     "submit"                                          -> form_submit
//   browser_navigate / act_and_observe{navigate} to a
//     different origin than the current page            -> cross_origin_navigation
//   browser_file_upload_select                          -> file_upload
std::optional<std::string> ClassifyJournalAction(
    const JournalActionInput& input);

// Name-only convenience wrapper (no argument signals).
std::optional<std::string> JournalActionForTool(std::string_view tool_name);

// True for actions that also raise a non-modal after-the-fact toast
// (purchase, credential typing, mail send).
bool ShouldShowAgentActionNotice(std::string_view action);

// Journal actor for a control plane / controller kind.
std::string JournalActorFor(ControlPlane plane, ControllerType controller_type);

class MahoControlActivityService : public KeyedService {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnControlActivityChanged(const ControlActivity &activity) = 0;

  protected:
    ~Observer() override = default;
  };

  static constexpr base::TimeDelta kMinLeaseDuration = base::Seconds(1);
  static constexpr base::TimeDelta kMaxLeaseDuration = base::Minutes(5);

  static MahoControlActivityService *GetForProfile(Profile *profile);

  explicit MahoControlActivityService(
      Profile *profile, const base::TickClock *tick_clock =
                            base::DefaultTickClock::GetInstance());
  ~MahoControlActivityService() override;

  MahoControlActivityService(const MahoControlActivityService &) = delete;
  MahoControlActivityService &
  operator=(const MahoControlActivityService &) = delete;

  void AddObserver(Observer *observer);
  void RemoveObserver(Observer *observer);

  bool StartActivity(StartControlActivityParams params);
  bool ApplyUpdate(std::string_view session_id, ControlActivityUpdate update);
  bool BindTarget(std::string_view session_id, uint64_t event_revision,
                  ControlTarget target, base::TimeDelta lease_duration);
  bool Disconnect(std::string_view session_id, uint64_t event_revision);
  bool RequestImmediateStop(int64_t tab_id);
  void OnTargetClosed(int64_t tab_id);

  // Process-wide sinks installed at browser startup. The writer persists one
  // record and returns false on failure; the presenter shows a non-modal
  // toast. Both are optional and must never block.
  using AgentActionJournalWriter =
      base::RepeatingCallback<bool(const AgentActionJournalRecord&)>;
  using AgentActionNoticePresenter =
      base::RepeatingCallback<void(Profile*, const AgentActionJournalRecord&)>;
  static void SetAgentActionJournalWriter(AgentActionJournalWriter writer);
  static void SetAgentActionNoticePresenter(
      AgentActionNoticePresenter presenter);

  // Best-effort: records `record` (unless the profile is off-the-record) and
  // raises the notice for purchase/credential/mail-send. A writer failure is
  // logged and ignored; the agent action is never blocked. Returns whether
  // the record was persisted.
  bool RecordAgentAction(const AgentActionJournalRecord& record);

  std::optional<ControlActivity> GetActivity(std::string_view session_id) const;
  std::vector<ControlActivity> GetActivities() const;

private:
  bool Transition(ControlActivity &activity, uint64_t event_revision,
                  ControlActivityState state, ApprovalOutcome approval_outcome,
                  base::TimeDelta lease_duration,
                  std::optional<ActivityReceipt> receipt);
  void Publish(ControlActivity &activity);
  void ScheduleNextExpiry();
  void ExpireLeases();

  const raw_ptr<Profile> profile_;
  const raw_ptr<const base::TickClock> tick_clock_;
  std::map<std::string, ControlActivity, std::less<>> activities_;
  uint64_t observer_revision_ = 0;
  base::ObserverList<Observer> observers_;
  base::OneShotTimer expiry_timer_;
  SEQUENCE_CHECKER(sequence_checker_);
};

} // namespace maho::ai

#endif // MAHO_BROWSER_AI_MAHO_CONTROL_ACTIVITY_SERVICE_H_
