// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_control_activity_service.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "content/public/browser/browser_context.h"

namespace maho::ai {
namespace {

constexpr size_t kMaxSessionIdLength = 256;
constexpr size_t kMaxControllerDisplayNameLength = 128;
constexpr size_t kMaxReceiptCodeLength = 64;
constexpr char kRedactedReceiptSummary[] = "[redacted]";
constexpr char kRedactedReceiptCode[] = "redacted";

class MahoControlActivityServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static MahoControlActivityServiceFactory* GetInstance() {
    static base::NoDestructor<MahoControlActivityServiceFactory> instance;
    return instance.get();
  }

  static MahoControlActivityService* GetForProfile(Profile* profile) {
    if (!profile || profile->IsOffTheRecord()) {
      return nullptr;
    }
    return static_cast<MahoControlActivityService*>(
        GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
  }

 private:
  friend base::NoDestructor<MahoControlActivityServiceFactory>;

  MahoControlActivityServiceFactory()
      : ProfileKeyedServiceFactory(
            "MahoControlActivityService",
            ProfileSelections::BuildForRegularProfile()) {}
  ~MahoControlActivityServiceFactory() override = default;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override {
    Profile* profile = Profile::FromBrowserContext(context);
    if (!profile || profile->IsOffTheRecord()) {
      return nullptr;
    }
    return std::make_unique<MahoControlActivityService>(profile);
  }
};

bool IsTerminal(ControlActivityState state) {
  return state == ControlActivityState::kDisconnected ||
         state == ControlActivityState::kCompleted ||
         state == ControlActivityState::kFailed;
}

bool IsValidTarget(const ControlTarget& target) {
  // The origin is allowed to be opaque here: a freshly created tab has no
  // committed navigation yet, and control attribution (window/tab/title) is
  // still meaningful for the user-visible control surfaces.
  return target.window_id > 0 && target.tab_id > 0;
}

bool IsAllowedReceiptCode(std::string_view code) {
  if (code.empty() || code.size() > kMaxReceiptCodeLength) {
    return false;
  }
  static constexpr std::string_view kAllowedCodes[] = {
      "started", "completed", "failed", "done", "navigation",
      "provider_error"};
  return std::ranges::find(kAllowedCodes, code) !=
         std::ranges::end(kAllowedCodes);
}

ActivityReceipt RedactReceipt(ActivityReceipt receipt) {
  if (!IsAllowedReceiptCode(receipt.code)) {
    receipt.code = kRedactedReceiptCode;
  }
  if (!receipt.summary.empty()) {
    receipt.summary = kRedactedReceiptSummary;
  }
  return receipt;
}

bool IsReceiptAllowedForState(ActivityReceiptKind kind,
                              ControlActivityState state) {
  switch (kind) {
    case ActivityReceiptKind::kStart:
      return false;
    case ActivityReceiptKind::kResult:
      return state == ControlActivityState::kCompleted;
    case ActivityReceiptKind::kFailure:
      return state == ControlActivityState::kFailed;
  }
}

bool IsResolvedApproval(ApprovalOutcome approval_outcome) {
  return approval_outcome == ApprovalOutcome::kNotRequested ||
         approval_outcome == ApprovalOutcome::kApproved ||
         approval_outcome == ApprovalOutcome::kDenied;
}

bool IsLegalTransition(ControlActivityState from,
                       ControlActivityState to,
                       ApprovalOutcome approval_outcome) {
  if (IsTerminal(from) || from == to) {
    return false;
  }
  if (to == ControlActivityState::kDisconnected) {
    return true;
  }

  switch (from) {
    case ControlActivityState::kReading:
      return (to == ControlActivityState::kActing &&
              (approval_outcome == ApprovalOutcome::kNotRequested ||
               approval_outcome == ApprovalOutcome::kApproved)) ||
             ((to == ControlActivityState::kCompleted ||
               to == ControlActivityState::kFailed) &&
              IsResolvedApproval(approval_outcome));
    case ControlActivityState::kActing:
      if (to == ControlActivityState::kWaitingApproval) {
        return approval_outcome == ApprovalOutcome::kPending;
      }
      return (to == ControlActivityState::kPaused ||
              to == ControlActivityState::kCompleted ||
              to == ControlActivityState::kFailed) &&
             IsResolvedApproval(approval_outcome);
    case ControlActivityState::kWaitingApproval:
      return (to == ControlActivityState::kActing &&
              approval_outcome == ApprovalOutcome::kApproved) ||
             (to == ControlActivityState::kPaused &&
              approval_outcome == ApprovalOutcome::kDenied) ||
             (to == ControlActivityState::kFailed &&
              approval_outcome == ApprovalOutcome::kDenied);
    case ControlActivityState::kPaused:
      return to == ControlActivityState::kActing &&
             IsResolvedApproval(approval_outcome);
    case ControlActivityState::kDisconnected:
    case ControlActivityState::kCompleted:
    case ControlActivityState::kFailed:
      return false;
  }
}

base::TimeDelta ClampLeaseDuration(base::TimeDelta lease_duration) {
  return std::clamp(lease_duration,
                    MahoControlActivityService::kMinLeaseDuration,
                    MahoControlActivityService::kMaxLeaseDuration);
}

}  // namespace

ControlActivity::ControlActivity() = default;
ControlActivity::ControlActivity(const ControlActivity&) = default;
ControlActivity& ControlActivity::operator=(const ControlActivity&) = default;
ControlActivity::ControlActivity(ControlActivity&&) = default;
ControlActivity& ControlActivity::operator=(ControlActivity&&) = default;
ControlActivity::~ControlActivity() = default;

StartControlActivityParams::StartControlActivityParams() = default;
StartControlActivityParams::StartControlActivityParams(
    const StartControlActivityParams&) = default;
StartControlActivityParams& StartControlActivityParams::operator=(
    const StartControlActivityParams&) = default;
StartControlActivityParams::StartControlActivityParams(
    StartControlActivityParams&&) = default;
StartControlActivityParams& StartControlActivityParams::operator=(
    StartControlActivityParams&&) = default;
StartControlActivityParams::~StartControlActivityParams() = default;

ControlActivityUpdate::ControlActivityUpdate() = default;
ControlActivityUpdate::ControlActivityUpdate(const ControlActivityUpdate&) =
    default;
ControlActivityUpdate& ControlActivityUpdate::operator=(
    const ControlActivityUpdate&) = default;
ControlActivityUpdate::ControlActivityUpdate(ControlActivityUpdate&&) = default;
ControlActivityUpdate& ControlActivityUpdate::operator=(
    ControlActivityUpdate&&) = default;
ControlActivityUpdate::~ControlActivityUpdate() = default;

void EnsureMahoControlActivityServiceFactoryBuilt() {
  MahoControlActivityServiceFactory::GetInstance();
}

// static
MahoControlActivityService* MahoControlActivityService::GetForProfile(
    Profile* profile) {
  return MahoControlActivityServiceFactory::GetForProfile(profile);
}

MahoControlActivityService::MahoControlActivityService(
    Profile* profile,
    const base::TickClock* tick_clock)
    : profile_(profile), tick_clock_(tick_clock), expiry_timer_(tick_clock) {
  CHECK(profile_);
  CHECK(tick_clock_);
}

MahoControlActivityService::~MahoControlActivityService() = default;

void MahoControlActivityService::AddObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void MahoControlActivityService::RemoveObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

bool MahoControlActivityService::StartActivity(
    StartControlActivityParams params) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (params.session_id.empty() ||
      params.session_id.size() > kMaxSessionIdLength ||
      params.controller_display_name.empty() ||
      params.controller_display_name.size() > kMaxControllerDisplayNameLength ||
      params.start_receipt.kind != ActivityReceiptKind::kStart ||
      (params.target && !IsValidTarget(*params.target)) ||
      activities_.contains(params.session_id)) {
    return false;
  }

