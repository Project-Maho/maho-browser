// Copyright 2026 Maho Browser. All rights reserved.

#include <map>
#include <string>
#include <vector>

#include "build/build_config.h"

#if BUILDFLAG(IS_LINUX)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#if BUILDFLAG(IS_MAC)
#include "sandbox/mac/seatbelt.h"
#endif
#if BUILDFLAG(IS_LINUX)
#include "maho/browser/mail_helper/maho_mail_helper_sandbox_linux.h"
#endif

#include "base/files/file_path.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/values.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/threading/thread.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_MAC)
#include "base/apple/foundation_util.h"
#endif
#include "maho/browser/mail_helper/maho_mail_helper.mojom.h"
#include "maho/browser/mail_helper/maho_mail_helper_version.h"
#include "maho/browser/mail_helper/maho_mail_read_bridge.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/core/embedder/scoped_ipc_support.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include "mojo/public/cpp/system/invitation.h"
#include "third_party/crashpad/crashpad/client/crashpad_client.h"

namespace {

#if BUILDFLAG(IS_MAC)
constexpr char kBaseBundleIdSwitch[] = "maho-mail-base-bundle-id";
#endif

// Command-line switch through which the host launcher passes the Crashpad
// database directory (shared with the browser's crash pipeline).
constexpr char kCrashpadDatabaseSwitch[] = "maho-mail-crashpad-database";
#if BUILDFLAG(IS_LINUX)
constexpr char kMailRootSwitch[] = "maho-mail-root";
constexpr char kOAuthLoopbackFdEnv[] = "MAHO_MAIL_OAUTH_LOOPBACK_FD";
#endif

// Name of the shared Crashpad handler executable shipped alongside the browser.
#if BUILDFLAG(IS_WIN)
constexpr char kCrashpadHandlerName[] = "chrome_crashpad_handler.exe";
#else
constexpr char kCrashpadHandlerName[] = "chrome_crashpad_handler";
#endif

// Installs the Crashpad handler so native crashes AND Rust panics/aborts
// (which lower to SIGABRT / structured exceptions) in this helper feed the
// same crash pipeline as the browser. Best-effort: logs and continues if the
// handler or database cannot be resolved so a crash-reporting failure never
// prevents the mail helper from starting.
[[maybe_unused]] void InstallCrashpadHandler(
    const base::CommandLine& cmd_line) {
  base::FilePath exe_dir;
  if (!base::PathService::Get(base::DIR_EXE, &exe_dir)) {
    LOG(ERROR) << "[MahoMailHelper] Crashpad: failed to resolve DIR_EXE.";
    return;
  }

  base::FilePath handler_path = exe_dir.AppendASCII(kCrashpadHandlerName);
  if (!base::PathExists(handler_path)) {
    LOG(WARNING) << "[MahoMailHelper] Crashpad handler is not present at "
                 << handler_path.value() << "; continuing without crash "
                    "reporting.";
    return;
  }

  base::FilePath database_path =
      cmd_line.GetSwitchValuePath(kCrashpadDatabaseSwitch);
  if (database_path.empty()) {
    database_path = exe_dir.AppendASCII("Crashpad");
  }

  crashpad::CrashpadClient client;
  const std::map<std::string, std::string> annotations = {
      {"ptype", "maho-mail-helper"},
      {"maho-mail-helper-version", maho::mail_helper::kMahoMailHelperVersion},
  };
  const std::vector<std::string> arguments = {"--no-rate-limit"};

  if (!client.StartHandler(handler_path, database_path,
                           /*metrics_dir=*/base::FilePath(),
                           /*url=*/std::string(), annotations, arguments,
                           /*restartable=*/true,
                           /*asynchronous_start=*/false)) {
    LOG(ERROR) << "[MahoMailHelper] Crashpad handler failed to start; "
                  "continuing without crash reporting.";
    return;
  }
  LOG(INFO) << "[MahoMailHelper] Crashpad handler installed.";
}


#if BUILDFLAG(IS_MAC)
// Seatbelt profile for the mail helper. Deny-by-default, then allow exactly
// what mail needs. Each allowance below is required by observed runtime
// behavior, not speculation:
//   - the MahoMail subtree holds the SQLCipher DB (mail-core opens
//     <profile>/MahoMail/maho_mail.db)
//   - DNS resolution reads resolver config and talks to mDNSResponder
//     (imap_client resolves hosts via ToSocketAddrs)
//   - TLS verification loads the system trust store through Security.framework
//     (rustls-native-certs), which needs trustd/ocspd
//   - OAuth uses a loopback listener, so local bind/accept is required
// Process creation is denied outright: that is the payoff, since a remote
// MIME/IMAP parsing bug then cannot escalate into code execution.
constexpr char kMailHelperSandboxProfile[] = R"(
(version 1)
(deny default (with no-log))

(allow process-info* (target self))
(allow sysctl-read)
(allow signal (target self))

; Executable/library loading and the app bundle itself.
(allow file-read* file-map-executable
  (subpath "/System")
  (subpath "/usr/lib")
  (subpath "/Library/Apple")
  (subpath (param "BUNDLE_PATH")))
(allow file-read-metadata (subpath "/"))

; Randomness, tty-less logging, timezone.
(allow file-read* (literal "/dev/urandom") (literal "/dev/random"))
(allow file-read* (subpath "/private/var/db/timezone"))
(allow file-read* (literal "/etc/localtime"))

; Mail data lives entirely under the profile directory.
(allow file-read* file-write* (subpath (param "PROFILE_PATH")))

; Crash reports.
(allow file-read* file-write* (subpath (param "CRASHPAD_PATH")))

; DNS resolution.
(allow file-read*
  (literal "/etc/hosts")
  (literal "/etc/resolv.conf")
  (literal "/private/etc/hosts")
  (literal "/private/etc/resolv.conf")
  (subpath "/private/var/run/resolv.conf"))
(allow network-outbound (literal "/private/var/run/mDNSResponder"))
(allow system-socket)

; IMAP/SMTP/OAuth HTTPS plus the OAuth loopback listener.
(allow network-outbound
  (control-name "com.apple.netsrc")
  (remote udp)
  (remote tcp))
(allow network-bind (local tcp "localhost:*"))
(allow network-inbound (local tcp "localhost:*"))

; System TLS trust store evaluation.
(allow file-read*
  (subpath "/Library/Keychains")
  (subpath "/private/var/db/mds")
  (subpath "/System/Library/Keychains"))
(allow mach-lookup
  (global-name "com.apple.SecurityServer")
  (global-name "com.apple.trustd")
  (global-name "com.apple.trustd.agent")
  (global-name "com.apple.ocspd")
  (global-name "com.apple.SystemConfiguration.configd")
  (global-name "com.apple.SystemConfiguration.DNSConfiguration")
  (global-name "com.apple.system.notification_center")
  (global-name "com.apple.system.logger")
  (global-name "com.apple.system.opendirectoryd.membership"))
)";

// Enters the Seatbelt sandbox exactly once. Returns false when entry fails so
// the caller can fail closed instead of serving mail unconfined.
bool EnterMailHelperSandbox(const std::string& profile_path) {
  static bool entered = false;
  if (entered) {
    return true;
  }
  if (profile_path.empty()) {
    LOG(ERROR) << "[MahoMailHelper] Refusing to sandbox with empty profile.";
    return false;
  }

  base::FilePath bundle_path;
  if (!base::PathService::Get(base::DIR_EXE, &bundle_path)) {
    LOG(ERROR) << "[MahoMailHelper] Cannot resolve DIR_EXE for sandbox.";
    return false;
  }

  base::FilePath crashpad_path =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          kCrashpadDatabaseSwitch);

  // Keep the helper below <profile>/MahoMail. Browser-owned attachment launch
  // copies live in a sibling directory that this sandbox cannot reach.
  const base::FilePath mail_root =
      base::FilePath::FromUTF8Unsafe(profile_path).AppendASCII("MahoMail");
  if (crashpad_path.empty()) {
    crashpad_path = mail_root.AppendASCII("Crashpad");
  }
  // InitWithParams takes a flat key,value,key,value,... list.
  const std::vector<std::string> params = {
      "PROFILE_PATH",  mail_root.value(),
      "BUNDLE_PATH",   bundle_path.value(),
      "CRASHPAD_PATH", crashpad_path.value(),
  };
  std::string error;
  const bool ok = sandbox::Seatbelt::InitWithParams(
      kMailHelperSandboxProfile, 0, params, &error);
  if (!ok) {
    LOG(ERROR) << "[MahoMailHelper] Seatbelt init failed: " << error;
    return false;
  }
  entered = true;
  LOG(INFO) << "[MahoMailHelper] Sandbox active.";
  return true;
}
#elif !BUILDFLAG(IS_LINUX)
bool EnterMailHelperSandbox(const std::string& profile_path) {
  // Sandboxing for other platforms is tracked separately; entry is a no-op so
  // the fail-closed check above does not block non-mac builds.
  return true;
}
#endif  // BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_LINUX)
base::ScopedFD CreateOAuthLoopbackListener() {
  base::ScopedFD listener(
      socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP));
  if (!listener.is_valid()) {
    PLOG(ERROR) << "[MahoMailHelper] OAuth loopback socket failed";
    return {};
  }
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (bind(listener.get(), reinterpret_cast<sockaddr*>(&address),
           sizeof(address)) != 0 ||
      listen(listener.get(), 4) != 0) {
    PLOG(ERROR) << "[MahoMailHelper] OAuth loopback bind/listen failed";
    return {};
  }
  return listener;
}
#endif

}  // namespace

