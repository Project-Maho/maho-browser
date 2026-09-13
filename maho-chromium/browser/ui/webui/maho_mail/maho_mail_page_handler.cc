// Copyright 2026 Maho Browser. All rights reserved.

#include "base/files/file_util.h"
#include "base/files/file_path.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/webui/maho_mail/maho_mail_page_handler.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/notreached.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "maho/browser/ai/maho_ffi_callback_handle.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/mail_helper/maho_mail_service.h"
#include "maho/browser/mail_helper/maho_mail_service_factory.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/webui/maho_mail/maho_mail_backend_boundary.h"
#include "maho/browser/ui/webui/maho_mail/maho_mail_attachment_launcher.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/browser/ui/webui/maho_mail/maho_mail_oauth_session.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "base/functional/callback_helpers.h"
#include "ui/base/page_transition_types.h"

namespace {

const char* EncodeMailLifecycleState(
    maho::MahoMailService::LifecycleState state) {
  using LifecycleState = maho::MahoMailService::LifecycleState;
  switch (state) {
    case LifecycleState::kDisabled:
      return "disabled";
    case LifecycleState::kStarting:
      return "starting";
    case LifecycleState::kReady:
      return "ready";
    case LifecycleState::kDraining:
      return "draining";
    case LifecycleState::kStopped:
      return "stopped";
    case LifecycleState::kFailed:
      return "failed";
  }
  NOTREACHED();
}

}  // namespace

MahoMailPageHandler::MahoMailPageHandler(
    mojo::PendingReceiver<maho_mail::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_mail::mojom::Page> page,
    Profile* profile,
    content::WebContents* web_contents)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      profile_(profile),
      web_contents_(web_contents) {
  DCHECK(profile_);
  if (!profile_) {
    return;
  }

  pref_registrar_.Init(profile_->GetPrefs());
  pref_registrar_.Add(
      maho::sidebar_prefs::kMahoMailEnabled,
      base::BindRepeating(&MahoMailPageHandler::OnMailEnabledPrefChanged,
                          weak_factory_.GetWeakPtr()));
  pref_registrar_.Add(
      prefs::kBrowserColorScheme,
      base::BindRepeating(&MahoMailPageHandler::OnBrowserUiPrefChanged,
                          weak_factory_.GetWeakPtr()));

  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (service) {
    mail_service_observation_.Observe(service);
    OnLifecycleChanged(service->lifecycle_state(), service->generation());
  } else {
    page_->OnLifecycleChanged(
        maho::sidebar_prefs::IsMahoMailEnabled(profile_->GetPrefs())
            ? "stopped"
            : "disabled",
        0);
  }

  // Density (maho-core appearance.density) live-observation: maho-core fires
  // maho_core_register_settings_change_callback on any settings change (incl.
  // density). Route its thunk through MaybePushBrowserUiPrefs() -- the same
  // coalescing sink theme's PrefChangeRegistrar uses -- so density updates
  // push live; dedup collapses redundant snapshots. The thunk runs on an
  // arbitrary maho-core thread, so the FfiCallbackHandle bridge recovers a
  // weak handle and PostTasks back to this sequence; a callback firing after
  // destruction is a safe no-op.
  settings_change_handle_ =
      std::make_unique<maho::FfiCallbackHandle<MahoMailPageHandler>>(
          base::SequencedTaskRunner::GetCurrentDefault(),
          weak_factory_.GetWeakPtr());
  if (MahoCore* core = maho::GetCore()) {
    settings_change_callback_token_ =
        maho_core_register_settings_change_callback(
            core, settings_change_handle_->user_data(),
            &MahoMailPageHandler::OnSettingsChangedThunk);
  }
}

MahoMailPageHandler::~MahoMailPageHandler() {
  DisableMailSurface();
}

MahoMailPageHandler::MahoMailPageHandler(Backend* backend_for_testing)
    : receiver_(this),
      profile_(nullptr),
      web_contents_(nullptr),
      backend_for_testing_(backend_for_testing) {
  CHECK(backend_for_testing_);
}

maho::MahoMailService* MahoMailPageHandler::GetMailService() {
  if (!profile_ ||
      !maho::sidebar_prefs::IsMahoMailEnabled(profile_->GetPrefs())) {
    return nullptr;
  }
  return maho::MahoMailServiceFactory::GetForProfile(profile_);
}

void MahoMailPageHandler::OnMailEnabledPrefChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!profile_) {
    return;
  }
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (service && !mail_service_observation_.IsObserving()) {
    mail_service_observation_.Observe(service);
  }
  if (!maho::sidebar_prefs::IsMahoMailEnabled(profile_->GetPrefs())) {
    page_->OnLifecycleChanged("disabled", service ? service->generation() : 0);
    return;
  }
  if (service) {
    OnLifecycleChanged(service->lifecycle_state(), service->generation());
  }
}

void MahoMailPageHandler::DisableMailSurface() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  pref_registrar_.RemoveAll();
  mail_service_observation_.Reset();

  // Stop new maho-core callbacks first, then cancel the handle so any in-flight
  // thunk that already loaded its shared_ptr copy short-circuits (UAF-safe).
  if (settings_change_callback_token_ != 0) {
    maho_core_unregister_settings_change_callback(
        maho::GetCore(), settings_change_callback_token_);
    settings_change_callback_token_ = 0;
  }
  if (settings_change_handle_) {
    settings_change_handle_->Cancel();
    settings_change_handle_.reset();
  }

  weak_factory_.InvalidateWeakPtrs();
  receiver_.reset();
  page_.reset();
}