  const base::TimeTicks now = tick_clock_->NowTicks();
  ControlActivity activity;
  activity.session_id = std::move(params.session_id);
  activity.controller_session_id = std::move(params.controller_session_id);
  activity.controller_display_name = std::move(params.controller_display_name);
  activity.controller_type = params.controller_type;
  activity.control_plane = params.control_plane;
  activity.target = std::move(params.target);
  activity.state = ControlActivityState::kReading;
  activity.category = params.category;
  activity.sensitivity = params.sensitivity;
  activity.approval_outcome = ApprovalOutcome::kNotRequested;
  activity.event_revision = 1;
  activity.created_at = now;
  activity.updated_at = now;
  activity.lease_expires_at = now + ClampLeaseDuration(params.lease_duration);
  activity.receipts = {RedactReceipt(std::move(params.start_receipt))};
  auto [it, inserted] =
      activities_.emplace(activity.session_id, std::move(activity));
  CHECK(inserted);
  Publish(it->second);
  ScheduleNextExpiry();
  return true;
}

bool MahoControlActivityService::ApplyUpdate(std::string_view session_id,
                                             ControlActivityUpdate update) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = activities_.find(session_id);
  if (it == activities_.end()) {
    return false;
  }
  return Transition(it->second, update.event_revision, update.state,
                    update.approval_outcome, update.lease_duration,
                    std::move(update.receipt));
}

bool MahoControlActivityService::BindTarget(std::string_view session_id,
                                            uint64_t event_revision,
                                            ControlTarget target,
                                            base::TimeDelta lease_duration) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = activities_.find(session_id);
  if (it == activities_.end() || it->second.target ||
      IsTerminal(it->second.state) || !IsValidTarget(target) ||
      event_revision != it->second.event_revision + 1) {
    return false;
  }

  ControlActivity& activity = it->second;
  const base::TimeTicks now = tick_clock_->NowTicks();
  activity.target = std::move(target);
  activity.event_revision = event_revision;
  activity.updated_at = now;
  activity.lease_expires_at = now + ClampLeaseDuration(lease_duration);
  Publish(activity);
  ScheduleNextExpiry();
  return true;
}