namespace maho {

class MahoMailHelperImpl;
MahoMailHelperImpl* g_helper_impl = nullptr;

class MahoMailHelperImpl : public mojom::MahoMailHelper {
 public:
  MahoMailHelperImpl(mojo::PendingReceiver<mojom::MahoMailHelper> receiver,
                     base::OnceClosure quit_closure)
      : receiver_(this, std::move(receiver)),
        quit_closure_(std::move(quit_closure)) {
    g_helper_impl = this;
    // Self-terminate if the browser end of the Mojo pipe drops (browser crash,
    // SIGKILL, or any exit that skips the graceful Shutdown() drain). Without
    // this the helper's RunLoop never quits on abnormal parent exit and the
    // process is reparented to launchd (ppid=1), leaking one helper per
    // non-graceful browser teardown. macOS has no parent-death signal, so the
    // pipe disconnect is the only reliable orphan-prevention hook.
    receiver_.set_disconnect_handler(base::BindOnce(
        &MahoMailHelperImpl::OnBrowserDisconnected, base::Unretained(this)));
    static const base::NoDestructor<
        scoped_refptr<base::SingleThreadTaskRunner>>
        main_runner(base::SingleThreadTaskRunner::GetCurrentDefault());
    MahoMailRegisterEventCallback([](const char* event_type, const char* account_id, const char* provider, const char* reason) {
      std::string ev(event_type);
      std::string acc(account_id);
      std::string prov(provider);
      std::string reas(reason);
      (*main_runner)->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](std::string ev, std::string acc, std::string prov, std::string reas) {
                if (g_helper_impl) {
                  g_helper_impl->OnEventReceived(ev, acc, prov, reas);
                }
              },
              std::move(ev), std::move(acc), std::move(prov), std::move(reas)));
    });
    MahoMailRegisterNewMailCallback(
        [](const char* account_id, const char* email_id,
           const char* message_id, const char* sender, const char* subject,
           uint64_t cursor, uint64_t epoch) {
          std::string acc(account_id);
          std::string eid(email_id);
          std::string mid(message_id);
          std::string from(sender);
          std::string subj(subject);
          (*main_runner)->PostTask(
              FROM_HERE,
              base::BindOnce(
                  [](std::string acc, std::string eid, std::string mid,
                     std::string from, std::string subj, uint64_t cursor,
                     uint64_t epoch) {
                    if (g_helper_impl && g_helper_impl->client_.is_bound()) {
                      g_helper_impl->client_->OnNewMail(
                          acc, eid, mid, from, subj, cursor, epoch);
                    }
                  },
                  std::move(acc), std::move(eid), std::move(mid),
                  std::move(from), std::move(subj), cursor, epoch));
        });
  }

  ~MahoMailHelperImpl() override {
    g_helper_impl = nullptr;
  }

  // Browser-side pipe dropped without a graceful Shutdown() call. Quit the
  // RunLoop so the helper process exits instead of lingering as an orphan.
  void OnBrowserDisconnected() {
    LOG(WARNING) << "[MahoMailHelper] Browser pipe disconnected; "
                    "self-terminating to avoid orphaned helper process.";
    if (quit_closure_) {
      std::move(quit_closure_).Run();
    }
  }

  void OnEventReceived(const std::string& ev,
                       const std::string& acc,
                       const std::string& prov,
                       const std::string& reas) {
    if (!client_.is_bound()) {
      return;
    }
    if (ev == "auth-reauth-required") {
      client_->OnAuthRequired(acc, prov, reas);
    } else if (ev == "auth-refresh-succeeded") {
      client_->OnAuthRefreshSucceeded(acc);
    } else if (ev == "accounts-changed") {
      client_->OnAccountsChanged();
    } else if (ev == "mutation") {
      client_->OnMutation(acc, reas);
    } else if (ev == "outbox") {
      client_->OnOutbox(acc, reas);
    } else if (ev == "scheduler") {
      client_->OnScheduler(acc, reas);
    } else if (ev == "agent-stream") {
      client_->OnAgentStream(acc, reas);
    } else if (ev == "calendar") {
      client_->OnCalendar(acc, reas);
    } else if (ev == "import") {
      client_->OnImport(reas);
    } else if (ev == "sync-event") {
      client_->OnSyncEvent(prov, reas);
    } else if (ev == "backfill-event") {
      client_->OnBackfillEvent(prov, reas);
    } else if (ev == "status-changed") {
      client_->OnStatusChanged(acc, prov == "connected");
    }
  }

  // mojom::MahoMailHelper:
  void Initialize(const std::string& version_token,
                  const std::string& profile_path,
                  InitializeCallback callback) override {
    LOG(INFO) << "[MahoMailHelper] Initializing. Version=" << version_token
              << " Profile=" << profile_path;
#if BUILDFLAG(IS_LINUX)
    const base::FilePath expected_mail_root =
        base::FilePath::FromUTF8Unsafe(profile_path).AppendASCII("MahoMail");
    if (!expected_mail_root.IsAbsolute() || expected_mail_root.ReferencesParent() ||
        expected_mail_root.StripTrailingSeparators() !=
            base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
                kMailRootSwitch)) {
      LOG(ERROR) << "[MahoMailHelper] Initialize profile disagrees with the "
                    "trusted Mail-root launch contract.";
      std::move(callback).Run(false);
      return;
    }
#else
    if (!EnterMailHelperSandbox(profile_path)) {
      // Fail closed: running unsandboxed would defeat the isolation, so report
      // failure and let the browser-side launcher give up after its bounded
      // respawn attempts rather than serving mail from an unconfined process.
      LOG(ERROR) << "[MahoMailHelper] Sandbox entry failed; refusing to serve.";
      std::move(callback).Run(false);
      return;
    }