void MahoMailPageHandler::ListAccounts(ListAccountsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListAccounts(base::BindOnce(
      [](ListAccountsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::AddAccount(const std::string& request_json, AddAccountCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->AddAccount(request_json, base::BindOnce(
      [](AddAccountCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::TestConnection(const std::string& params_json, TestConnectionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->TestConnection(params_json, base::BindOnce(
      [](TestConnectionCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DeleteAccount(const std::string& account_id, DeleteAccountCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteAccount(account_id, base::BindOnce(
      [](DeleteAccountCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::OAuthStartUrl(const std::string& provider,
                                       const std::string& client_id,
                                       const std::string& redirect_uri,
                                       OAuthStartUrlCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  const std::string options_json =
      provider == "gmail"
          ? maho::auth::BuildMailOAuthStartOptionsJson(profile_->GetPrefs())
          : "{}";
  service->OAuthStartUrl(provider, client_id, redirect_uri, options_json,
                         base::BindOnce(
      [](OAuthStartUrlCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::BeginOAuth(const std::string& provider,
                                     const std::string& reauthorize_account_id,
                                     BeginOAuthCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable", std::string());
    return;
  }
  std::string options_json =
      provider == "gmail"
          ? maho::auth::BuildMailOAuthStartOptionsJson(profile_->GetPrefs())
          : "{}";
  if (!reauthorize_account_id.empty()) {
    base::DictValue options;
    options.Set("reauthorize_account_id", reauthorize_account_id);
    base::JSONWriter::Write(options, &options_json);
  }
  service->OAuthLoopbackSignIn(
      provider, options_json,
      base::BindOnce(
          [](base::WeakPtr<MahoMailPageHandler> self, Profile* profile,
             BeginOAuthCallback cb, bool ok, std::string result) {
             if (!ok) {
               std::move(cb).Run(false, std::move(result), std::string());
               return;
             }
            maho::MahoMailOAuthSession::StartResult start =
                maho::MahoMailOAuthSession::ParseStartResult(result);
             if (!start.ok) {
               std::move(cb).Run(false, std::move(start.error_json),
                                 std::string());
               return;
             }
             if (!self || !self->web_contents_) {
               std::move(cb).Run(false, "Mail surface unavailable",
                                 std::string());
               return;
             }
            auto cancel_closure = base::BindOnce(
                [](Profile* prof, std::string st) {
                  maho::MahoMailService* mail_service =
                      maho::MahoMailServiceFactory::GetForProfileIfExists(prof);
                  if (mail_service) {
                    mail_service->OAuthCancel(st, base::DoNothing());
                  }
                },
                profile, start.state);
            maho::MahoMailOAuthSession::CreateForWebContents(
                self->web_contents_, start.state, std::move(cancel_closure));

             content::NavigationController::LoadURLParams params(start.auth_url);
             params.transition_type = ui::PAGE_TRANSITION_LINK;
             self->web_contents_->GetController().LoadURLWithParams(params);
             std::move(cb).Run(true, std::string(), std::move(start.state));
           },
          weak_factory_.GetWeakPtr(), profile_, std::move(callback)));
}

void MahoMailPageHandler::OAuthComplete(const std::string& state,
                                       const std::string& code,
                                       OAuthCompleteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->OAuthComplete(state, code, base::BindOnce(
      [](base::WeakPtr<MahoMailPageHandler> self, std::string state,
         OAuthCompleteCallback cb, bool ok, std::string result) {
        if (ok && self && self->web_contents_) {
          maho::MahoMailOAuthSession::MarkCompleteForWebContents(
              self->web_contents_, state);
        }
        std::move(cb).Run(ok, std::move(result));
      },
      weak_factory_.GetWeakPtr(), state, std::move(callback)));
}

void MahoMailPageHandler::ReconnectAccount(const std::string& account_id, ReconnectAccountCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!backend_for_testing_ && !service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  base::DictValue options;
  options.Set("reauthorize_account_id", account_id);
  std::string options_json;
  base::JSONWriter::Write(options, &options_json);
  auto completion = base::BindOnce(
          [](base::WeakPtr<MahoMailPageHandler> self, Profile* profile,
             ReconnectAccountCallback cb, bool ok, std::string result) {
            if (!ok) {
              std::move(cb).Run(false, std::move(result));
              return;
            }
            maho::MahoMailOAuthSession::StartResult start =
                maho::MahoMailOAuthSession::ParseStartResult(result);
            if (!start.ok) {
              std::move(cb).Run(false, std::move(start.error_json));
              return;
            }
            if (!self || !self->web_contents_) {
              std::move(cb).Run(false, "Mail surface unavailable");
              return;
            }
            auto cancel_closure = base::BindOnce(
                [](Profile* prof, std::string state) {
                  maho::MahoMailService* mail_service =
                      maho::MahoMailServiceFactory::GetForProfileIfExists(prof);
                  if (mail_service) {
                    mail_service->OAuthCancel(state, base::DoNothing());
                  }
                },
                profile, start.state);
            maho::MahoMailOAuthSession::CreateForWebContents(
                self->web_contents_, start.state, std::move(cancel_closure));
            std::move(cb).Run(true, "null");

            content::NavigationController::LoadURLParams params(start.auth_url);
            params.transition_type = ui::PAGE_TRANSITION_LINK;
            self->web_contents_->GetController().LoadURLWithParams(params);
          },
          weak_factory_.GetWeakPtr(), profile_, std::move(callback));
  if (backend_for_testing_) {
    backend_for_testing_->OAuthLoopbackSignIn(
        "gmail", options_json, std::move(completion));
  } else {
    service->OAuthLoopbackSignIn("gmail", options_json, std::move(completion));
  }
}

void MahoMailPageHandler::OnAuthRequired(const std::string& account_id,
                                         const std::string& provider,
                                         const std::string& reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnAuthRequired(account_id, provider, reason);
}

void MahoMailPageHandler::OnAuthRefreshSucceeded(const std::string& account_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnAuthRefreshSucceeded(account_id);
}

void MahoMailPageHandler::CancelOAuth(const std::string& state, CancelOAuthCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  service->OAuthCancel(state, std::move(callback));
}

void MahoMailPageHandler::OnAccountsChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnAccountsChanged();
}

void MahoMailPageHandler::OnLifecycleChanged(
    maho::MahoMailService::LifecycleState state,
    uint64_t generation) {
  page_->OnLifecycleChanged(EncodeMailLifecycleState(state), generation);
}

void MahoMailPageHandler::OnSyncEvent(const std::string& event_type, const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnSyncEvent(event_type, payload);
}

void MahoMailPageHandler::OnBackfillEvent(const std::string& event_type, const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnBackfillEvent(event_type, payload);
}

void MahoMailPageHandler::OnStatusChanged(const std::string& account_id, bool connected) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnStatusChanged(account_id, connected);
}

void MahoMailPageHandler::OnNewMail(const std::string& account_id,
                                    const std::string& email_id,
                                    const std::string& message_id,
                                    const std::string& sender,
                                    const std::string& subject,
                                    uint64_t cursor,
                                    uint64_t epoch) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnNewMail(account_id);
}

void MahoMailPageHandler::OnMutation(const std::string& account_id, const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnMutation(account_id, payload);
}

void MahoMailPageHandler::OnOutbox(const std::string& account_id, const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnOutbox(account_id, payload);
}

void MahoMailPageHandler::OnScheduler(const std::string& account_id, const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnScheduler(account_id, payload);
}

void MahoMailPageHandler::OnAgentStream(const std::string& session_id, const std::string& chunk) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnAgentStream(session_id, chunk);
}

void MahoMailPageHandler::OnCalendar(const std::string& account_id, const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnCalendar(account_id, payload);
}

void MahoMailPageHandler::OnImport(const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnImport(payload);
}

void MahoMailPageHandler::CloseOnboarding() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!web_contents_) {
    return;
  }
  if (content::WebContentsDelegate* delegate = web_contents_->GetDelegate()) {
    delegate->CloseContents(web_contents_);
  }
}

std::string MahoMailPageHandler::BuildBrowserUiPrefsJson() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Theme: Chromium's authoritative kBrowserColorScheme int (0=system,
  // 1=light, 2=dark), matching maho_settings' appearance.theme mapping.
  std::string theme = "system";
  if (profile_) {
    switch (profile_->GetPrefs()->GetInteger(prefs::kBrowserColorScheme)) {
      case 1:
        theme = "light";
        break;
      case 2:
        theme = "dark";
        break;
      default:
        theme = "system";
        break;
    }
  }

  // Density: maho-core appearance.density string, same source maho_settings
  // reads via maho_core_get_settings.
  std::string density = "comfortable";
  if (MahoCore* core = maho::GetCore()) {
    if (char* json_str = maho_core_get_settings(core)) {
      std::string settings_json(json_str);
      maho_string_free(json_str);
      if (auto parsed =
              base::JSONReader::ReadDict(settings_json, base::JSON_PARSE_RFC)) {
        if (const base::DictValue* appearance =
                parsed->FindDict("appearance")) {
          if (const std::string* d = appearance->FindString("density")) {
            if (*d == "compact" || *d == "comfortable") {
              density = *d;
            }
          }
        }
      }
    }
  }

  base::DictValue dict;
  dict.Set("theme", theme);
  dict.Set("density", density);
  std::string out;
  base::JSONWriter::Write(base::Value(std::move(dict)), &out);
  return out;
}

void MahoMailPageHandler::GetBrowserUiPrefs(GetBrowserUiPrefsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(BuildBrowserUiPrefsJson());
}

void MahoMailPageHandler::OnBrowserUiPrefChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MaybePushBrowserUiPrefs();
}

void MahoMailPageHandler::MaybePushBrowserUiPrefs() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::string prefs_json = BuildBrowserUiPrefsJson();
  if (prefs_json == last_browser_ui_prefs_json_) {
    return;
  }
  last_browser_ui_prefs_json_ = prefs_json;
  page_->OnBrowserUiPrefsChanged(std::move(prefs_json));
}

// static
void MahoMailPageHandler::OnSettingsChangedThunk(void* user_data,
                                                 const char* /*settings_json*/) {
  if (!user_data) {
    return;
  }
  // Recover a weak handle rather than a raw `this`: this fires on an arbitrary
  // maho-core thread and the handler may already be gone. FromUserData copies a
  // shared_ptr that keeps the pointee alive for the load+PostTask below.
  auto pointee =
      maho::FfiCallbackHandle<MahoMailPageHandler>::FromUserData(user_data);
  if (!pointee || pointee->cancelled.load(std::memory_order_acquire) ||
      !pointee->task_runner) {
    return;
  }
  pointee->task_runner->PostTask(
      FROM_HERE, base::BindOnce(&MahoMailPageHandler::MaybePushBrowserUiPrefs,
                                pointee->owner));
}

void MahoMailPageHandler::MarkRead(const std::string& email_id, MarkReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->MarkRead(email_id, base::BindOnce(
      [](MarkReadCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::MarkUnread(const std::string& email_id, MarkUnreadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->MarkUnread(email_id, base::BindOnce(
      [](MarkUnreadCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ToggleStar(const std::string& email_id, ToggleStarCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ToggleStar(email_id, base::BindOnce(
      [](ToggleStarCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DeleteEmail(const std::string& email_id, DeleteEmailCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteEmail(email_id, base::BindOnce(
      [](DeleteEmailCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::MoveEmail(const std::string& email_id, const std::string& target_folder_id, MoveEmailCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->MoveEmail(email_id, target_folder_id, base::BindOnce(
      [](MoveEmailCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::BatchMarkRead(const std::string& request_json, BatchMarkReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->BatchMarkRead(request_json, base::BindOnce(
      [](BatchMarkReadCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::BatchMarkUnread(const std::string& request_json, BatchMarkUnreadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->BatchMarkUnread(request_json, base::BindOnce(
      [](BatchMarkUnreadCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::BatchDelete(const std::string& request_json, BatchDeleteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->BatchDelete(request_json, base::BindOnce(
      [](BatchDeleteCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::BatchMove(const std::string& request_json, BatchMoveCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->BatchMove(request_json, base::BindOnce(
      [](BatchMoveCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::BatchToggleStar(const std::string& request_json, BatchToggleStarCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->BatchToggleStar(request_json, base::BindOnce(
      [](BatchToggleStarCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SyncFolders(const std::string& account_id, SyncFoldersCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SyncFolders(account_id, base::BindOnce(
      [](SyncFoldersCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SyncFolder(const std::string& account_id, const std::string& folder_id, SyncFolderCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SyncFolder(account_id, folder_id, base::BindOnce(
      [](SyncFolderCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::CreateFolder(const std::string& account_id, const std::string& folder_name, CreateFolderCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->CreateFolder(account_id, folder_name, base::BindOnce(
      [](CreateFolderCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::RenameFolder(const std::string& account_id, const std::string& folder_id, const std::string& new_name, RenameFolderCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->RenameFolder(account_id, folder_id, new_name, base::BindOnce(
      [](RenameFolderCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DeleteFolder(const std::string& account_id, const std::string& folder_id, DeleteFolderCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteFolder(account_id, folder_id, base::BindOnce(
      [](DeleteFolderCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::GetFolderCounts(const std::string& account_id, GetFolderCountsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GetFolderCounts(account_id, base::BindOnce(
      [](GetFolderCountsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::FlushPendingMutations(const std::string& account_id, FlushPendingMutationsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->FlushPendingMutations(account_id, base::BindOnce(
      [](FlushPendingMutationsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::GetPendingMutationCount(GetPendingMutationCountCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GetPendingMutationCount(base::BindOnce(
      [](GetPendingMutationCountCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListPendingMutations(const std::string& account_id, ListPendingMutationsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListPendingMutations(account_id, base::BindOnce(
      [](ListPendingMutationsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

// === [W-A] Read API WebUI Exposure ===
void MahoMailPageHandler::ListFolders(const std::string& account_id, ListFoldersCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListFolders(account_id, base::BindOnce(
      [](ListFoldersCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListEmails(const std::string& account_id,
                                    const std::string& folder_id,
                                    int64_t limit,
                                    int64_t offset,
                                    ListEmailsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListEmails(account_id, folder_id, limit, offset, base::BindOnce(
      [](ListEmailsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::GetEmail(const std::string& email_id, GetEmailCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GetEmail(email_id, base::BindOnce(
      [](GetEmailCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SearchEmails(const std::string& query_json, SearchEmailsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SearchEmails(query_json, base::BindOnce(
      [](SearchEmailsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListThread(const std::string& account_id,
                                    const std::string& message_id,
                                    ListThreadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListThread(account_id, message_id, base::BindOnce(
      [](ListThreadCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

  // === [W-C..W-K.Typed] ===


void MahoMailPageHandler::StartAllAccountSync(StartAllAccountSyncCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->StartAllAccountSync(base::BindOnce(
      [](StartAllAccountSyncCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SendEmail(const std::string& request_json, SendEmailCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SendEmail(request_json, base::BindOnce(
      [](SendEmailCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SaveDraft(const std::string& request_json, SaveDraftCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SaveDraft(request_json, base::BindOnce(
      [](SaveDraftCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::UpdateDraft(const std::string& draft_id, const std::string& request_json, UpdateDraftCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->UpdateDraft(draft_id, request_json, base::BindOnce(
      [](UpdateDraftCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::GetReplyContext(const std::string& email_id, GetReplyContextCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GetReplyContext(email_id, base::BindOnce(
      [](GetReplyContextCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::QueueEmail(const std::string& request_json, QueueEmailCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->QueueEmail(request_json, base::BindOnce(
      [](QueueEmailCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListOutbox(const std::string& account_id, ListOutboxCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListOutbox(account_id, base::BindOnce(
      [](ListOutboxCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::RetryOutboxItem(const std::string& item_id, RetryOutboxItemCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->RetryOutboxItem(item_id, base::BindOnce(
      [](RetryOutboxItemCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DeleteOutboxItem(const std::string& item_id, DeleteOutboxItemCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteOutboxItem(item_id, base::BindOnce(
      [](DeleteOutboxItemCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::FlushOutbox(FlushOutboxCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->FlushOutbox(base::BindOnce(
      [](FlushOutboxCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DownloadAttachment(const std::string& account_id, int64_t email_uid, const std::string& folder_id, const std::string& part_id, const std::string& filename, DownloadAttachmentCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DownloadAttachment(account_id, email_uid, folder_id, part_id, filename, base::BindOnce(
      [](DownloadAttachmentCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ExtractOtp(const std::string& account_id, const std::string& folder_id, const std::string& query, int64_t max_age_seconds, ExtractOtpCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ExtractOtp(account_id, folder_id, query, max_age_seconds, base::BindOnce(
      [](ExtractOtpCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SnoozeEmail(const std::string& email_id, const std::string& snooze_until, SnoozeEmailCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SnoozeEmail(email_id, snooze_until, base::BindOnce(
      [](SnoozeEmailCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::UnsnoozeEmail(const std::string& email_id, UnsnoozeEmailCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->UnsnoozeEmail(email_id, base::BindOnce(
      [](UnsnoozeEmailCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListSnoozedEmails(const std::string& account_id, ListSnoozedEmailsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListSnoozedEmails(account_id, base::BindOnce(
      [](ListSnoozedEmailsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SetReminder(const std::string& email_id, const std::string& reminder_at, SetReminderCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SetReminder(email_id, reminder_at, base::BindOnce(
      [](SetReminderCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ClearReminder(const std::string& email_id, ClearReminderCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ClearReminder(email_id, base::BindOnce(
      [](ClearReminderCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListReminders(const std::string& account_id, ListRemindersCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListReminders(account_id, base::BindOnce(
      [](ListRemindersCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::MuteThread(const std::string& account_id, const std::string& message_id, MuteThreadCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->MuteThread(account_id, message_id, base::BindOnce(
      [](MuteThreadCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::UnmuteThread(const std::string& account_id, const std::string& message_id, UnmuteThreadCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->UnmuteThread(account_id, message_id, base::BindOnce(
      [](UnmuteThreadCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::IsThreadMuted(const std::string& account_id, const std::string& message_id, IsThreadMutedCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->IsThreadMuted(account_id, message_id, base::BindOnce(
      [](IsThreadMutedCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListMutedThreads(const std::string& account_id, ListMutedThreadsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListMutedThreads(account_id, base::BindOnce(
      [](ListMutedThreadsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::FilterMutedMessageIds(const std::string& account_id, const std::string& message_ids_json, FilterMutedMessageIdsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->FilterMutedMessageIds(account_id, message_ids_json, base::BindOnce(
      [](FilterMutedMessageIdsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::PinEmail(const std::string& email_id, PinEmailCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->PinEmail(email_id, base::BindOnce(
      [](PinEmailCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::UnpinEmail(const std::string& email_id, UnpinEmailCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->UnpinEmail(email_id, base::BindOnce(
      [](UnpinEmailCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListPinnedEmails(const std::string& account_id, ListPinnedEmailsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListPinnedEmails(account_id, base::BindOnce(
      [](ListPinnedEmailsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::CreateMailRule(const std::string& request_json, CreateMailRuleCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->CreateMailRule(request_json, base::BindOnce(
      [](CreateMailRuleCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::UpdateMailRule(const std::string& request_json, UpdateMailRuleCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->UpdateMailRule(request_json, base::BindOnce(
      [](UpdateMailRuleCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DeleteMailRule(const std::string& rule_id, DeleteMailRuleCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteMailRule(rule_id, base::BindOnce(
      [](DeleteMailRuleCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListMailRules(const std::string& account_id, ListMailRulesCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListMailRules(account_id, base::BindOnce(
      [](ListMailRulesCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ReorderMailRules(const std::string& account_id, const std::string& rule_ids_json, ReorderMailRulesCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ReorderMailRules(account_id, rule_ids_json, base::BindOnce(
      [](ReorderMailRulesCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListLabels(const std::string& account_id, ListLabelsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListLabels(account_id, base::BindOnce(
      [](ListLabelsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::CreateLabel(const std::string& request_json, CreateLabelCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->CreateLabel(request_json, base::BindOnce(
      [](CreateLabelCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DeleteLabel(const std::string& id, DeleteLabelCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteLabel(id, base::BindOnce(
      [](DeleteLabelCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::AddLabelToEmail(const std::string& email_id, const std::string& label_id, AddLabelToEmailCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->AddLabelToEmail(email_id, label_id, base::BindOnce(
      [](AddLabelToEmailCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::RemoveLabelFromEmail(const std::string& email_id, const std::string& label_id, RemoveLabelFromEmailCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->RemoveLabelFromEmail(email_id, label_id, base::BindOnce(
      [](RemoveLabelFromEmailCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListEmailLabels(const std::string& email_id, ListEmailLabelsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListEmailLabels(email_id, base::BindOnce(
      [](ListEmailLabelsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SaveSearch(const std::string& name, const std::string& query, const std::string& account_id, SaveSearchCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SaveSearch(name, query, account_id, base::BindOnce(
      [](SaveSearchCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListSavedSearches(const std::string& account_id, ListSavedSearchesCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListSavedSearches(account_id, base::BindOnce(
      [](ListSavedSearchesCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DeleteSavedSearch(const std::string& id, DeleteSavedSearchCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteSavedSearch(id, base::BindOnce(
      [](DeleteSavedSearchCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ScheduleSend(const std::string& request_json, ScheduleSendCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ScheduleSend(request_json, base::BindOnce(
      [](ScheduleSendCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::CancelScheduledSend(const std::string& id, CancelScheduledSendCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->CancelScheduledSend(id, base::BindOnce(
      [](CancelScheduledSendCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListScheduledSends(const std::string& account_id, ListScheduledSendsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListScheduledSends(account_id, base::BindOnce(
      [](ListScheduledSendsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::StartScheduler(StartSchedulerCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->StartScheduler(base::BindOnce(
      [](StartSchedulerCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::StopScheduler(StopSchedulerCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->StopScheduler(base::BindOnce(
      [](StopSchedulerCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SearchContacts(const std::string& account_id, const std::string& query, int64_t limit, SearchContactsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SearchContacts(account_id, query, limit, base::BindOnce(
      [](SearchContactsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ToggleVip(const std::string& contact_id, ToggleVipCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ToggleVip(contact_id, base::BindOnce(
      [](ToggleVipCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListVipContacts(const std::string& account_id, ListVipContactsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListVipContacts(account_id, base::BindOnce(
      [](ListVipContactsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::PopulateContactsFromHistory(const std::string& account_id, PopulateContactsFromHistoryCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->PopulateContactsFromHistory(account_id, base::BindOnce(
      [](PopulateContactsFromHistoryCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListContactGroups(const std::string& account_id, ListContactGroupsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListContactGroups(account_id, base::BindOnce(
      [](ListContactGroupsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SearchContactGroups(const std::string& account_id, const std::string& query, SearchContactGroupsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SearchContactGroups(account_id, query, base::BindOnce(
      [](SearchContactGroupsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::CreateContactGroup(const std::string& account_id, const std::string& name, const std::string& member_emails_json, CreateContactGroupCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->CreateContactGroup(account_id, name, member_emails_json, base::BindOnce(
      [](CreateContactGroupCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::UpdateContactGroup(const std::string& group_id, const std::string& name, const std::string& member_emails_json, UpdateContactGroupCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->UpdateContactGroup(group_id, name, member_emails_json, base::BindOnce(
      [](UpdateContactGroupCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DeleteContactGroup(const std::string& group_id, DeleteContactGroupCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteContactGroup(group_id, base::BindOnce(
      [](DeleteContactGroupCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListSignatures(const std::string& account_id, ListSignaturesCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListSignatures(account_id, base::BindOnce(
      [](ListSignaturesCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::CreateSignature(const std::string& request_json, CreateSignatureCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->CreateSignature(request_json, base::BindOnce(
      [](CreateSignatureCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::UpdateSignature(const std::string& id, const std::string& request_json, UpdateSignatureCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->UpdateSignature(id, request_json, base::BindOnce(
      [](UpdateSignatureCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DeleteSignature(const std::string& id, DeleteSignatureCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteSignature(id, base::BindOnce(
      [](DeleteSignatureCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListTemplates(ListTemplatesCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListTemplates(base::BindOnce(
      [](ListTemplatesCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::CreateTemplate(const std::string& request_json, CreateTemplateCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->CreateTemplate(request_json, base::BindOnce(
      [](CreateTemplateCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::UpdateTemplate(const std::string& id, const std::string& request_json, UpdateTemplateCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->UpdateTemplate(id, request_json, base::BindOnce(
      [](UpdateTemplateCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DeleteTemplate(const std::string& id, DeleteTemplateCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteTemplate(id, base::BindOnce(
      [](DeleteTemplateCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::GetEmailSummary(const std::string& account_id, const std::string& request_json, GetEmailSummaryCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GetEmailSummary(account_id, request_json, base::BindOnce(
      [](GetEmailSummaryCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::GetReplyDraft(const std::string& account_id, const std::string& request_json, GetReplyDraftCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GetReplyDraft(account_id, request_json, base::BindOnce(
      [](GetReplyDraftCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::AdjustTone(const std::string& request_json, AdjustToneCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->AdjustTone(request_json, base::BindOnce(
      [](AdjustToneCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ClassifyEmail(const std::string& account_id, const std::string& request_json, ClassifyEmailCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ClassifyEmail(account_id, request_json, base::BindOnce(
      [](ClassifyEmailCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::NaturalLanguageSearch(const std::string& request_json, NaturalLanguageSearchCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->NaturalLanguageSearch(request_json, base::BindOnce(
      [](NaturalLanguageSearchCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::GetAiActionHistory(const std::string& account_id, int64_t limit, GetAiActionHistoryCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GetAiActionHistory(account_id, limit, base::BindOnce(
      [](GetAiActionHistoryCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SaveAiConfig(const std::string& config_json, SaveAiConfigCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SaveAiConfig(config_json, base::BindOnce(
      [](SaveAiConfigCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::GetAiConfig(const std::string& feature, GetAiConfigCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GetAiConfig(feature, base::BindOnce(
      [](GetAiConfigCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DeleteAiConfig(const std::string& feature, DeleteAiConfigCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteAiConfig(feature, base::BindOnce(
      [](DeleteAiConfigCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::TestAiConnection(const std::string& config_json, TestAiConnectionCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->TestAiConnection(config_json, base::BindOnce(
      [](TestAiConnectionCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::GetAutoDraftForEmail(const std::string& account_id, const std::string& email_id, GetAutoDraftForEmailCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GetAutoDraftForEmail(account_id, email_id, base::BindOnce(
      [](GetAutoDraftForEmailCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::UpdateAutoDraftStatus(const std::string& draft_id, const std::string& status, UpdateAutoDraftStatusCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->UpdateAutoDraftStatus(draft_id, status, base::BindOnce(
      [](UpdateAutoDraftStatusCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::TranslateText(const std::string& text, const std::string& target_lang, const std::string& source_lang, TranslateTextCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->TranslateText(text, target_lang, source_lang, base::BindOnce(
      [](TranslateTextCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::GeneratePgpKey(const std::string& request_json, GeneratePgpKeyCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GeneratePgpKey(request_json, base::BindOnce(
      [](GeneratePgpKeyCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ImportPgpKey(const std::string& request_json, ImportPgpKeyCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ImportPgpKey(request_json, base::BindOnce(
      [](ImportPgpKeyCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ExportPgpKey(const std::string& key_id, bool include_private, ExportPgpKeyCallback callback) {
  maho::MahoMailService* service =
      backend_for_testing_ ? nullptr : GetMailService();
  if (!backend_for_testing_ && !service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  maho::mail::DispatchPublicPgpExport(
      key_id,
      base::BindOnce(
          [](ExportPgpKeyCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, std::move(result));
          },
          std::move(callback)),
      base::BindOnce(
          [](MahoMailPageHandler::Backend* test_backend,
             maho::MahoMailService* service, const std::string& safe_key_id,
             bool include_private, maho::mail::GenericBackendReply reply) {
            if (test_backend) {
              test_backend->ExportPgpKey(safe_key_id, include_private,
                                         std::move(reply));
              return;
            }
            service->ExportPgpKey(safe_key_id, include_private, std::move(reply));
          },
          base::Unretained(backend_for_testing_), base::Unretained(service)));
}

void MahoMailPageHandler::ListPgpKeys(const std::string& account_id, ListPgpKeysCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListPgpKeys(account_id, base::BindOnce(
      [](ListPgpKeysCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DeletePgpKey(const std::string& key_id, DeletePgpKeyCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeletePgpKey(key_id, base::BindOnce(
      [](DeletePgpKeyCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SetDefaultPgpKey(const std::string& account_id, const std::string& key_id, SetDefaultPgpKeyCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SetDefaultPgpKey(account_id, key_id, base::BindOnce(
      [](SetDefaultPgpKeyCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::EncryptEmailPgp(const std::string& request_json, EncryptEmailPgpCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->EncryptEmailPgp(request_json, base::BindOnce(
      [](EncryptEmailPgpCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::EncryptAttachmentPgp(const std::string& request_json, EncryptAttachmentPgpCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->EncryptAttachmentPgp(request_json, base::BindOnce(
      [](EncryptAttachmentPgpCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DecryptEmailPgp(const std::string& request_json, DecryptEmailPgpCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DecryptEmailPgp(request_json, base::BindOnce(
      [](DecryptEmailPgpCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SignEmailPgp(const std::string& request_json, SignEmailPgpCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SignEmailPgp(request_json, base::BindOnce(
      [](SignEmailPgpCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::VerifyEmailPgp(const std::string& request_json, VerifyEmailPgpCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->VerifyEmailPgp(request_json, base::BindOnce(
      [](VerifyEmailPgpCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ImportSmimeIdentity(const std::string& request_json, ImportSmimeIdentityCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ImportSmimeIdentity(request_json, base::BindOnce(
      [](ImportSmimeIdentityCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListSmimeIdentities(const std::string& account_id, ListSmimeIdentitiesCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListSmimeIdentities(account_id, base::BindOnce(
      [](ListSmimeIdentitiesCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DeleteSmimeIdentity(const std::string& id, DeleteSmimeIdentityCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteSmimeIdentity(id, base::BindOnce(
      [](DeleteSmimeIdentityCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SetDefaultSmimeIdentity(const std::string& account_id, const std::string& identity_id, SetDefaultSmimeIdentityCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SetDefaultSmimeIdentity(account_id, identity_id, base::BindOnce(
      [](SetDefaultSmimeIdentityCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ExportSmimeCert(const std::string& id, ExportSmimeCertCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ExportSmimeCert(id, base::BindOnce(
      [](ExportSmimeCertCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SignEmailSmime(const std::string& request_json, SignEmailSmimeCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SignEmailSmime(request_json, base::BindOnce(
      [](SignEmailSmimeCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::EncryptEmailSmime(const std::string& request_json, EncryptEmailSmimeCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->EncryptEmailSmime(request_json, base::BindOnce(
      [](EncryptEmailSmimeCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DecryptEmailSmime(const std::string& request_json, DecryptEmailSmimeCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DecryptEmailSmime(request_json, base::BindOnce(
      [](DecryptEmailSmimeCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::VerifyEmailSmime(const std::string& signed_body, VerifyEmailSmimeCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->VerifyEmailSmime(signed_body, base::BindOnce(
      [](VerifyEmailSmimeCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::CleanupSmimeForAccount(const std::string& account_id, CleanupSmimeForAccountCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->CleanupSmimeForAccount(account_id, base::BindOnce(
      [](CleanupSmimeForAccountCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ImportCalendarEvent(const std::string& account_id, const std::string& email_id, const std::string& ics_data, ImportCalendarEventCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ImportCalendarEvent(account_id, email_id, ics_data, base::BindOnce(
      [](ImportCalendarEventCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListCalendarEvents(const std::string& account_id, const std::string& from_date, const std::string& to_date, ListCalendarEventsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListCalendarEvents(account_id, from_date, to_date, base::BindOnce(
      [](ListCalendarEventsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::GetCalendarEvent(const std::string& event_id, GetCalendarEventCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GetCalendarEvent(event_id, base::BindOnce(
      [](GetCalendarEventCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::UpdateRsvp(const std::string& event_id, const std::string& rsvp_status, UpdateRsvpCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->UpdateRsvp(event_id, rsvp_status, base::BindOnce(
      [](UpdateRsvpCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::GenerateRsvpReply(const std::string& event_id, const std::string& rsvp_status, const std::string& account_email, GenerateRsvpReplyCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GenerateRsvpReply(event_id, rsvp_status, account_email, base::BindOnce(
      [](GenerateRsvpReplyCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DeleteCalendarEvent(const std::string& event_id, const std::string& delete_scope, DeleteCalendarEventCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteCalendarEvent(event_id, delete_scope, base::BindOnce(
      [](DeleteCalendarEventCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::AutoImportCalendarEvents(const std::string& account_id, const std::string& email_id, const std::string& body_html, const std::string& body_text, AutoImportCalendarEventsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->AutoImportCalendarEvents(account_id, email_id, body_html, body_text, base::BindOnce(
      [](AutoImportCalendarEventsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::CreateCalendarEvent(const std::string& request_json, CreateCalendarEventCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->CreateCalendarEvent(request_json, base::BindOnce(
      [](CreateCalendarEventCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::UpdateCalendarEvent(const std::string& request_json, UpdateCalendarEventCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->UpdateCalendarEvent(request_json, base::BindOnce(
      [](UpdateCalendarEventCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SearchCalendarEvents(const std::string& account_id, const std::string& query, int64_t limit, SearchCalendarEventsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SearchCalendarEvents(account_id, query, limit, base::BindOnce(
      [](SearchCalendarEventsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::CheckEventConflict(const std::string& account_id, const std::string& dtstart, const std::string& dtend, const std::string& exclude_event_id, CheckEventConflictCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->CheckEventConflict(account_id, dtstart, dtend, exclude_event_id, base::BindOnce(
      [](CheckEventConflictCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DuplicateCalendarEvent(const std::string& event_id, DuplicateCalendarEventCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DuplicateCalendarEvent(event_id, base::BindOnce(
      [](DuplicateCalendarEventCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::GoogleCalendarMoveEvent(const std::string& account_id, const std::string& calendar_id, const std::string& event_id, const std::string& destination_calendar_id, GoogleCalendarMoveEventCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GoogleCalendarMoveEvent(account_id, calendar_id, event_id, destination_calendar_id, base::BindOnce(
      [](GoogleCalendarMoveEventCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ExportCalendarIcs(const std::string& request_json, ExportCalendarIcsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ExportCalendarIcs(request_json, base::BindOnce(
      [](ExportCalendarIcsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListCalendarCategories(const std::string& account_id, ListCalendarCategoriesCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListCalendarCategories(account_id, base::BindOnce(
      [](ListCalendarCategoriesCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::CreateCalendarCategory(const std::string& account_id, const std::string& name, const std::string& color, CreateCalendarCategoryCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->CreateCalendarCategory(account_id, name, color, base::BindOnce(
      [](CreateCalendarCategoryCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::UpdateCalendarCategory(const std::string& id, const std::string& name, const std::string& color, UpdateCalendarCategoryCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->UpdateCalendarCategory(id, name, color, base::BindOnce(
      [](UpdateCalendarCategoryCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::DeleteCalendarCategory(const std::string& id, DeleteCalendarCategoryCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteCalendarCategory(id, base::BindOnce(
      [](DeleteCalendarCategoryCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SyncGoogleCalendar(const std::string& account_id, bool _full_sync, SyncGoogleCalendarCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SyncGoogleCalendar(account_id, _full_sync, base::BindOnce(
      [](SyncGoogleCalendarCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::ListAccountCalendars(const std::string& account_id, ListAccountCalendarsCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListAccountCalendars(account_id, base::BindOnce(
      [](ListAccountCalendarsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SetCalendarVisibility(const std::string& calendar_row_id, bool visible, SetCalendarVisibilityCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SetCalendarVisibility(calendar_row_id, visible, base::BindOnce(
      [](SetCalendarVisibilityCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SubscribeHolidayCalendar(const std::string& account_id, const std::string& locale_code, SubscribeHolidayCalendarCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SubscribeHolidayCalendar(account_id, locale_code, base::BindOnce(
      [](SubscribeHolidayCalendarCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::GoogleCalendarFreeBusy(const std::string& request_json, GoogleCalendarFreeBusyCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GoogleCalendarFreeBusy(request_json, base::BindOnce(
      [](GoogleCalendarFreeBusyCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::GetAppSetting(const std::string& key, GetAppSettingCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GetAppSetting(key, base::BindOnce(
      [](GetAppSettingCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::SetAppSetting(const std::string& key, const std::string& value, SetAppSettingCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->SetAppSetting(key, value, base::BindOnce(
      [](SetAppSettingCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::Md5Hash(const std::string& input, Md5HashCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->Md5Hash(input, base::BindOnce(
      [](Md5HashCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}


  // === [W-C.Additional.Typed] ===
void MahoMailPageHandler::GetAccount(const std::string& account_id, GetAccountCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GetAccount(account_id, base::BindOnce(
      [](GetAccountCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::UpdateAccount(const std::string& request_json, UpdateAccountCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->UpdateAccount(request_json, base::BindOnce(
      [](UpdateAccountCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

  // === [W-C.OpenAttachment] ===
void MahoMailPageHandler::OpenAttachment(
    const std::string& capability_token,
    OpenAttachmentCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->OpenAttachment(
      capability_token,
      base::BindOnce(&MahoMailPageHandler::OnAttachmentCapabilityConsumed,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoMailPageHandler::OnAttachmentCapabilityConsumed(
    OpenAttachmentCallback callback,
    std::unique_ptr<maho::MahoMailAttachmentRegistry::ConsumedAttachment>
        attachment,
    std::string error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!attachment) {
    std::move(callback).Run(false, std::move(error));
    return;
  }
  maho::mail::LaunchConsumedAttachment(
      std::move(attachment),
      base::BindOnce(
          [](OpenAttachmentCallback callback, bool ok, std::string error) {
            std::move(callback).Run(ok, std::move(error));
          },
          std::move(callback)));
}

void MahoMailPageHandler::SaveAttachment(
    const std::string& capability_token,
    SaveAttachmentCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable", std::string());
    return;
  }
  service->SaveAttachment(
      capability_token,
      base::BindOnce(
          [](SaveAttachmentCallback callback, bool ok, std::string error,
             std::string saved_path) {
            std::move(callback).Run(ok, std::move(error), std::move(saved_path));
          },
          std::move(callback)));
}

void MahoMailPageHandler::GetDownloadDir(GetDownloadDirCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::FilePath download_path = profile_->GetPrefs()->GetFilePath(prefs::kDownloadDefaultDirectory);
  std::move(callback).Run(download_path.AsUTF8Unsafe());
}

  // === [W-C.Additional.Refresh.Typed] ===
void MahoMailPageHandler::RefreshOAuthToken(const std::string& account_id, RefreshOAuthTokenCallback callback) {
  maho::MahoMailService* service = GetMailService();
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->RefreshOAuthToken(account_id, base::BindOnce(
      [](RefreshOAuthTokenCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoMailPageHandler::CallBackend(const std::string& command,
                                      const std::string& args_json,
                                      CallBackendCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      backend_for_testing_ ? nullptr : GetMailService();
  if (!backend_for_testing_ && !service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  maho::mail::DispatchGenericBackendCall(
      command, args_json,
      base::BindOnce(
          [](CallBackendCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, std::move(result));
          },
          std::move(callback)),
      base::BindOnce(
          [](MahoMailPageHandler::Backend* test_backend,
             maho::MahoMailService* service, const std::string& safe_command,
             const std::string& safe_args,
             maho::mail::GenericBackendReply reply) {
            if (test_backend) {
              test_backend->CallBackend(safe_command, safe_args,
                                        std::move(reply));
              return;
            }
            service->CallBackend(safe_command, safe_args, std::move(reply));
          },
          base::Unretained(backend_for_testing_), base::Unretained(service)));
}