bool MahoControlActivityService::Disconnect(std::string_view session_id,
                                            uint64_t event_revision) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = activities_.find(session_id);
  if (it == activities_.end()) {
    return false;
  }
  return Transition(
      it->second, event_revision, ControlActivityState::kDisconnected,
      it->second.approval_outcome, base::TimeDelta(), std::nullopt);
}

bool MahoControlActivityService::RequestImmediateStop(int64_t tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (tab_id <= 0) {
    return false;
  }

  bool stopped = false;
  const base::TimeTicks now = tick_clock_->NowTicks();
  for (auto& [session_id, activity] : activities_) {
    if (IsTerminal(activity.state) || !activity.target ||
        activity.target->tab_id != tab_id) {
      continue;
    }
    activity.state = ControlActivityState::kDisconnected;
    ++activity.event_revision;
    activity.updated_at = now;
    activity.lease_expires_at = base::TimeTicks();
    Publish(activity);
    stopped = true;
  }
  if (stopped) {
    ScheduleNextExpiry();
  }
  return stopped;
}

void MahoControlActivityService::OnTargetClosed(int64_t tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (tab_id <= 0) {
    return;
  }

  bool changed = false;
  const base::TimeTicks now = tick_clock_->NowTicks();
  for (auto& [session_id, activity] : activities_) {
    if (IsTerminal(activity.state) || !activity.target ||
        activity.target->tab_id != tab_id) {
      continue;
    }
    activity.state = ControlActivityState::kDisconnected;
    ++activity.event_revision;
    activity.updated_at = now;
    activity.lease_expires_at = base::TimeTicks();
    Publish(activity);
    changed = true;
  }
  if (changed) {
    ScheduleNextExpiry();
  }
}

std::optional<ControlActivity> MahoControlActivityService::GetActivity(
    std::string_view session_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = activities_.find(session_id);
  return it == activities_.end() ? std::nullopt
                                 : std::optional<ControlActivity>(it->second);
}

std::vector<ControlActivity> MahoControlActivityService::GetActivities() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<ControlActivity> result;
  result.reserve(activities_.size());
  for (const auto& [session_id, activity] : activities_) {
    result.push_back(activity);
  }
  return result;
}

bool MahoControlActivityService::Transition(
    ControlActivity& activity,
    uint64_t event_revision,
    ControlActivityState state,
    ApprovalOutcome approval_outcome,
    base::TimeDelta lease_duration,
    std::optional<ActivityReceipt> receipt) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (event_revision != activity.event_revision + 1 ||
      !IsLegalTransition(activity.state, state, approval_outcome) ||
      (receipt && !IsReceiptAllowedForState(receipt->kind, state))) {
    return false;
  }

  const base::TimeTicks now = tick_clock_->NowTicks();
  activity.state = state;
  activity.approval_outcome = approval_outcome;
  activity.event_revision = event_revision;
  activity.updated_at = now;
  activity.lease_expires_at = IsTerminal(state)
                                  ? base::TimeTicks()
                                  : now + ClampLeaseDuration(lease_duration);
  if (receipt) {
    activity.receipts.push_back(RedactReceipt(std::move(*receipt)));
  }
  Publish(activity);
  ScheduleNextExpiry();
  return true;
}

void MahoControlActivityService::Publish(ControlActivity& activity) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  activity.observer_revision = ++observer_revision_;
  for (Observer& observer : observers_) {
    observer.OnControlActivityChanged(activity);
  }
}

void MahoControlActivityService::ScheduleNextExpiry() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::optional<base::TimeTicks> next_expiry;
  for (const auto& [session_id, activity] : activities_) {
    if (IsTerminal(activity.state) || activity.lease_expires_at.is_null()) {
      continue;
    }
    if (!next_expiry || activity.lease_expires_at < *next_expiry) {
      next_expiry = activity.lease_expires_at;
    }
  }

  expiry_timer_.Stop();
  if (!next_expiry) {
    return;
  }
  expiry_timer_.Start(
      FROM_HERE,
      std::max(base::TimeDelta(), *next_expiry - tick_clock_->NowTicks()),
      base::BindOnce(&MahoControlActivityService::ExpireLeases,
                     base::Unretained(this)));
}

void MahoControlActivityService::ExpireLeases() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const base::TimeTicks now = tick_clock_->NowTicks();
  for (auto& [session_id, activity] : activities_) {
    if (IsTerminal(activity.state) || activity.lease_expires_at.is_null() ||
        now < activity.lease_expires_at) {
      continue;
    }
    activity.state = ControlActivityState::kDisconnected;
    ++activity.event_revision;
    activity.updated_at = now;
    activity.lease_expires_at = base::TimeTicks();
    Publish(activity);
  }
  ScheduleNextExpiry();
}

}  // namespace maho::ai