#endif
    bool success = MahoMailInitialize(version_token.c_str(), profile_path.c_str());
    std::move(callback).Run(success);
  }

  void InjectKeys(const std::string& sqlcipher_key,
                  const std::string& credential_key,
                  InjectKeysCallback callback) override {
    LOG(INFO) << "[MahoMailHelper] Injecting DB keys.";
    bool success = MahoMailInjectKeys(sqlcipher_key.c_str(), credential_key.c_str());
    std::move(callback).Run(success);
    // Resume sync+backfill for pre-existing accounts. Idempotent in Rust and a
    // fast non-blocking dispatch, so safe to call inline on the Mojo thread.
    if (success) {
      MahoMailStartAllAccountSync();
    }
  }

  void StartSync(const std::string& account_id, StartSyncCallback callback) override {
    LOG(INFO) << "[MahoMailHelper] Starting sync for account: " << account_id;
    bool success = MahoMailStartSync(account_id.c_str());
    std::move(callback).Run(success);
  }

  void StopSync(StopSyncCallback callback) override {
    LOG(INFO) << "[MahoMailHelper] Stopping sync.";
    bool success = MahoMailStopSync();
    std::move(callback).Run(success);
  }

  void StartBackfill(const std::string& account_id, StartBackfillCallback callback) override {
    LOG(INFO) << "[MahoMailHelper] Starting backfill for account: " << account_id;
    bool success = MahoMailStartBackfill(account_id.c_str());
    std::move(callback).Run(success);
  }

  void GetVersion(GetVersionCallback callback) override {
    std::move(callback).Run(maho::mail_helper::kMahoMailHelperVersion);
  }

  void PrepareShutdown(PrepareShutdownCallback callback) override {
    LOG(INFO) << "[MahoMailHelper] PrepareShutdown requested; draining backend.";
    // No MahoMailPrepareShutdown / MahoMailDrainBackend FFI exists in
    // mail-core (checked ffi.rs, cbindgen.toml, and both generated headers),
    // so none is invented here. Best-effort drain with the available
    // synchronous primitive: MahoMailStopSync() stops all sync + backfill
    // workers (sets their stop flags and aborts their tokio tasks). It is
    // non-blocking and safe inline on the Mojo thread -- the StopSync handler
    // below already calls it the same way. The browser-side launcher bounds
    // the overall graceful drain with its shutdown timer, so the callback
    // always fires promptly after the drain is attempted.
    MahoMailStopSync();
    std::move(callback).Run();
  }

  void Shutdown(ShutdownCallback callback) override {
    LOG(INFO) << "[MahoMailHelper] Shutdown requested.";
    std::move(callback).Run();
    if (quit_closure_) {
      std::move(quit_closure_).Run();
    }
  }

  void ListAccounts(ListAccountsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(&MahoMailListAccounts),
        base::BindOnce(
            [](ListAccountsCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void ListFolders(const std::string& account_id,
                   ListFoldersCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, MahoMailReadCallback cb, void* ud) {
              return MahoMailListFolders(id.c_str(), cb, ud);
            },
            account_id),
        base::BindOnce(
            [](ListFoldersCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void ListEmails(const std::string& account_id,
                  const std::string& folder_id,
                  int64_t limit,
                  int64_t offset,
                  ListEmailsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string aid, std::string fid, int64_t lim, int64_t off,
               MahoMailReadCallback cb, void* ud) {
              return MahoMailListEmails(aid.c_str(), fid.c_str(), lim, off, cb,
                                       ud);
            },
            account_id, folder_id, limit, offset),
        base::BindOnce(
            [](ListEmailsCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void GetEmail(const std::string& email_id,
                GetEmailCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, MahoMailReadCallback cb, void* ud) {
              return MahoMailGetEmail(id.c_str(), cb, ud);
            },
            email_id),
        base::BindOnce(
            [](GetEmailCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void SearchEmails(const std::string& query_json,
                    SearchEmailsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string qj, MahoMailReadCallback cb, void* ud) {
              return MahoMailSearchEmails(qj.c_str(), cb, ud);
            },
            query_json),
        base::BindOnce(
            [](SearchEmailsCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void ListThread(const std::string& account_id,
                  const std::string& message_id,
                  ListThreadCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string aid, std::string mid, MahoMailReadCallback cb,
               void* ud) { return MahoMailListThread(aid.c_str(), mid.c_str(), cb, ud); },
            account_id, message_id),
        base::BindOnce(
            [](ListThreadCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void AddAccount(const std::string& request_json, AddAccountCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string req, MahoMailReadCallback cb, void* ud) {
              return MahoMailAddAccount(req.c_str(), cb, ud);
            },
            request_json),
        base::BindOnce(
            [](AddAccountCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void TestConnection(const std::string& params_json, TestConnectionCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string params, MahoMailReadCallback cb, void* ud) {
              return MahoMailTestConnection(params.c_str(), cb, ud);
            },
            params_json),
        base::BindOnce(
            [](TestConnectionCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void DeleteAccount(const std::string& account_id, DeleteAccountCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, MahoMailReadCallback cb, void* ud) {
              return MahoMailDeleteAccount(id.c_str(), cb, ud);
            },
            account_id),
        base::BindOnce(
            [](DeleteAccountCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void OAuthStartUrl(const std::string& provider,
                     const std::string& client_id,
                     const std::string& redirect_uri,
                     const std::string& options_json,
                     OAuthStartUrlCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string prov, std::string cid, std::string redir,
               std::string options, MahoMailReadCallback cb, void* ud) {
              return options == "{}"
                         ? MahoMailOAuthStartUrl(prov.c_str(), cid.c_str(),
                                                 redir.c_str(), cb, ud)
                         : MahoMailOAuthStartUrlWithOptions(
                               prov.c_str(), cid.c_str(), redir.c_str(),
                               options.c_str(), cb, ud);
            },
            provider, client_id, redirect_uri, options_json),
        base::BindOnce(
            [](OAuthStartUrlCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void OAuthLoopbackSignIn(const std::string& provider,
                           const std::string& options_json,
                           OAuthLoopbackSignInCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string prov, std::string options,
               MahoMailReadCallback cb, void* ud) {
              return MahoMailOAuthLoopbackSignIn(
                  prov.c_str(), options.c_str(), cb, ud);
            },
            provider, options_json),
        base::BindOnce(
            [](OAuthLoopbackSignInCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void OAuthComplete(const std::string& state,
                     const std::string& code,
                     OAuthCompleteCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string st, std::string cd, MahoMailReadCallback cb, void* ud) {
              return MahoMailOAuthComplete(st.c_str(), cd.c_str(), cb, ud);
            },
            state, code),
        base::BindOnce(
            [](OAuthCompleteCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void ReconnectAccount(const std::string& account_id, ReconnectAccountCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, MahoMailReadCallback cb, void* ud) {
              return MahoMailReconnectAccount(id.c_str(), cb, ud);
            },
            account_id),
        base::BindOnce(
            [](ReconnectAccountCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void OAuthCancel(const std::string& state, OAuthCancelCallback callback) override {
    bool accepted = MahoMailOAuthCancel(state.c_str());
    std::move(callback).Run(accepted);
  }

  void ImportMigrationArchive(const std::string& archive_json, ImportMigrationArchiveCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string json, MahoMailReadCallback cb, void* ud) {
              return MahoMailImportMigrationArchive(json.c_str(), cb, ud);
            },
            archive_json),
        base::BindOnce(
            [](ImportMigrationArchiveCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void MarkRead(const std::string& email_id, MarkReadCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, MahoMailReadCallback cb, void* ud) {
              return MahoMailMarkRead(id.c_str(), cb, ud);
            },
            email_id),
        base::BindOnce(
            [](MarkReadCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void MarkUnread(const std::string& email_id, MarkUnreadCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, MahoMailReadCallback cb, void* ud) {
              return MahoMailMarkUnread(id.c_str(), cb, ud);
            },
            email_id),
        base::BindOnce(
            [](MarkUnreadCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void ToggleStar(const std::string& email_id, ToggleStarCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, MahoMailReadCallback cb, void* ud) {
              return MahoMailToggleStar(id.c_str(), cb, ud);
            },
            email_id),
        base::BindOnce(
            [](ToggleStarCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void DeleteEmail(const std::string& email_id, DeleteEmailCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, MahoMailReadCallback cb, void* ud) {
              return MahoMailDeleteEmail(id.c_str(), cb, ud);
            },
            email_id),
        base::BindOnce(
            [](DeleteEmailCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void MoveEmail(const std::string& email_id, const std::string& target_folder_id, MoveEmailCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, std::string target, MahoMailReadCallback cb, void* ud) {
              return MahoMailMoveEmail(id.c_str(), target.c_str(), cb, ud);
            },
            email_id, target_folder_id),
        base::BindOnce(
            [](MoveEmailCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void BatchMarkRead(const std::string& request_json, BatchMarkReadCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string req, MahoMailReadCallback cb, void* ud) {
              return MahoMailBatchMarkRead(req.c_str(), cb, ud);
            },
            request_json),
        base::BindOnce(
            [](BatchMarkReadCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void BatchMarkUnread(const std::string& request_json, BatchMarkUnreadCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string req, MahoMailReadCallback cb, void* ud) {
              return MahoMailBatchMarkUnread(req.c_str(), cb, ud);
            },
            request_json),
        base::BindOnce(
            [](BatchMarkUnreadCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void BatchDelete(const std::string& request_json, BatchDeleteCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string req, MahoMailReadCallback cb, void* ud) {
              return MahoMailBatchDelete(req.c_str(), cb, ud);
            },
            request_json),
        base::BindOnce(
            [](BatchDeleteCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void BatchMove(const std::string& request_json, BatchMoveCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string req, MahoMailReadCallback cb, void* ud) {
              return MahoMailBatchMove(req.c_str(), cb, ud);
            },
            request_json),
        base::BindOnce(
            [](BatchMoveCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void BatchToggleStar(const std::string& request_json, BatchToggleStarCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string req, MahoMailReadCallback cb, void* ud) {
              return MahoMailBatchToggleStar(req.c_str(), cb, ud);
            },
            request_json),
        base::BindOnce(
            [](BatchToggleStarCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void SyncFolders(const std::string& account_id, SyncFoldersCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, MahoMailReadCallback cb, void* ud) {
              return MahoMailSyncFolders(id.c_str(), cb, ud);
            },
            account_id),
        base::BindOnce(
            [](SyncFoldersCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void SyncFolder(const std::string& account_id, const std::string& folder_id, SyncFolderCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, std::string fid, MahoMailReadCallback cb, void* ud) {
              return MahoMailSyncFolder(id.c_str(), fid.c_str(), cb, ud);
            },
            account_id, folder_id),
        base::BindOnce(
            [](SyncFolderCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void CreateFolder(const std::string& account_id, const std::string& folder_name, CreateFolderCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, std::string name, MahoMailReadCallback cb, void* ud) {
              return MahoMailCreateFolder(id.c_str(), name.c_str(), cb, ud);
            },
            account_id, folder_name),
        base::BindOnce(
            [](CreateFolderCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void RenameFolder(const std::string& account_id, const std::string& folder_id, const std::string& new_name, RenameFolderCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, std::string fid, std::string name, MahoMailReadCallback cb, void* ud) {
              return MahoMailRenameFolder(id.c_str(), fid.c_str(), name.c_str(), cb, ud);
            },
            account_id, folder_id, new_name),
        base::BindOnce(
            [](RenameFolderCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void DeleteFolder(const std::string& account_id, const std::string& folder_id, DeleteFolderCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, std::string fid, MahoMailReadCallback cb, void* ud) {
              return MahoMailDeleteFolder(id.c_str(), fid.c_str(), cb, ud);
            },
            account_id, folder_id),
        base::BindOnce(
            [](DeleteFolderCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void GetFolderCounts(const std::string& account_id, GetFolderCountsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, MahoMailReadCallback cb, void* ud) {
              return MahoMailGetFolderCounts(id.c_str(), cb, ud);
            },
            account_id),
        base::BindOnce(
            [](GetFolderCountsCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void FlushPendingMutations(const std::string& account_id, FlushPendingMutationsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, MahoMailReadCallback cb, void* ud) {
              return MahoMailFlushPendingMutations(id.c_str(), cb, ud);
            },
            account_id),
        base::BindOnce(
            [](FlushPendingMutationsCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void GetPendingMutationCount(GetPendingMutationCountCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](MahoMailReadCallback cb, void* ud) {
              return MahoMailGetPendingMutationCount(cb, ud);
            }),
        base::BindOnce(
            [](GetPendingMutationCountCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void ListPendingMutations(const std::string& account_id, ListPendingMutationsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, MahoMailReadCallback cb, void* ud) {
              return MahoMailListPendingMutations(id.c_str(), cb, ud);
            },
            account_id),
        base::BindOnce(
            [](ListPendingMutationsCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }


  // === [W-C..W-K.Typed] ===
  void StartAllAccountSync(StartAllAccountSyncCallback callback) override {
    bool success = MahoMailStartAllAccountSync();
    std::move(callback).Run(success, success ? "{}" : "{\"error\":\"Failed to start sync\"}");
  }

  void SendEmail(const std::string& request_json, SendEmailCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailSendEmail(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](SendEmailCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SaveDraft(const std::string& request_json, SaveDraftCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailSaveDraft(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](SaveDraftCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void UpdateDraft(const std::string& draft_id, const std::string& request_json, UpdateDraftCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string draft_id, std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailUpdateDraft(draft_id.c_str(), request_json.c_str(), cb, ud);
        }, draft_id, request_json),
        base::BindOnce([](UpdateDraftCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void GetReplyContext(const std::string& email_id, GetReplyContextCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string email_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailGetReplyContext(email_id.c_str(), cb, ud);
        }, email_id),
        base::BindOnce([](GetReplyContextCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void QueueEmail(const std::string& request_json, QueueEmailCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailQueueEmail(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](QueueEmailCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListOutbox(const std::string& account_id, ListOutboxCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListOutbox(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListOutboxCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void RetryOutboxItem(const std::string& item_id, RetryOutboxItemCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string item_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailRetryOutboxItem(item_id.c_str(), cb, ud);
        }, item_id),
        base::BindOnce([](RetryOutboxItemCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DeleteOutboxItem(const std::string& item_id, DeleteOutboxItemCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string item_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailDeleteOutboxItem(item_id.c_str(), cb, ud);
        }, item_id),
        base::BindOnce([](DeleteOutboxItemCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void FlushOutbox(FlushOutboxCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](MahoMailReadCallback cb, void* ud) {
          return MahoMailFlushOutbox(cb, ud);
        }),
        base::BindOnce([](FlushOutboxCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DownloadAttachment(const std::string& account_id, int64_t email_uid, const std::string& folder_id, const std::string& part_id, const std::string& filename, DownloadAttachmentCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, int64_t email_uid, std::string folder_id, std::string part_id, std::string filename, MahoMailReadCallback cb, void* ud) {
          return MahoMailDownloadAttachment(account_id.c_str(), email_uid, folder_id.c_str(), part_id.c_str(), filename.c_str(), cb, ud);
        }, account_id, email_uid, folder_id, part_id, filename),
        base::BindOnce([](DownloadAttachmentCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ExtractOtp(const std::string& account_id, const std::string& folder_id, const std::string& query, int64_t max_age_seconds, ExtractOtpCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string folder_id, std::string query, int64_t max_age_seconds, MahoMailReadCallback cb, void* ud) {
          return MahoMailExtractOtp(account_id.c_str(), folder_id.c_str(), query.c_str(), max_age_seconds, cb, ud);
        }, account_id, folder_id, query, max_age_seconds),
        base::BindOnce([](ExtractOtpCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SnoozeEmail(const std::string& email_id, const std::string& snooze_until, SnoozeEmailCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string email_id, std::string snooze_until, MahoMailReadCallback cb, void* ud) {
          return MahoMailSnoozeEmail(email_id.c_str(), snooze_until.c_str(), cb, ud);
        }, email_id, snooze_until),
        base::BindOnce([](SnoozeEmailCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void UnsnoozeEmail(const std::string& email_id, UnsnoozeEmailCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string email_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailUnsnoozeEmail(email_id.c_str(), cb, ud);
        }, email_id),
        base::BindOnce([](UnsnoozeEmailCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListSnoozedEmails(const std::string& account_id, ListSnoozedEmailsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListSnoozedEmails(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListSnoozedEmailsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SetReminder(const std::string& email_id, const std::string& reminder_at, SetReminderCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string email_id, std::string reminder_at, MahoMailReadCallback cb, void* ud) {
          return MahoMailSetReminder(email_id.c_str(), reminder_at.c_str(), cb, ud);
        }, email_id, reminder_at),
        base::BindOnce([](SetReminderCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ClearReminder(const std::string& email_id, ClearReminderCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string email_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailClearReminder(email_id.c_str(), cb, ud);
        }, email_id),
        base::BindOnce([](ClearReminderCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListReminders(const std::string& account_id, ListRemindersCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListReminders(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListRemindersCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void MuteThread(const std::string& account_id, const std::string& message_id, MuteThreadCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string message_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailMuteThread(account_id.c_str(), message_id.c_str(), cb, ud);
        }, account_id, message_id),
        base::BindOnce([](MuteThreadCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void UnmuteThread(const std::string& account_id, const std::string& message_id, UnmuteThreadCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string message_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailUnmuteThread(account_id.c_str(), message_id.c_str(), cb, ud);
        }, account_id, message_id),
        base::BindOnce([](UnmuteThreadCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void IsThreadMuted(const std::string& account_id, const std::string& message_id, IsThreadMutedCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string message_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailIsThreadMuted(account_id.c_str(), message_id.c_str(), cb, ud);
        }, account_id, message_id),
        base::BindOnce([](IsThreadMutedCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListMutedThreads(const std::string& account_id, ListMutedThreadsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListMutedThreads(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListMutedThreadsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void FilterMutedMessageIds(const std::string& account_id, const std::string& message_ids_json, FilterMutedMessageIdsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string message_ids_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailFilterMutedMessageIds(account_id.c_str(), message_ids_json.c_str(), cb, ud);
        }, account_id, message_ids_json),
        base::BindOnce([](FilterMutedMessageIdsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void PinEmail(const std::string& email_id, PinEmailCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string email_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailPinEmail(email_id.c_str(), cb, ud);
        }, email_id),
        base::BindOnce([](PinEmailCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void UnpinEmail(const std::string& email_id, UnpinEmailCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string email_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailUnpinEmail(email_id.c_str(), cb, ud);
        }, email_id),
        base::BindOnce([](UnpinEmailCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListPinnedEmails(const std::string& account_id, ListPinnedEmailsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListPinnedEmails(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListPinnedEmailsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void CreateMailRule(const std::string& request_json, CreateMailRuleCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailCreateMailRule(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](CreateMailRuleCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void UpdateMailRule(const std::string& request_json, UpdateMailRuleCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailUpdateMailRule(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](UpdateMailRuleCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DeleteMailRule(const std::string& rule_id, DeleteMailRuleCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string rule_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailDeleteMailRule(rule_id.c_str(), cb, ud);
        }, rule_id),
        base::BindOnce([](DeleteMailRuleCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListMailRules(const std::string& account_id, ListMailRulesCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListMailRules(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListMailRulesCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ReorderMailRules(const std::string& account_id, const std::string& rule_ids_json, ReorderMailRulesCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string rule_ids_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailReorderMailRules(account_id.c_str(), rule_ids_json.c_str(), cb, ud);
        }, account_id, rule_ids_json),
        base::BindOnce([](ReorderMailRulesCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListLabels(const std::string& account_id, ListLabelsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListLabels(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListLabelsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void CreateLabel(const std::string& request_json, CreateLabelCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailCreateLabel(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](CreateLabelCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DeleteLabel(const std::string& id, DeleteLabelCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string id, MahoMailReadCallback cb, void* ud) {
          return MahoMailDeleteLabel(id.c_str(), cb, ud);
        }, id),
        base::BindOnce([](DeleteLabelCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void AddLabelToEmail(const std::string& email_id, const std::string& label_id, AddLabelToEmailCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string email_id, std::string label_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailAddLabelToEmail(email_id.c_str(), label_id.c_str(), cb, ud);
        }, email_id, label_id),
        base::BindOnce([](AddLabelToEmailCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void RemoveLabelFromEmail(const std::string& email_id, const std::string& label_id, RemoveLabelFromEmailCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string email_id, std::string label_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailRemoveLabelFromEmail(email_id.c_str(), label_id.c_str(), cb, ud);
        }, email_id, label_id),
        base::BindOnce([](RemoveLabelFromEmailCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListEmailLabels(const std::string& email_id, ListEmailLabelsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string email_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListEmailLabels(email_id.c_str(), cb, ud);
        }, email_id),
        base::BindOnce([](ListEmailLabelsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SaveSearch(const std::string& name, const std::string& query, const std::string& account_id, SaveSearchCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string name, std::string query, std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailSaveSearch(name.c_str(), query.c_str(), account_id.c_str(), cb, ud);
        }, name, query, account_id),
        base::BindOnce([](SaveSearchCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListSavedSearches(const std::string& account_id, ListSavedSearchesCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListSavedSearches(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListSavedSearchesCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DeleteSavedSearch(const std::string& id, DeleteSavedSearchCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string id, MahoMailReadCallback cb, void* ud) {
          return MahoMailDeleteSavedSearch(id.c_str(), cb, ud);
        }, id),
        base::BindOnce([](DeleteSavedSearchCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ScheduleSend(const std::string& request_json, ScheduleSendCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailScheduleSend(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](ScheduleSendCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void CancelScheduledSend(const std::string& id, CancelScheduledSendCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string id, MahoMailReadCallback cb, void* ud) {
          return MahoMailCancelScheduledSend(id.c_str(), cb, ud);
        }, id),
        base::BindOnce([](CancelScheduledSendCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListScheduledSends(const std::string& account_id, ListScheduledSendsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListScheduledSends(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListScheduledSendsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void StartScheduler(StartSchedulerCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](MahoMailReadCallback cb, void* ud) {
          return MahoMailStartScheduler(cb, ud);
        }),
        base::BindOnce([](StartSchedulerCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void StopScheduler(StopSchedulerCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](MahoMailReadCallback cb, void* ud) {
          return MahoMailStopScheduler(cb, ud);
        }),
        base::BindOnce([](StopSchedulerCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SearchContacts(const std::string& account_id, const std::string& query, int64_t limit, SearchContactsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string query, int64_t limit, MahoMailReadCallback cb, void* ud) {
          return MahoMailSearchContacts(account_id.c_str(), query.c_str(), limit, cb, ud);
        }, account_id, query, limit),
        base::BindOnce([](SearchContactsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ToggleVip(const std::string& contact_id, ToggleVipCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string contact_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailToggleVip(contact_id.c_str(), cb, ud);
        }, contact_id),
        base::BindOnce([](ToggleVipCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListVipContacts(const std::string& account_id, ListVipContactsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListVipContacts(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListVipContactsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void PopulateContactsFromHistory(const std::string& account_id, PopulateContactsFromHistoryCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailPopulateContactsFromHistory(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](PopulateContactsFromHistoryCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListContactGroups(const std::string& account_id, ListContactGroupsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListContactGroups(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListContactGroupsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SearchContactGroups(const std::string& account_id, const std::string& query, SearchContactGroupsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string query, MahoMailReadCallback cb, void* ud) {
          return MahoMailSearchContactGroups(account_id.c_str(), query.c_str(), cb, ud);
        }, account_id, query),
        base::BindOnce([](SearchContactGroupsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void CreateContactGroup(const std::string& account_id, const std::string& name, const std::string& member_emails_json, CreateContactGroupCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string name, std::string member_emails_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailCreateContactGroup(account_id.c_str(), name.c_str(), member_emails_json.c_str(), cb, ud);
        }, account_id, name, member_emails_json),
        base::BindOnce([](CreateContactGroupCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void UpdateContactGroup(const std::string& group_id, const std::string& name, const std::string& member_emails_json, UpdateContactGroupCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string group_id, std::string name, std::string member_emails_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailUpdateContactGroup(group_id.c_str(), name.c_str(), member_emails_json.c_str(), cb, ud);
        }, group_id, name, member_emails_json),
        base::BindOnce([](UpdateContactGroupCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DeleteContactGroup(const std::string& group_id, DeleteContactGroupCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string group_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailDeleteContactGroup(group_id.c_str(), cb, ud);
        }, group_id),
        base::BindOnce([](DeleteContactGroupCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListSignatures(const std::string& account_id, ListSignaturesCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListSignatures(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListSignaturesCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void CreateSignature(const std::string& request_json, CreateSignatureCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailCreateSignature(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](CreateSignatureCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void UpdateSignature(const std::string& id, const std::string& request_json, UpdateSignatureCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string id, std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailUpdateSignature(id.c_str(), request_json.c_str(), cb, ud);
        }, id, request_json),
        base::BindOnce([](UpdateSignatureCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DeleteSignature(const std::string& id, DeleteSignatureCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string id, MahoMailReadCallback cb, void* ud) {
          return MahoMailDeleteSignature(id.c_str(), cb, ud);
        }, id),
        base::BindOnce([](DeleteSignatureCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListTemplates(ListTemplatesCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](MahoMailReadCallback cb, void* ud) {
          return MahoMailListTemplates(cb, ud);
        }),
        base::BindOnce([](ListTemplatesCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void CreateTemplate(const std::string& request_json, CreateTemplateCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailCreateTemplate(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](CreateTemplateCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void UpdateTemplate(const std::string& id, const std::string& request_json, UpdateTemplateCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string id, std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailUpdateTemplate(id.c_str(), request_json.c_str(), cb, ud);
        }, id, request_json),
        base::BindOnce([](UpdateTemplateCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DeleteTemplate(const std::string& id, DeleteTemplateCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string id, MahoMailReadCallback cb, void* ud) {
          return MahoMailDeleteTemplate(id.c_str(), cb, ud);
        }, id),
        base::BindOnce([](DeleteTemplateCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void GetEmailSummary(const std::string& account_id, const std::string& request_json, GetEmailSummaryCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailGetEmailSummary(account_id.c_str(), request_json.c_str(), cb, ud);
        }, account_id, request_json),
        base::BindOnce([](GetEmailSummaryCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void GetReplyDraft(const std::string& account_id, const std::string& request_json, GetReplyDraftCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailGetReplyDraft(account_id.c_str(), request_json.c_str(), cb, ud);
        }, account_id, request_json),
        base::BindOnce([](GetReplyDraftCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void AdjustTone(const std::string& request_json, AdjustToneCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailAdjustTone(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](AdjustToneCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ClassifyEmail(const std::string& account_id, const std::string& request_json, ClassifyEmailCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailClassifyEmail(account_id.c_str(), request_json.c_str(), cb, ud);
        }, account_id, request_json),
        base::BindOnce([](ClassifyEmailCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void NaturalLanguageSearch(const std::string& request_json, NaturalLanguageSearchCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailNaturalLanguageSearch(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](NaturalLanguageSearchCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void GetAiActionHistory(const std::string& account_id, int64_t limit, GetAiActionHistoryCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, int64_t limit, MahoMailReadCallback cb, void* ud) {
          return MahoMailGetAiActionHistory(account_id.c_str(), limit, cb, ud);
        }, account_id, limit),
        base::BindOnce([](GetAiActionHistoryCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SaveAiConfig(const std::string& config_json, SaveAiConfigCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string config_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailSaveAiConfig(config_json.c_str(), cb, ud);
        }, config_json),
        base::BindOnce([](SaveAiConfigCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void GetAiConfig(const std::string& feature, GetAiConfigCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string feature, MahoMailReadCallback cb, void* ud) {
          return MahoMailGetAiConfig(feature.c_str(), cb, ud);
        }, feature),
        base::BindOnce([](GetAiConfigCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DeleteAiConfig(const std::string& feature, DeleteAiConfigCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string feature, MahoMailReadCallback cb, void* ud) {
          return MahoMailDeleteAiConfig(feature.c_str(), cb, ud);
        }, feature),
        base::BindOnce([](DeleteAiConfigCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SetBrowserAiKeys(const std::string& keys_json, SetBrowserAiKeysCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string keys_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailSetBrowserAiKeys(keys_json.c_str(), cb, ud);
        }, keys_json),
        base::BindOnce([](SetBrowserAiKeysCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void TestAiConnection(const std::string& config_json, TestAiConnectionCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string config_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailTestAiConnection(config_json.c_str(), cb, ud);
        }, config_json),
        base::BindOnce([](TestAiConnectionCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void GetAutoDraftForEmail(const std::string& account_id, const std::string& email_id, GetAutoDraftForEmailCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string email_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailGetAutoDraftForEmail(account_id.c_str(), email_id.c_str(), cb, ud);
        }, account_id, email_id),
        base::BindOnce([](GetAutoDraftForEmailCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void UpdateAutoDraftStatus(const std::string& draft_id, const std::string& status, UpdateAutoDraftStatusCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string draft_id, std::string status, MahoMailReadCallback cb, void* ud) {
          return MahoMailUpdateAutoDraftStatus(draft_id.c_str(), status.c_str(), cb, ud);
        }, draft_id, status),
        base::BindOnce([](UpdateAutoDraftStatusCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void TranslateText(const std::string& text, const std::string& target_lang, const std::string& source_lang, TranslateTextCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string text, std::string target_lang, std::string source_lang, MahoMailReadCallback cb, void* ud) {
          return MahoMailTranslateText(text.c_str(), target_lang.c_str(), source_lang.c_str(), cb, ud);
        }, text, target_lang, source_lang),
        base::BindOnce([](TranslateTextCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void GeneratePgpKey(const std::string& request_json, GeneratePgpKeyCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailGeneratePgpKey(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](GeneratePgpKeyCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ImportPgpKey(const std::string& request_json, ImportPgpKeyCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailImportPgpKey(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](ImportPgpKeyCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ExportPgpKey(const std::string& key_id, bool include_private, ExportPgpKeyCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string key_id, bool include_private, MahoMailReadCallback cb, void* ud) {
          return MahoMailExportPgpKey(key_id.c_str(), include_private, cb, ud);
        }, key_id, include_private),
        base::BindOnce([](ExportPgpKeyCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListPgpKeys(const std::string& account_id, ListPgpKeysCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListPgpKeys(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListPgpKeysCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DeletePgpKey(const std::string& key_id, DeletePgpKeyCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string key_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailDeletePgpKey(key_id.c_str(), cb, ud);
        }, key_id),
        base::BindOnce([](DeletePgpKeyCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SetDefaultPgpKey(const std::string& account_id, const std::string& key_id, SetDefaultPgpKeyCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string key_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailSetDefaultPgpKey(account_id.c_str(), key_id.c_str(), cb, ud);
        }, account_id, key_id),
        base::BindOnce([](SetDefaultPgpKeyCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void EncryptEmailPgp(const std::string& request_json, EncryptEmailPgpCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailEncryptEmailPgp(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](EncryptEmailPgpCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void EncryptAttachmentPgp(const std::string& request_json, EncryptAttachmentPgpCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailEncryptAttachmentPgp(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](EncryptAttachmentPgpCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DecryptEmailPgp(const std::string& request_json, DecryptEmailPgpCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailDecryptEmailPgp(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](DecryptEmailPgpCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SignEmailPgp(const std::string& request_json, SignEmailPgpCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailSignEmailPgp(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](SignEmailPgpCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void VerifyEmailPgp(const std::string& request_json, VerifyEmailPgpCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailVerifyEmailPgp(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](VerifyEmailPgpCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ImportSmimeIdentity(const std::string& request_json, ImportSmimeIdentityCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailImportSmimeIdentity(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](ImportSmimeIdentityCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListSmimeIdentities(const std::string& account_id, ListSmimeIdentitiesCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListSmimeIdentities(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListSmimeIdentitiesCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DeleteSmimeIdentity(const std::string& id, DeleteSmimeIdentityCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string id, MahoMailReadCallback cb, void* ud) {
          return MahoMailDeleteSmimeIdentity(id.c_str(), cb, ud);
        }, id),
        base::BindOnce([](DeleteSmimeIdentityCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SetDefaultSmimeIdentity(const std::string& account_id, const std::string& identity_id, SetDefaultSmimeIdentityCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string identity_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailSetDefaultSmimeIdentity(account_id.c_str(), identity_id.c_str(), cb, ud);
        }, account_id, identity_id),
        base::BindOnce([](SetDefaultSmimeIdentityCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ExportSmimeCert(const std::string& id, ExportSmimeCertCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string id, MahoMailReadCallback cb, void* ud) {
          return MahoMailExportSmimeCert(id.c_str(), cb, ud);
        }, id),
        base::BindOnce([](ExportSmimeCertCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SignEmailSmime(const std::string& request_json, SignEmailSmimeCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailSignEmailSmime(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](SignEmailSmimeCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void EncryptEmailSmime(const std::string& request_json, EncryptEmailSmimeCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailEncryptEmailSmime(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](EncryptEmailSmimeCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DecryptEmailSmime(const std::string& request_json, DecryptEmailSmimeCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailDecryptEmailSmime(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](DecryptEmailSmimeCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void VerifyEmailSmime(const std::string& signed_body, VerifyEmailSmimeCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string signed_body, MahoMailReadCallback cb, void* ud) {
          return MahoMailVerifyEmailSmime(signed_body.c_str(), cb, ud);
        }, signed_body),
        base::BindOnce([](VerifyEmailSmimeCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void CleanupSmimeForAccount(const std::string& account_id, CleanupSmimeForAccountCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailCleanupSmimeForAccount(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](CleanupSmimeForAccountCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ImportCalendarEvent(const std::string& account_id, const std::string& email_id, const std::string& ics_data, ImportCalendarEventCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string email_id, std::string ics_data, MahoMailReadCallback cb, void* ud) {
          return MahoMailImportCalendarEvent(account_id.c_str(), email_id.c_str(), ics_data.c_str(), cb, ud);
        }, account_id, email_id, ics_data),
        base::BindOnce([](ImportCalendarEventCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListCalendarEvents(const std::string& account_id, const std::string& from_date, const std::string& to_date, ListCalendarEventsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string from_date, std::string to_date, MahoMailReadCallback cb, void* ud) {
          return MahoMailListCalendarEvents(account_id.c_str(), from_date.c_str(), to_date.c_str(), cb, ud);
        }, account_id, from_date, to_date),
        base::BindOnce([](ListCalendarEventsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void GetCalendarEvent(const std::string& event_id, GetCalendarEventCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string event_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailGetCalendarEvent(event_id.c_str(), cb, ud);
        }, event_id),
        base::BindOnce([](GetCalendarEventCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void UpdateRsvp(const std::string& event_id, const std::string& rsvp_status, UpdateRsvpCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string event_id, std::string rsvp_status, MahoMailReadCallback cb, void* ud) {
          return MahoMailUpdateRsvp(event_id.c_str(), rsvp_status.c_str(), cb, ud);
        }, event_id, rsvp_status),
        base::BindOnce([](UpdateRsvpCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void GenerateRsvpReply(const std::string& event_id, const std::string& rsvp_status, const std::string& account_email, GenerateRsvpReplyCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string event_id, std::string rsvp_status, std::string account_email, MahoMailReadCallback cb, void* ud) {
          return MahoMailGenerateRsvpReply(event_id.c_str(), rsvp_status.c_str(), account_email.c_str(), cb, ud);
        }, event_id, rsvp_status, account_email),
        base::BindOnce([](GenerateRsvpReplyCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DeleteCalendarEvent(const std::string& event_id, const std::string& delete_scope, DeleteCalendarEventCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string event_id, std::string delete_scope, MahoMailReadCallback cb, void* ud) {
          return MahoMailDeleteCalendarEvent(event_id.c_str(), delete_scope.c_str(), cb, ud);
        }, event_id, delete_scope),
        base::BindOnce([](DeleteCalendarEventCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void AutoImportCalendarEvents(const std::string& account_id, const std::string& email_id, const std::string& body_html, const std::string& body_text, AutoImportCalendarEventsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string email_id, std::string body_html, std::string body_text, MahoMailReadCallback cb, void* ud) {
          return MahoMailAutoImportCalendarEvents(account_id.c_str(), email_id.c_str(), body_html.c_str(), body_text.c_str(), cb, ud);
        }, account_id, email_id, body_html, body_text),
        base::BindOnce([](AutoImportCalendarEventsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void CreateCalendarEvent(const std::string& request_json, CreateCalendarEventCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailCreateCalendarEvent(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](CreateCalendarEventCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void UpdateCalendarEvent(const std::string& request_json, UpdateCalendarEventCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailUpdateCalendarEvent(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](UpdateCalendarEventCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SearchCalendarEvents(const std::string& account_id, const std::string& query, int64_t limit, SearchCalendarEventsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string query, int64_t limit, MahoMailReadCallback cb, void* ud) {
          return MahoMailSearchCalendarEvents(account_id.c_str(), query.c_str(), limit, cb, ud);
        }, account_id, query, limit),
        base::BindOnce([](SearchCalendarEventsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void CheckEventConflict(const std::string& account_id, const std::string& dtstart, const std::string& dtend, const std::string& exclude_event_id, CheckEventConflictCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string dtstart, std::string dtend, std::string exclude_event_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailCheckEventConflict(account_id.c_str(), dtstart.c_str(), dtend.c_str(), exclude_event_id.c_str(), cb, ud);
        }, account_id, dtstart, dtend, exclude_event_id),
        base::BindOnce([](CheckEventConflictCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DuplicateCalendarEvent(const std::string& event_id, DuplicateCalendarEventCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string event_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailDuplicateCalendarEvent(event_id.c_str(), cb, ud);
        }, event_id),
        base::BindOnce([](DuplicateCalendarEventCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void GoogleCalendarMoveEvent(const std::string& account_id, const std::string& calendar_id, const std::string& event_id, const std::string& destination_calendar_id, GoogleCalendarMoveEventCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string calendar_id, std::string event_id, std::string destination_calendar_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailGoogleCalendarMoveEvent(account_id.c_str(), calendar_id.c_str(), event_id.c_str(), destination_calendar_id.c_str(), cb, ud);
        }, account_id, calendar_id, event_id, destination_calendar_id),
        base::BindOnce([](GoogleCalendarMoveEventCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ExportCalendarIcs(const std::string& request_json, ExportCalendarIcsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailExportCalendarIcs(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](ExportCalendarIcsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListCalendarCategories(const std::string& account_id, ListCalendarCategoriesCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListCalendarCategories(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListCalendarCategoriesCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void CreateCalendarCategory(const std::string& account_id, const std::string& name, const std::string& color, CreateCalendarCategoryCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string name, std::string color, MahoMailReadCallback cb, void* ud) {
          return MahoMailCreateCalendarCategory(account_id.c_str(), name.c_str(), color.c_str(), cb, ud);
        }, account_id, name, color),
        base::BindOnce([](CreateCalendarCategoryCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void UpdateCalendarCategory(const std::string& id, const std::string& name, const std::string& color, UpdateCalendarCategoryCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string id, std::string name, std::string color, MahoMailReadCallback cb, void* ud) {
          return MahoMailUpdateCalendarCategory(id.c_str(), name.c_str(), color.c_str(), cb, ud);
        }, id, name, color),
        base::BindOnce([](UpdateCalendarCategoryCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void DeleteCalendarCategory(const std::string& id, DeleteCalendarCategoryCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string id, MahoMailReadCallback cb, void* ud) {
          return MahoMailDeleteCalendarCategory(id.c_str(), cb, ud);
        }, id),
        base::BindOnce([](DeleteCalendarCategoryCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SyncGoogleCalendar(const std::string& account_id, bool _full_sync, SyncGoogleCalendarCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, bool _full_sync, MahoMailReadCallback cb, void* ud) {
          return MahoMailSyncGoogleCalendar(account_id.c_str(), _full_sync, cb, ud);
        }, account_id, _full_sync),
        base::BindOnce([](SyncGoogleCalendarCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void ListAccountCalendars(const std::string& account_id, ListAccountCalendarsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailListAccountCalendars(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](ListAccountCalendarsCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SetCalendarVisibility(const std::string& calendar_row_id, bool visible, SetCalendarVisibilityCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string calendar_row_id, bool visible, MahoMailReadCallback cb, void* ud) {
          return MahoMailSetCalendarVisibility(calendar_row_id.c_str(), visible, cb, ud);
        }, calendar_row_id, visible),
        base::BindOnce([](SetCalendarVisibilityCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SubscribeHolidayCalendar(const std::string& account_id, const std::string& locale_code, SubscribeHolidayCalendarCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, std::string locale_code, MahoMailReadCallback cb, void* ud) {
          return MahoMailSubscribeHolidayCalendar(account_id.c_str(), locale_code.c_str(), cb, ud);
        }, account_id, locale_code),
        base::BindOnce([](SubscribeHolidayCalendarCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void GoogleCalendarFreeBusy(const std::string& request_json, GoogleCalendarFreeBusyCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailGoogleCalendarFreeBusy(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](GoogleCalendarFreeBusyCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void GetAppSetting(const std::string& key, GetAppSettingCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string key, MahoMailReadCallback cb, void* ud) {
          return MahoMailGetAppSetting(key.c_str(), cb, ud);
        }, key),
        base::BindOnce([](GetAppSettingCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void SetAppSetting(const std::string& key, const std::string& value, SetAppSettingCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string key, std::string value, MahoMailReadCallback cb, void* ud) {
          return MahoMailSetAppSetting(key.c_str(), value.c_str(), cb, ud);
        }, key, value),
        base::BindOnce([](SetAppSettingCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void UpdateBehaviorSetting(uint64_t expected_revision,
                             const std::string& key,
                             const std::string& value_json,
                             UpdateBehaviorSettingCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](uint64_t expected_revision, std::string key,
                          std::string value_json, MahoMailReadCallback cb,
                          void* ud) {
          return MahoMailUpdateBehaviorSetting(expected_revision, key.c_str(),
                                               value_json.c_str(), cb, ud);
        }, expected_revision, key, value_json),
        base::BindOnce([](UpdateBehaviorSettingCallback cb, bool ok,
                          std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void Md5Hash(const std::string& input, Md5HashCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string input, MahoMailReadCallback cb, void* ud) {
          return MahoMailMd5Hash(input.c_str(), cb, ud);
        }, input),
        base::BindOnce([](Md5HashCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }


  // === [W-C.Additional.Typed] ===
  void GetAccount(const std::string& account_id, GetAccountCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailGetAccount(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](GetAccountCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void UpdateAccount(const std::string& request_json, UpdateAccountCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string request_json, MahoMailReadCallback cb, void* ud) {
          return MahoMailUpdateAccount(request_json.c_str(), cb, ud);
        }, request_json),
        base::BindOnce([](UpdateAccountCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  // === [W-C.Additional.Refresh.Typed] ===
  void RefreshOAuthToken(const std::string& account_id, RefreshOAuthTokenCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce([](std::string account_id, MahoMailReadCallback cb, void* ud) {
          return MahoMailRefreshOAuthToken(account_id.c_str(), cb, ud);
        }, account_id),
        base::BindOnce([](RefreshOAuthTokenCallback cb, bool ok, std::string json) {
          std::move(cb).Run(ok, std::move(json));
        }, std::move(callback)));
  }

  void CallBackend(const std::string& command, const std::string& args_json, CallBackendCallback callback) override {
    std::optional<base::Value> parsed = base::JSONReader::Read(args_json, base::JSON_PARSE_RFC);
    const base::DictValue* dict = parsed ? parsed->GetIfDict() : nullptr;

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
          base::BindOnce([](std::string eid, std::string rem, MahoMailReadCallback cb, void* ud) {
            return MahoMailSetReminder(eid.c_str(), rem.c_str(), cb, ud);
          }, get_str("email_id"), get_str("reminder_at")),
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
          base::BindOnce([](std::string name, std::string query, std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailSaveSearch(name.c_str(), query.c_str(), acc.c_str(), cb, ud);
          }, get_str("name"), get_str("query"), get_str("account_id")),
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
    if (command == "StartAutoSync") {
      const int interval_minutes = get_int("interval_minutes", 15);
      const bool ok =
          interval_minutes > 0 &&
          MahoMailSetSyncIntervalMinutes(
              static_cast<uint64_t>(interval_minutes)) &&
          MahoMailStartAllAccountSync();
      std::move(callback).Run(
          ok, ok ? "{}" : "{\"error\":\"Failed to start sync\"}");
      return;
    }
    if (command == "StopAutoSync") {
      MahoMailStopSync();
      std::move(callback).Run(true, "{}");
      return;
    }
    if (command == "StartIdleMonitor") {
      // No dedicated IDLE FFI exists: the IMAP IDLE loop is bundled in the
      // per-account sync worker (mail-core sync.rs `start_sync_worker` spawns
      // the `idle_loop` task alongside the sync loop). MahoMailStartSync is
      // the real primitive that arms it; it validates account_id and reports
      // failure honestly.
      const std::string account_id = get_str("account_id");
      const bool ok =
          !account_id.empty() && MahoMailStartSync(account_id.c_str());
      std::move(callback).Run(
          ok, ok ? "{}" : "{\"error\":\"Failed to start idle monitor\"}");
      return;
    }
    if (command == "StopIdleMonitor") {
      // No per-account idle-stop FFI exists (only global MahoMailStopSync,
      // which would halt every account's sync+backfill -- wrong scope for a
      // per-account stop). Fail loudly instead of fake success.
      std::move(callback).Run(
          false,
          "{\"error\":\"StopIdleMonitor not supported: no per-account "
          "idle-stop backend primitive\"}");
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
          base::BindOnce([](std::string acc, std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailGetEmailSummary(acc.c_str(), req.c_str(), cb, ud);
          }, get_str("account_id"), args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "GetReplyDraft") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailGetReplyDraft(acc.c_str(), req.c_str(), cb, ud);
          }, get_str("account_id"), args_json),
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
          base::BindOnce([](std::string acc, std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailClassifyEmail(acc.c_str(), req.c_str(), cb, ud);
          }, get_str("account_id"), args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DelegateEmail") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailDelegateEmail(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SendMdnReceipt") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailSendMdnReceipt(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "GetForwardedAttachments") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailGetForwardedAttachments(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SnoozeCalendarEvent") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string eid, int minutes, MahoMailReadCallback cb, void* ud) {
            return MahoMailSnoozeCalendarEvent(eid.c_str(), minutes, cb, ud);
          }, get_str("event_id"), get_int("minutes", 0)),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ClassifyEmailsSmart") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, MahoMailReadCallback cb, void* ud) {
            return MahoMailClassifyEmailsSmart(acc.c_str(), cb, ud);
          }, get_str("account_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ListBySmartCategory") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string cat, int limit, int offset, MahoMailReadCallback cb, void* ud) {
            return MahoMailListBySmartCategory(acc.c_str(), cat.c_str(), limit, offset, cb, ud);
          }, get_str("account_id"), get_str("category"), get_int("limit", 50), get_int("offset", 0)),
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
    if (command == "GetAutoDraftForEmail") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string acc, std::string eid, MahoMailReadCallback cb, void* ud) {
            return MahoMailGetAutoDraftForEmail(acc.c_str(), eid.c_str(), cb, ud);
          }, get_str("account_id"), get_str("email_id")),
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
          base::BindOnce([](std::string text, std::string target, std::string source, MahoMailReadCallback cb, void* ud) {
            return MahoMailTranslateText(text.c_str(), target.c_str(), source.c_str(), cb, ud);
          }, get_str("text"), get_str("target_lang"), get_str("source_lang")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }

    // --- Wave H: Cryptography (PGP / S-MIME) ---
    if (command == "GeneratePgpKey") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailGeneratePgpKey(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ImportPgpKey") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailImportPgpKey(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ExportPgpKey") {
      std::move(callback).Run(false,
                              "generic PGP export is not permitted");
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
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailEncryptEmailPgp(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "EncryptAttachmentPgp") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailEncryptAttachmentPgp(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DecryptEmailPgp") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailDecryptEmailPgp(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "SignEmailPgp") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailSignEmailPgp(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "VerifyEmailPgp") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailVerifyEmailPgp(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ImportSmimeIdentity") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailImportSmimeIdentity(req.c_str(), cb, ud);
          }, args_json),
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
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailSignEmailSmime(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "EncryptEmailSmime") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailEncryptEmailSmime(req.c_str(), cb, ud);
          }, args_json),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "DecryptEmailSmime") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string req, MahoMailReadCallback cb, void* ud) {
            return MahoMailDecryptEmailSmime(req.c_str(), cb, ud);
          }, args_json),
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
          base::BindOnce([](std::string acc, bool full, MahoMailReadCallback cb, void* ud) {
            return MahoMailSyncGoogleCalendar(acc.c_str(), full, cb, ud);
          }, get_str("account_id"), get_bool("full_sync", false)),
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
    if (command == "ImportEmlContent") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string b64, std::string acc, std::string fid, MahoMailReadCallback cb, void* ud) {
            return MahoMailImportEmlContent(b64.c_str(), acc.c_str(), fid.c_str(), cb, ud);
          }, get_str("content_base64"), get_str("account_id"), get_str("folder_id")),
          base::BindOnce([](CallBackendCallback cb, bool ok, std::string json) {
            std::move(cb).Run(ok, std::move(json));
          }, std::move(callback)));
      return;
    }
    if (command == "ImportMboxContent") {
      MahoMailReadBridge::Start(
          base::BindOnce([](std::string b64, std::string acc, std::string fid, MahoMailReadCallback cb, void* ud) {
            return MahoMailImportMboxContent(b64.c_str(), acc.c_str(), fid.c_str(), cb, ud);
          }, get_str("content_base64"), get_str("account_id"), get_str("folder_id")),
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
    std::move(callback).Run(false, "{\"error\": \"Unknown command or not implemented\"}");
  }

  void BindClient(mojo::PendingRemote<mojom::MahoMailHelperClient> client) override {
    LOG(INFO) << "[MahoMailHelper] Binding client remote.";
    client_.reset();
    client_.Bind(std::move(client));
  }

 private:
  mojo::Receiver<mojom::MahoMailHelper> receiver_;
  base::OnceClosure quit_closure_;
  mojo::Remote<mojom::MahoMailHelperClient> client_;
};

}  // namespace maho

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
#if BUILDFLAG(IS_MAC)
  const std::string base_bundle_id =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          kBaseBundleIdSwitch);
  if (base_bundle_id.empty()) {
    LOG(ERROR) << "[MahoMailHelper] Missing browser base bundle ID switch.";
    return 1;
  }
  base::apple::SetBaseBundleIDOverride(base_bundle_id);
#endif

  LOG(INFO) << "[MahoMailHelper] Starting helper process...";

  mojo::PlatformChannelEndpoint endpoint;
#if BUILDFLAG(IS_LINUX)
  endpoint = mojo::PlatformChannel::RecoverPassedEndpointFromCommandLine(
      *base::CommandLine::ForCurrentProcess());
  if (!endpoint.is_valid() || !endpoint.platform_handle().is_fd()) {
    LOG(ERROR) << "[MahoMailHelper] Invalid PlatformChannel endpoint.";
    return 1;
  }
  base::ScopedFD oauth_loopback_listener = CreateOAuthLoopbackListener();
  if (!oauth_loopback_listener.is_valid()) {
    return 1;
  }
  const std::string oauth_loopback_fd =
      base::NumberToString(oauth_loopback_listener.get());
  if (setenv(kOAuthLoopbackFdEnv, oauth_loopback_fd.c_str(), 1) != 0) {
    PLOG(ERROR) << "[MahoMailHelper] Failed to publish OAuth loopback fd";
    return 1;
  }
  const int mojo_fd = endpoint.platform_handle().GetFD().get();
  if (!maho::mail_helper::CloseLinuxMailHelperInheritedFds(
          mojo_fd, oauth_loopback_listener.get())) {
    LOG(ERROR) << "[MahoMailHelper] Failed to close inherited descriptors.";
    return 1;
  }
  const base::FilePath mail_root =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          kMailRootSwitch);
  if (!maho::mail_helper::InitializeLinuxMailHelperSandbox(
          mail_root, oauth_loopback_listener.get())) {
    LOG(ERROR) << "[MahoMailHelper] Linux sandbox initialization failed; "
                  "refusing to start.";
    return 1;
  }
#else
  // Starting Crashpad launches a handler process, so Linux must not do this
  // before seccomp (which would violate startup ordering) or after seccomp
  // (where process creation is deliberately denied).
  InstallCrashpadHandler(*base::CommandLine::ForCurrentProcess());
  endpoint = mojo::PlatformChannel::RecoverPassedEndpointFromCommandLine(
      *base::CommandLine::ForCurrentProcess());
#endif

  // Initialize Mojo
  mojo::core::Init();

  base::Thread io_thread("maho_mail_helper_io");
  io_thread.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0));
  mojo::core::ScopedIPCSupport ipc_support(
      io_thread.task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  base::SingleThreadTaskExecutor main_task_executor;
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("maho_mail_helper_thread_pool");

  base::RunLoop run_loop;

  std::unique_ptr<maho::MahoMailHelperImpl> helper_impl;

  if (endpoint.is_valid()) {
    mojo::IncomingInvitation invitation = mojo::IncomingInvitation::Accept(std::move(endpoint));
    mojo::ScopedMessagePipeHandle pipe = invitation.ExtractMessagePipe("maho_mail_pipe");

    helper_impl = std::make_unique<maho::MahoMailHelperImpl>(
        mojo::PendingReceiver<maho::mojom::MahoMailHelper>(std::move(pipe)),
        run_loop.QuitClosure());
    LOG(INFO) << "[MahoMailHelper] Mojo communication established.";
  } else {
    LOG(ERROR) << "[MahoMailHelper] Invalid PlatformChannel endpoint.";
    return 1;
  }

  run_loop.Run();
  return 0;
}
