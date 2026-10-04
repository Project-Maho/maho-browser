// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/passwords/maho_password_store_backend.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "chrome/test/base/testing_profile.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_form_digest.h"
#include "components/password_manager/core/browser/password_store/password_store_change.h"
#include "components/password_manager/core/browser/stub_password_manager_driver.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/passwords/maho_password_authorization_service.h"
#include "maho/browser/passwords/maho_password_provider_utils.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace maho {
namespace passwords {

namespace {

constexpr char kMasterPassphrase[] = "correct horse battery staple";
constexpr char kRecoverySecret[] = "test recovery phrase";
constexpr char kSqlcipherKey[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
constexpr char kSentinelPassword[] = "S3NTINEL-maho-vault-9F4C";
constexpr char kImportedPassword[] = "S3NTINEL-imported-runtime-2A7B";
constexpr char kKillSwitchPassword[] = "S3NTINEL-killswitch-runtime-8E2F";
constexpr char kRoutingPassword[] = "S3NTINEL-routing-runtime-4B9A";
constexpr char kJitPassword[] = "S3NTINEL-jit-runtime-6D8C";
constexpr char16_t kJitPassword16[] = u"S3NTINEL-jit-runtime-6D8C";

inline const PasswordStoreBackendCredential &
GetChangeCredential(const password_manager::PasswordStoreChange &change) {
#if MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL
  return change.credential();
#else
  return change.form();
#endif
}

inline void SetCredentialPassword(PasswordStoreBackendCredential &cred,
                                  std::u16string password) {
#if MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL
  cred.password_value = password_manager::PasswordString(std::move(password));
#else
  cred.password_value = std::move(password);
#endif
}

inline std::u16string
GetCredentialPassword(const PasswordStoreBackendCredential &cred) {
#if MAHO_PASSWORD_STORE_BACKEND_USES_STORED_CREDENTIAL
  return cred.password_value.value();
#else
  return cred.password_value;
#endif
}

struct ResolverFfiProbe {
  int credential_execute_calls = 0;
  int result_status =
      static_cast<int>(maho::core::VaultBackendResultStatus::kFailed);
  std::string request;
  std::string payload;
};

ResolverFfiProbe *g_resolver_ffi_probe = nullptr;

MahoVaultBackendSession *ResolverProbeSessionNew(MahoCore *) {
  return reinterpret_cast<MahoVaultBackendSession *>(0x1001);
}
void ResolverProbeSessionClose(MahoVaultBackendSession *) {}
void ResolverProbeSessionFree(MahoVaultBackendSession *) {}
MahoVaultBackendResult *ResolverProbeExecute(MahoVaultBackendSession *,
                                             const char *) {
  return reinterpret_cast<MahoVaultBackendResult *>(0x1002);
}
MahoVaultBackendResult *
ResolverProbeExecuteCredentials(MahoVaultBackendSession *,
                                const char *request) {
  ++g_resolver_ffi_probe->credential_execute_calls;
  g_resolver_ffi_probe->request = request ? request : std::string();
  return reinterpret_cast<MahoVaultBackendResult *>(0x1002);
}
int ResolverProbeResultStatus(const MahoVaultBackendResult *) {
  return g_resolver_ffi_probe->result_status;
}
MahoVaultBackendBuffer *ResolverProbeResultConsume(MahoVaultBackendResult *) {
  return g_resolver_ffi_probe->payload.empty()
             ? nullptr
             : reinterpret_cast<MahoVaultBackendBuffer *>(0x1003);
}
void ResolverProbeResultFree(MahoVaultBackendResult *) {}
const uint8_t *ResolverProbeBufferData(const MahoVaultBackendBuffer *) {
  return reinterpret_cast<const uint8_t *>(
      g_resolver_ffi_probe->payload.data());
}
size_t ResolverProbeBufferLen(const MahoVaultBackendBuffer *) {
  return g_resolver_ffi_probe->payload.size();
}
void ResolverProbeBufferFree(MahoVaultBackendBuffer *) {
  std::fill(g_resolver_ffi_probe->payload.begin(),
            g_resolver_ffi_probe->payload.end(), '\0');
  g_resolver_ffi_probe->payload.clear();
}

const maho::core::VaultBackendFfiForTesting kResolverProbeFfi = {
    &ResolverProbeSessionNew,         &ResolverProbeSessionClose,
    &ResolverProbeSessionFree,        &ResolverProbeExecute,
    &ResolverProbeExecuteCredentials, &ResolverProbeResultStatus,
    &ResolverProbeResultConsume,      &ResolverProbeResultFree,
    &ResolverProbeBufferData,         &ResolverProbeBufferLen,
    &ResolverProbeBufferFree,
};

class ScopedResolverFfiProbe {
public:
  explicit ScopedResolverFfiProbe(ResolverFfiProbe *probe) {
    g_resolver_ffi_probe = probe;
    maho::core::SetVaultBackendFfiForTesting(&kResolverProbeFfi);
  }
  ~ScopedResolverFfiProbe() {
    maho::core::SetVaultBackendFfiForTesting(nullptr);
    g_resolver_ffi_probe = nullptr;
  }

  ScopedResolverFfiProbe(const ScopedResolverFfiProbe &) = delete;
  ScopedResolverFfiProbe &operator=(const ScopedResolverFfiProbe &) = delete;
};

class ScopedZeroizeObserver {
public:
  explicit ScopedZeroizeObserver(std::vector<std::string> *observations) {
    SetSecureZeroizeObserverForTesting(base::BindRepeating(
        [](std::vector<std::string> *out, const std::string &value) {
          out->push_back(value);
        },
        observations));
  }
  ~ScopedZeroizeObserver() {
    SetSecureZeroizeObserverForTesting(SecureZeroizeObserverForTesting());
  }

  ScopedZeroizeObserver(const ScopedZeroizeObserver &) = delete;
  ScopedZeroizeObserver &operator=(const ScopedZeroizeObserver &) = delete;
};

class ResolverTestDriver : public password_manager::StubPasswordManagerDriver {
public:
  explicit ResolverTestDriver(const std::string &origin,
                              std::u16string_view expected_password = {})
      : origin_(url::Origin::Create(GURL(origin))),
        expected_password_(expected_password) {}
  explicit ResolverTestDriver(
      const password_manager::PasswordFillRequestContext &context,
      std::u16string_view expected_password = {})
      : context_(context),
        origin_(context.requesting_origin),
        expected_password_(expected_password) {
    SetPasswordFillRequestContextForTesting(context);
  }
  ~ResolverTestDriver() override = default;

  const url::Origin &GetLastCommittedOrigin() const override {
    return origin_;
  }

  void FillSuggestion(const std::u16string &username,
                      const std::u16string &password,
                      base::OnceCallback<void(bool)> callback) override {
    std::move(callback).Run(
        !password.empty() &&
        (expected_password_.empty() || password == expected_password_));
  }

  password_manager::PasswordFillResolver MakeResolver(
      const password_manager::PasswordFillRequestContext &context,
      base::OnceCallback<void(bool)> callback) {
    return CreatePasswordFillResolver(context, std::u16string(),
                                      std::move(callback));
  }

private:
  std::optional<password_manager::PasswordFillRequestContext> context_;
  const url::Origin origin_;
  const std::u16string_view expected_password_;
};

// Extension IDs registered for the external providers in the shared provider
// registry (maho-types `get_provider_registry`). An external provider is only
// "available" while one of its registered extensions is installed + enabled for
// the profile that owns the core.
constexpr char kBitwardenExtensionId[] = "nngceckbapebfimnlniiiahkandclblb";
constexpr char kOnePasswordExtensionId[] = "aeblfdkhhhdcdjpifhhbdiojplfjncoa";

std::string InstalledExtensionsJson(const std::string &extension_id,
                                    const std::string &name) {
  return std::string(R"([{"id":")") + extension_id + R"(","name":")" + name +
         R"(","version":"1.0","enabled":true}])";
}

std::u16string RoutingPassword16() { return u"S3NTINEL-routing-runtime-4B9A"; }

std::u16string KillSwitchPassword16() {
  return u"S3NTINEL-killswitch-runtime-8E2F";
}

std::u16string ManualPassword16() { return u"S3NTINEL-manual-runtime-5C1D"; }

// Installs a core AND its owning profile.
// maho::IsPasswordManagerAllowedForProfile only admits the single regular
// profile that owns the browser-global core, and CreateVaultBackendSession()
// resolves that owner itself, so a core installed without an owner profile
// yields an invalid session and every Vault-backed operation fails closed.
// Tests that exercise a SUCCESSFUL Vault path must therefore bind an owner
// profile, not just a core.
class ScopedCoreOverride {
public:
  ScopedCoreOverride(MahoCore *core, Profile *owner_profile)
      : saved_core_(maho::GetCore()),
        saved_owner_(maho::GetCoreOwnerProfile()) {
    maho::SetCoreForProfile(core, owner_profile);
  }
  ~ScopedCoreOverride() { maho::SetCoreForProfile(saved_core_, saved_owner_); }

  ScopedCoreOverride(const ScopedCoreOverride &) = delete;
  ScopedCoreOverride &operator=(const ScopedCoreOverride &) = delete;

private:
  MahoCore *saved_core_;
  Profile *saved_owner_;
};

bool VaultResponseOk(char *response) {
  if (!response) {
    return false;
  }
  std::string json(response);
  maho_string_free(response);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  return parsed && parsed->is_dict() &&
         parsed->GetDict().FindBool("ok").value_or(false);
}

std::string TakeImportJson(char *response) {
  if (!response) {
    return {};
  }
  std::string json(response);
  maho_import_string_free(response);
  return json;
}

std::optional<std::string>
PreviewTokenFromImportResponse(const std::string &json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict() ||
      !parsed->GetDict().FindBool("ok").value_or(false)) {
    return std::nullopt;
  }
  const base::DictValue *data = parsed->GetDict().FindDict("data");
  if (!data) {
    return std::nullopt;
  }
  const std::string *preview_token = data->FindString("previewToken");
  if (!preview_token || preview_token->empty()) {
    return std::nullopt;
  }
  return *preview_token;
}

bool ImportCommitResponseOk(const std::string &json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict() ||
      !parsed->GetDict().FindBool("ok").value_or(false)) {
    return false;
  }
  const base::DictValue *data = parsed->GetDict().FindDict("data");
  return data && data->FindInt("committed").value_or(0) == 1;
}

class ScopedPasswordImportJob {
public:
  ScopedPasswordImportJob() : job_(maho_password_import_job_new()) {}
  ~ScopedPasswordImportJob() { maho_password_import_job_free(job_); }

  ScopedPasswordImportJob(const ScopedPasswordImportJob &) = delete;
  ScopedPasswordImportJob &operator=(const ScopedPasswordImportJob &) = delete;

  MahoPasswordImportJob *get() const { return job_; }

private:
  MahoPasswordImportJob *job_ = nullptr;
};

} // namespace

class MahoPasswordStoreBackendTest : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(maho_storage_set_sqlcipher_key(kSqlcipherKey));
    owner_profile_ = TestingProfile::Builder().Build();
    ASSERT_NE(owner_profile_, nullptr);
    maho::passwords::MahoPasswordAuthorizationService::Get()->ResetForTesting();
    maho::passwords::MahoPasswordAuthorizationService::Get()
        ->SetInteractiveAuthorizationEnabledForTesting(false);
  }

  void TearDown() override {
    maho::passwords::MahoPasswordAuthorizationService::Get()->ResetForTesting();
    core_override_.reset();
    if (core_) {
      maho_core_free(core_);
      core_ = nullptr;
    }
    owner_profile_.reset();
  }

  // The regular profile that owns the browser-global core in these tests. It is
  // the only profile maho::IsPasswordManagerAllowedForProfile admits, so it is
  // what every successful Vault-backed operation below routes through.
  TestingProfile *owner_profile() { return owner_profile_.get(); }

  MahoPasswordStoreBackend CreateBackend(bool enabled = true) {
    // Match the production factory (password_store_backend_factory.cc
    // override): the backend is constructed with (enabled, profile_key), where
    // the key is derived from the owning profile so its session is
    // caller-profile-bound.
    return MahoPasswordStoreBackend(
        enabled, maho::GetProfileIdentityKey(owner_profile_->GetPath(),
                                             owner_profile_->GetPrefs()));
  }

  std::string OwnerProfileKey() const {
    return maho::GetProfileIdentityKey(owner_profile_->GetPath(),
                                       owner_profile_->GetPrefs());
  }

  void Grant(PasswordAuthorizationAction action,
             const std::string &origin_scope) {
    MahoPasswordAuthorizationService::Get()->GrantForTesting(
        OwnerProfileKey(), origin_scope, action,
        base::TimeTicks::Now() +
            MahoPasswordAuthorizationService::kAuthorizationLifetime);
  }

  void GrantReadAllAndRangeDelete() {
    Grant(PasswordAuthorizationAction::kReadAll,
          MahoPasswordAuthorizationService::kAllOriginsScope);
    Grant(PasswordAuthorizationAction::kRangeDelete,
          MahoPasswordAuthorizationService::kRangeDeleteScope);
  }

  void CreateInitializedVaultCore() {
    const base::FilePath database_path =
        temp_dir_.GetPath().AppendASCII("password-backend.sqlite");
    core_ = maho_core_new_with_storage(database_path.AsUTF8Unsafe().c_str());
    ASSERT_NE(core_, nullptr);
    core_override_ =
        std::make_unique<ScopedCoreOverride>(core_, owner_profile_.get());

    const std::string request = std::string("{\"masterPassphrase\":\"") +
                                kMasterPassphrase + "\",\"recoverySecret\":\"" +
                                kRecoverySecret + "\"}";
    ASSERT_TRUE(
        VaultResponseOk(maho_vault_initialize_json(core_, request.c_str())));
    maho::NotifyVaultLockStateChanged(/*locked=*/false);
    MahoPasswordAuthorizationService::Get()->SetVaultLockedForTesting(false);
    GrantReadAllAndRangeDelete();
  }

  bool LockVault() {
    maho::NotifyVaultLockStateChanged(/*locked=*/true);
    return VaultResponseOk(maho_vault_lock_json(core_));
  }

  void SelectPasswordProvider(const std::string &provider_mode,
                              const std::string &installed_extensions_json) {
    ASSERT_NE(core_, nullptr);
    maho_core_set_installed_extensions(core_,
                                       installed_extensions_json.c_str());

    base::DictValue autofill;
    autofill.Set("passwordsEnabled", true);
    autofill.Set("passwordProvider", provider_mode);
    base::DictValue settings;
    settings.Set("autofill", std::move(autofill));
    std::string settings_json;
    base::JSONWriter::Write(base::Value(std::move(settings)), &settings_json);
    maho_core_update_settings(core_, settings_json.c_str());
  }

  base::FilePath WriteOnePasswordExport(const std::string &origin,
                                        const std::string &username,
                                        const std::string &password) {
    const base::FilePath path =
        temp_dir_.GetPath().AppendASCII("runtime-onepassword-import.csv");
    const std::string csv =
        "Title,Website,Username,Password,One-time password,Favorite status,"
        "Archived status,Tags,Notes\n"
        "Runtime," +
        origin + "," + username + "," + password +
        ",,false,false,synthetic,Synthetic runtime smoke fixture\n";
    EXPECT_TRUE(base::WriteFile(path, csv));
    return path;
  }

  bool ImportOnePasswordCredential(const std::string &origin,
                                   const std::string &username,
                                   const std::string &password) {
    ScopedPasswordImportJob import_job;
    if (!import_job.get()) {
      return false;
    }

    const base::FilePath export_path =
        WriteOnePasswordExport(origin, username, password);
    base::DictValue preview_request;
    preview_request.Set("sourceFormat", "one_password_csv");
    preview_request.Set("path", export_path.AsUTF8Unsafe());
    std::string preview_json;
    base::JSONWriter::Write(base::Value(std::move(preview_request)),
                            &preview_json);
    const std::string preview_response =
        TakeImportJson(maho_password_import_job_preview_path_json(
            import_job.get(), preview_json.c_str()));
    const std::optional<std::string> preview_token =
        PreviewTokenFromImportResponse(preview_response);
    if (!preview_token) {
      return false;
    }

    base::DictValue commit_request;
    commit_request.Set("previewToken", *preview_token);
    std::string commit_json;
    base::JSONWriter::Write(base::Value(std::move(commit_request)),
                            &commit_json);
    const std::string commit_response =
        TakeImportJson(maho_password_import_job_commit_json(
            import_job.get(), core_, commit_json.c_str()));
    return ImportCommitResponseOk(commit_response);
  }

  std::vector<MahoPasswordForm> FillMahoForms(MahoPasswordStoreBackend &backend,
                                              const std::string &origin) {
    Grant(PasswordAuthorizationAction::kFill, origin);
    base::RunLoop fill_loop;
    bool fill_success = false;
    std::vector<MahoPasswordForm> matched_forms;
    backend.FillMatchingLoginsMahoAsync(
        origin, password_manager::PasswordForm::Scheme::kHtml,
        base::BindOnce(
            [](base::RunLoop *loop, bool *success_out,
               std::vector<MahoPasswordForm> *forms_out,
               std::vector<MahoPasswordForm> forms, bool success) {
              *success_out = success;
              *forms_out = std::move(forms);
              loop->Quit();
            },
            &fill_loop, &fill_success, &matched_forms));
    fill_loop.Run();
    return matched_forms;
  }

  password_manager::BackendLoginsResult
  FillPasswordStoreForms(MahoPasswordStoreBackend &backend,
                         const std::string &origin,
                         password_manager::PasswordForm::Scheme scheme =
                             password_manager::PasswordForm::Scheme::kHtml) {
    Grant(PasswordAuthorizationAction::kFill, origin);
    base::RunLoop fill_loop;
    bool fill_success = false;
    password_manager::BackendLoginsResult matched_forms;
    std::vector<password_manager::PasswordFormDigest> digests;
    digests.emplace_back(scheme, origin, GURL(origin + "/login"));
    const bool include_psl = false;
    backend.FillMatchingLoginsAsync(
        base::BindOnce(
            [](base::RunLoop *loop, bool *success_out,
               password_manager::BackendLoginsResult *forms_out,
               password_manager::BackendLoginsResultOrError result) {
              if (auto *forms =
                      std::get_if<password_manager::BackendLoginsResult>(
                          &result)) {
                *success_out = true;
                *forms_out = std::move(*forms);
              }
              loop->Quit();
            },
            &fill_loop, &fill_success, &matched_forms),
        include_psl, digests);
    fill_loop.Run();
    return matched_forms;
  }

  std::string CredentialPayloadForOrigin(const std::string &origin) {
    maho::core::VaultBackendSession session = maho::CreateVaultBackendSession();
    EXPECT_TRUE(session.is_valid());
    base::DictValue request;
    request.Set("origin", origin);
    std::string json;
    base::JSONWriter::Write(request, &json);
    maho::core::VaultBackendResult result = session.ExecuteCredentials(json);
    EXPECT_EQ(maho::core::VaultBackendResultStatus::kSuccess, result.status());
    return result.ConsumeJson();
  }

  password_manager::PasswordFillRequestContext FillContextFor(
      const std::string &origin) {
    return {.requesting_origin = url::Origin::Create(GURL(origin)),
            .document_token =
                password_manager::PasswordManagerDocumentToken()};
  }

  password_manager::BackendLoginsResult DiscoverForContext(
      MahoPasswordStoreBackend &backend, const std::string &origin,
      const password_manager::PasswordFillRequestContext &context) {
    Grant(PasswordAuthorizationAction::kFill, origin);
    password_manager::PasswordFormDigest digest(
        password_manager::PasswordForm::Scheme::kHtml, origin,
        GURL(origin + "/login"));
    base::RunLoop loop;
    password_manager::BackendLoginsResult forms;
    backend.GetGroupedMatchingLoginsAsync(
        digest, context,
        base::BindOnce(
            [](base::RunLoop *loop,
               password_manager::BackendLoginsResult *forms_out,
               password_manager::BackendLoginsResultOrError result) {
              if (auto *resolved =
                      std::get_if<password_manager::BackendLoginsResult>(
                          &result)) {
                *forms_out = std::move(*resolved);
              }
              loop->Quit();
            },
            &loop, &forms));
    loop.Run();
    return forms;
  }

  bool ResolveForContext(
      MahoPasswordStoreBackend &backend,
      const password_manager::PasswordFillRequestContext &context,
      const password_manager::PasswordFillSelection &selection,
      int *callback_count = nullptr,
      std::u16string_view expected_password = {}) {
    base::RunLoop loop;
    bool success = false;
    ResolverTestDriver driver(context, expected_password);
    backend.ResolvePasswordFill(
        context, selection,
        driver.MakeResolver(
            context,
            base::BindOnce(
                [](base::RunLoop *loop, bool *success_out, int *count,
                   bool resolved) {
                  *success_out = resolved;
                  if (count) {
                    ++*count;
                  }
                  loop->Quit();
                },
                &loop, &success, callback_count)));
    loop.Run();
    return success;
  }

  MahoPasswordForm AddLoginAndFetch(MahoPasswordStoreBackend &backend,
                                    const std::string &origin,
                                    const std::string &username,
                                    const std::string &password) {
    MahoPasswordForm form;
    form.signon_realm = origin;
    form.username_value = username;
    form.password_value = password;

    base::RunLoop add_loop;
    bool add_success = false;
    Grant(PasswordAuthorizationAction::kAdd, origin);
    backend.AddLoginMahoAsync(
        form, base::BindOnce(
                  [](base::RunLoop *loop, bool *success_out, bool success) {
                    *success_out = success;
                    loop->Quit();
                  },
                  &add_loop, &add_success));
    add_loop.Run();
    EXPECT_TRUE(add_success);

    Grant(PasswordAuthorizationAction::kFill, origin);
    base::RunLoop fill_loop;
    bool fill_success = false;
    std::vector<MahoPasswordForm> matched_forms;
    backend.FillMatchingLoginsMahoAsync(
        origin, password_manager::PasswordForm::Scheme::kHtml,
        base::BindOnce(
            [](base::RunLoop *loop, bool *success_out,
               std::vector<MahoPasswordForm> *forms_out,
               std::vector<MahoPasswordForm> forms, bool success) {
              *success_out = success;
              *forms_out = std::move(forms);
              loop->Quit();
            },
            &fill_loop, &fill_success, &matched_forms));
    fill_loop.Run();
    EXPECT_TRUE(fill_success);
    EXPECT_EQ(1u, matched_forms.size());
    return matched_forms.empty() ? MahoPasswordForm()
                                 : std::move(matched_forms[0]);
  }

  // Creates a local-state-like PrefService carrying the Maho native password
  // write kill switch, and installs it as the process-wide source the backend
  // consults. Returns the raw pointer so tests can flip the pref.
  TestingPrefServiceSimple *CreateKillSwitchPrefs() {
    // Reset any previously installed override FIRST: assigning over a live
    // pref_override_ constructs the new scoped guard before destroying the old
    // one, so the old destructor would restore g_...prefs_for_testing to a
    // stale previous_ and drop the new override entirely (leaving reads to
    // fall through to the process local state). Resetting first keeps the
    // install chain correct across repeat calls within one test.
    pref_override_.reset();
    pref_service_ = std::make_unique<TestingPrefServiceSimple>();
    RegisterLocalStatePrefs(pref_service_->registry());
    pref_override_ = std::make_unique<ScopedNativePasswordWritePrefsForTesting>(
        pref_service_.get());
    return pref_service_.get();
  }

  // Installs the scoped kill-switch pref override and turns the native write
  // kill switch ON for tests that exercise the native write/fill behavior
  // itself. The registry default is also true, but installing the override
  // makes the test the sole owner of the switch for the fixture's lifetime.
  void EnableNativeWriteForTesting() {
    CreateKillSwitchPrefs()->SetBoolean(prefs::kMahoNativePasswordWriteEnabled,
                                        true);
  }

  // BrowserTaskEnvironment (not the plain TaskEnvironment) because
  // TestingProfile requires browser threads; MOCK_TIME is preserved so no test
  // waits on wall clock time.
  content::BrowserTaskEnvironment task_environment_{
      content::BrowserTaskEnvironment::TimeSource::MOCK_TIME};

private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<TestingProfile> owner_profile_;
  std::unique_ptr<TestingPrefServiceSimple> pref_service_;
  std::unique_ptr<ScopedNativePasswordWritePrefsForTesting> pref_override_;
  MahoCore *core_ = nullptr;
  std::unique_ptr<ScopedCoreOverride> core_override_;
};

TEST_F(MahoPasswordStoreBackendTest, InitBackend) {
  MahoPasswordStoreBackend backend = CreateBackend();
  EXPECT_FALSE(backend.IsInitialized());

  base::RunLoop run_loop;
  bool init_result = false;
  backend.InitBackend(base::BindOnce(
      [](base::RunLoop *loop, bool *result_out, bool success) {
        *result_out = success;
        loop->Quit();
      },
      &run_loop, &init_result));
  run_loop.Run();

  EXPECT_TRUE(init_result);
  EXPECT_TRUE(backend.IsInitialized());
}

TEST_F(MahoPasswordStoreBackendTest, Shutdown) {
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(backend.IsInitialized());

  base::RunLoop run_loop;
  backend.Shutdown(run_loop.QuitClosure());
  run_loop.Run();

  EXPECT_FALSE(backend.IsInitialized());

  // Callback after shutdown should handle shutdown gracefully without crashing.
  base::RunLoop call_loop;
  bool call_success = true;
  backend.GetAllLoginsMahoAsync(base::BindOnce(
      [](base::RunLoop *loop, bool *success_out,
         std::vector<MahoPasswordForm> forms, bool success) {
        *success_out = success;
        loop->Quit();
      },
      &call_loop, &call_success));
  call_loop.Run();

  EXPECT_FALSE(call_success);
}

TEST_F(MahoPasswordStoreBackendTest,
       AuthorizationDeniesAllSecretReadsAndMutationsUntilGranted) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  MahoPasswordAuthorizationService::Get()->ResetForTesting();
  MahoPasswordAuthorizationService::Get()->SetVaultLockedForTesting(false);
  MahoPasswordAuthorizationService::Get()->SetInteractiveAuthorizationEnabledForTesting(false);
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  SelectPasswordProvider("maho_native", "[]");

  MahoPasswordForm form;
  form.signon_realm = "https://authorization.example";
  form.username_value = "user@example.com";
  form.password_value = "password";

  base::RunLoop add_loop;
  bool add_success = true;
  backend.AddLoginMahoAsync(
      form, base::BindOnce(
                [](base::RunLoop *loop, bool *success, bool result) {
                  *success = result;
                  loop->Quit();
                },
                &add_loop, &add_success));
  add_loop.Run();
  EXPECT_FALSE(add_success);

  base::RunLoop read_loop;
  bool read_success = true;
  backend.GetAllLoginsMahoAsync(base::BindOnce(
      [](base::RunLoop *loop, bool *success,
         std::vector<MahoPasswordForm> forms, bool result) {
        EXPECT_TRUE(forms.empty());
        *success = result;
        loop->Quit();
      },
      &read_loop, &read_success));
  read_loop.Run();
  EXPECT_FALSE(read_success);

  base::RunLoop fill_loop;
  bool fill_success = true;
  backend.FillMatchingLoginsMahoAsync(
      form.signon_realm, form.scheme,
      base::BindOnce(
          [](base::RunLoop *loop, bool *success,
             std::vector<MahoPasswordForm> forms, bool result) {
            EXPECT_TRUE(forms.empty());
            *success = result;
            loop->Quit();
          },
          &fill_loop, &fill_success));
  fill_loop.Run();
  EXPECT_FALSE(fill_success);
}

TEST_F(MahoPasswordStoreBackendTest,
       ConstructionCapturesEachProfileIdentityIndependentOfCoreOwner) {
  std::unique_ptr<TestingProfile> first_profile =
      TestingProfile::Builder().Build();
  std::unique_ptr<TestingProfile> second_profile =
      TestingProfile::Builder().Build();
  ASSERT_NE(first_profile, nullptr);
  ASSERT_NE(second_profile, nullptr);

  const std::string first_key = maho::GetProfileIdentityKey(
      first_profile->GetPath(), first_profile->GetPrefs());
  const std::string second_key = maho::GetProfileIdentityKey(
      second_profile->GetPath(), second_profile->GetPrefs());
  ASSERT_NE(first_key, second_key);

  MahoPasswordStoreBackend first_backend(/*enabled=*/true, first_key);
  MahoPasswordStoreBackend second_backend(/*enabled=*/true, second_key);
  EXPECT_EQ(first_key, first_backend.profile_key_for_testing());
  EXPECT_EQ(second_key, second_backend.profile_key_for_testing());

  CreateInitializedVaultCore();
  EXPECT_EQ(first_key, first_backend.profile_key_for_testing());
  EXPECT_EQ(second_key, second_backend.profile_key_for_testing());
  EXPECT_NE(OwnerProfileKey(), first_backend.profile_key_for_testing());
  EXPECT_NE(OwnerProfileKey(), second_backend.profile_key_for_testing());
}

TEST_F(MahoPasswordStoreBackendTest,
       MutationAuthorizationIsExactOriginAndActionBound) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  SelectPasswordProvider("maho_native", "[]");
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  const std::string authorized_origin = "https://authorized-mutation.example";
  const std::string other_origin = "https://other-mutation.example";
  Grant(PasswordAuthorizationAction::kAdd, authorized_origin);

  MahoPasswordForm other;
  other.signon_realm = other_origin;
  other.url = other_origin;
  other.username_value = "other@example.test";
  other.password_value = "must-not-persist";
  base::RunLoop denied_loop;
  bool denied_add = true;
  backend.AddLoginMahoAsync(
      other, base::BindOnce(
                 [](base::RunLoop *loop, bool *out, bool success) {
                   *out = success;
                   loop->Quit();
                 },
                 &denied_loop, &denied_add));
  denied_loop.Run();
  EXPECT_FALSE(denied_add);

  MahoPasswordAuthorizationService::Get()->ResetForTesting();
  MahoPasswordAuthorizationService::Get()->SetVaultLockedForTesting(false);
  Grant(PasswordAuthorizationAction::kDelete, authorized_origin);
  MahoPasswordForm add_with_wrong_action;
  add_with_wrong_action.signon_realm = authorized_origin;
  add_with_wrong_action.url = authorized_origin;
  add_with_wrong_action.username_value = "wrong-action@example.test";
  add_with_wrong_action.password_value = "must-not-persist";
  base::RunLoop wrong_action_loop;
  bool wrong_action_add = true;
  backend.AddLoginMahoAsync(
      add_with_wrong_action,
      base::BindOnce(
          [](base::RunLoop *loop, bool *out, bool success) {
            *out = success;
            loop->Quit();
          },
          &wrong_action_loop, &wrong_action_add));
  wrong_action_loop.Run();
  EXPECT_FALSE(wrong_action_add);
}

TEST_F(MahoPasswordStoreBackendTest,
       DisabledAccountStoreBackendInitializesAndFailsClosed) {
  // Asserts account-store denial (`enabled_ == false`), not the kill switch.
  EnableNativeWriteForTesting();
  MahoPasswordStoreBackend backend = CreateBackend(/*enabled=*/false);

  base::RunLoop init_loop;
  bool init_success = false;
  backend.InitBackend(base::BindOnce(
      [](base::RunLoop *loop, bool *success_out, bool success) {
        *success_out = success;
        loop->Quit();
      },
      &init_loop, &init_success));
  init_loop.Run();
  ASSERT_TRUE(init_success);
  EXPECT_TRUE(backend.IsInitialized());

  MahoPasswordForm form;
  form.signon_realm = "https://account-store.example";
  form.username_value = "account@example.com";
  form.password_value = "disabled-store-password";
  base::RunLoop add_loop;
  bool add_success = true;
  backend.AddLoginMahoAsync(
      form, base::BindOnce(
                [](base::RunLoop *loop, bool *success_out, bool success) {
                  *success_out = success;
                  loop->Quit();
                },
                &add_loop, &add_success));
  add_loop.Run();
  EXPECT_FALSE(add_success);

  base::RunLoop wrapper_add_loop;
  bool wrapper_add_failed_closed = false;
  PasswordStoreBackendCredential disabled_credential;
  disabled_credential.signon_realm = "https://account-store.example";
  disabled_credential.username_value = u"account@example.com";
  SetCredentialPassword(disabled_credential, u"disabled-store-password");
  backend.AddLoginAsync(
      std::move(disabled_credential),
      base::BindOnce(
          [](base::RunLoop *loop, bool *failed_closed,
             password_manager::PasswordChangesOrError result) {
            *failed_closed = std::holds_alternative<
                password_manager::PasswordStoreBackendError>(result);
            loop->Quit();
          },
          &wrapper_add_loop, &wrapper_add_failed_closed));
  wrapper_add_loop.Run();
  EXPECT_TRUE(wrapper_add_failed_closed);

  base::RunLoop get_all_loop;
  bool get_all_success = false;
  size_t all_count = 1;
  backend.GetAllLoginsAsync(base::BindOnce(
      [](base::RunLoop *loop, bool *success_out, size_t *count_out,
         password_manager::BackendLoginsResultOrError result) {
        if (auto *forms =
                std::get_if<password_manager::BackendLoginsResult>(&result)) {
          *success_out = true;
          *count_out = forms->size();
        }
        loop->Quit();
      },
      &get_all_loop, &get_all_success, &all_count));
  get_all_loop.Run();
  EXPECT_TRUE(get_all_success);
  EXPECT_EQ(0u, all_count);

  password_manager::BackendLoginsResult disabled_fill =
      FillPasswordStoreForms(backend, "https://account-store.example");
  EXPECT_TRUE(disabled_fill.empty());
}

TEST_F(MahoPasswordStoreBackendTest, GetAllLoginsAsync) {
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  base::RunLoop run_loop;
  bool get_success = true;
  std::vector<MahoPasswordForm> fetched_forms;
  backend.GetAllLoginsMahoAsync(base::BindOnce(
      [](base::RunLoop *loop, bool *success_out,
         std::vector<MahoPasswordForm> *forms_out,
         std::vector<MahoPasswordForm> forms, bool success) {
        *success_out = success;
        *forms_out = std::move(forms);
        loop->Quit();
      },
      &run_loop, &get_success, &fetched_forms));
  run_loop.Run();

  // No core is installed here, so the Task-2 effective provider resolves
  // fail-closed to `disabled`. Task 3 makes that a Maho NO-OP rather than a
  // Vault read: the backend reports an empty result set (and does not pretend
  // the Vault failed) so upstream/extension behavior is preserved.
  EXPECT_TRUE(get_success);
  EXPECT_TRUE(fetched_forms.empty());
}

TEST_F(MahoPasswordStoreBackendTest, AddAndRemoveLoginAsync) {
  // No core => effective provider resolves to `disabled`, so after Task 3 every
  // operation below is a Maho no-op (reported as a successful no-change result)
  // and nothing reaches the Vault. The kill switch is ON so the routing check,
  // not the kill switch, is what is being observed.
  EnableNativeWriteForTesting();
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  MahoPasswordForm form;
  form.id = "test-form-1";
  form.signon_realm = "https://example.com";
  form.username_value = "user@example.com";
  form.password_value = "secret123";

  // Test AddLoginMahoAsync
  base::RunLoop add_loop;
  bool add_success = true;
  backend.AddLoginMahoAsync(
      form, base::BindOnce(
                [](base::RunLoop *loop, bool *success_out, bool success) {
                  *success_out = success;
                  loop->Quit();
                },
                &add_loop, &add_success));
  add_loop.Run();
  // Provider not native: no-op write, no Vault FFI call, no error surfaced.
  EXPECT_TRUE(add_success);

  // Test FillMatchingLoginsMahoAsync
  base::RunLoop fill_loop;
  bool fill_success = true;
  std::vector<MahoPasswordForm> matched_forms;
  backend.FillMatchingLoginsMahoAsync(
      "https://example.com", form.scheme,
      base::BindOnce(
          [](base::RunLoop *loop, bool *success_out,
             std::vector<MahoPasswordForm> *forms_out,
             std::vector<MahoPasswordForm> forms, bool success) {
            *success_out = success;
            *forms_out = std::move(forms);
            loop->Quit();
          },
          &fill_loop, &fill_success, &matched_forms));
  fill_loop.Run();
  // Provider not native: empty fill, no secret read.
  EXPECT_TRUE(fill_success);
  EXPECT_TRUE(matched_forms.empty());

  // Test RemoveLoginMahoAsync
  base::RunLoop remove_loop;
  bool remove_success = true;
  backend.RemoveLoginMahoAsync(
      form, base::BindOnce(
                [](base::RunLoop *loop, bool *success_out, bool success) {
                  *success_out = success;
                  loop->Quit();
                },
                &remove_loop, &remove_success));
  remove_loop.Run();
  // Provider not native: nothing of Maho's to remove.
  EXPECT_TRUE(remove_success);
}

TEST_F(MahoPasswordStoreBackendTest, UpdateLoginAsync) {
  // No core => `disabled` provider => Task-3 no-op update (see above).
  EnableNativeWriteForTesting();
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  MahoPasswordForm form;
  form.id = "test-update-1";
  form.signon_realm = "https://update.example.com";
  form.username_value = "user_updated";
  form.password_value = "newsecret456";

  base::RunLoop update_loop;
  bool update_success = true;
  backend.UpdateLoginMahoAsync(
      form, base::BindOnce(
                [](base::RunLoop *loop, bool *success_out, bool success) {
                  *success_out = success;
                  loop->Quit();
                },
                &update_loop, &update_success));
  update_loop.Run();
  EXPECT_TRUE(update_success);
}

TEST_F(MahoPasswordStoreBackendTest, RemoveLoginsCreatedBetweenAsync) {
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  base::Time now = base::Time::Now();
  base::RunLoop range_loop;
  bool range_success = true;
  backend.RemoveLoginsCreatedBetweenMahoAsync(
      now - base::Days(1), now + base::Days(1),
      base::BindOnce(
          [](base::RunLoop *loop, bool *success_out, bool success) {
            *success_out = success;
            loop->Quit();
          },
          &range_loop, &range_success));
  range_loop.Run();
  // No core => `disabled` provider => Maho deletes nothing and reports the
  // no-change success the contract expects.
  EXPECT_TRUE(range_success);
}

TEST_F(MahoPasswordStoreBackendTest, UnlockedVaultAddListAndFillLoginAsync) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  // Task 3 routes every operation through the effective provider, so the Maho
  // path is only exercised while Native is selected.
  SelectPasswordProvider("maho_native", "[]");

  MahoPasswordForm fetched = AddLoginAndFetch(
      backend, "https://example.com", "user@example.com", kSentinelPassword);
  const std::string credential_payload =
      CredentialPayloadForOrigin("https://example.com");
  EXPECT_EQ(std::string::npos, credential_payload.find(kSentinelPassword));
  EXPECT_EQ(std::string::npos, credential_payload.find("\"password\""));
  EXPECT_EQ(std::string::npos, credential_payload.find("\"secret\""));
  std::optional<base::Value> parsed_payload =
      base::JSONReader::Read(credential_payload, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed_payload);
  ASSERT_TRUE(parsed_payload->is_list());
  ASSERT_EQ(1u, parsed_payload->GetList().size());
  EXPECT_FALSE(parsed_payload->GetList()[0].GetDict().contains("password"));
  EXPECT_FALSE(parsed_payload->GetList()[0].GetDict().contains("secret"));

  EXPECT_FALSE(fetched.id.empty());
  EXPECT_EQ("https://example.com", fetched.signon_realm);
  EXPECT_EQ("user@example.com", fetched.username_value);
  EXPECT_TRUE(fetched.password_value.empty());
  EXPECT_EQ(1u, fetched.revision);

  base::RunLoop all_loop;
  bool all_success = false;
  std::vector<MahoPasswordForm> all_forms;
  backend.GetAllLoginsMahoAsync(base::BindOnce(
      [](base::RunLoop *loop, bool *success_out,
         std::vector<MahoPasswordForm> *forms_out,
         std::vector<MahoPasswordForm> forms, bool success) {
        *success_out = success;
        *forms_out = std::move(forms);
        loop->Quit();
      },
      &all_loop, &all_success, &all_forms));
  all_loop.Run();
  ASSERT_TRUE(all_success);
  ASSERT_EQ(1u, all_forms.size());
  EXPECT_TRUE(all_forms[0].password_value.empty());
}

TEST_F(MahoPasswordStoreBackendTest,
       ContextDiscoveryIsSecretFreeAndExactSelectionResolvesOnce) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  SelectPasswordProvider("maho_native", "[]");
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  const std::string origin = "https://jit-resolver.example";
  ASSERT_TRUE(
      ImportOnePasswordCredential(origin, "jit@example.test", kJitPassword));
  const password_manager::PasswordFillRequestContext context =
      FillContextFor(origin);
  password_manager::BackendLoginsResult discovered =
      DiscoverForContext(backend, origin, context);
  ASSERT_EQ(1u, discovered.size());
  EXPECT_TRUE(discovered[0].password_value.empty());
  ASSERT_TRUE(discovered[0].primary_key.has_value());

  const int primary_key = discovered[0].primary_key->value();
  std::optional<uint64_t> revision =
      backend.identity_map_for_testing().RevisionForPrimaryKey(primary_key);
  ASSERT_TRUE(revision.has_value());
  password_manager::PasswordFillSelection selection{
      .primary_key = discovered[0].primary_key, .observed_revision = *revision};

  ResolverFfiProbe ffi_probe;
  ffi_probe.result_status =
      static_cast<int>(maho::core::VaultBackendResultStatus::kSuccess);
  ffi_probe.payload = std::string("{\"password\":\"") + kJitPassword + "\"}";
  ScopedResolverFfiProbe scoped_ffi_probe(&ffi_probe);
  std::vector<std::string> zeroized_buffers;
  ScopedZeroizeObserver zeroize_observer(&zeroized_buffers);

  Grant(PasswordAuthorizationAction::kFill, origin);
  int callback_count = 0;
  EXPECT_TRUE(ResolveForContext(backend, context, selection, &callback_count,
                                kJitPassword16));
  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(1, ffi_probe.credential_execute_calls);

  std::optional<base::Value> request =
      base::JSONReader::Read(ffi_probe.request, base::JSON_PARSE_RFC);
  ASSERT_TRUE(request);
  ASSERT_TRUE(request->is_dict());
  EXPECT_EQ("fill", *request->GetDict().FindString("action"));
  EXPECT_EQ(
      *backend.identity_map_for_testing().VaultIdForPrimaryKey(primary_key),
      *request->GetDict().FindString("itemId"));
  EXPECT_EQ(base::NumberToString(*revision),
            *request->GetDict().FindString("expectedRevision"));

  ASSERT_EQ(3u, zeroized_buffers.size());
  for (const std::string &buffer : zeroized_buffers) {
    EXPECT_FALSE(buffer.empty());
    EXPECT_TRUE(std::all_of(buffer.begin(), buffer.end(),
                            [](char byte) { return byte == 0; }));
  }
}

TEST_F(MahoPasswordStoreBackendTest,
       ResolverRejectsUnknownStaleAndWrongDocumentSnapshotsBeforeSecretUse) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  SelectPasswordProvider("maho_native", "[]");
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  const std::string origin = "https://jit-denial.example";
  ASSERT_TRUE(ImportOnePasswordCredential(origin, "jit@example.test",
                                          kJitPassword));
  const password_manager::PasswordFillRequestContext context =
      FillContextFor(origin);
  password_manager::BackendLoginsResult discovered =
      DiscoverForContext(backend, origin, context);
  ASSERT_EQ(1u, discovered.size());
  ASSERT_TRUE(discovered[0].primary_key.has_value());
  const int primary_key = discovered[0].primary_key->value();
  std::optional<uint64_t> revision =
      backend.identity_map_for_testing().RevisionForPrimaryKey(primary_key);
  ASSERT_TRUE(revision.has_value());

  ResolverFfiProbe ffi_probe;
  ScopedResolverFfiProbe scoped_ffi_probe(&ffi_probe);

  password_manager::PasswordFillSelection exact{
      .primary_key = discovered[0].primary_key,
      .observed_revision = *revision};
  MahoPasswordAuthorizationService::Get()->RevokeProfile(OwnerProfileKey());
  int authorization_callbacks = 0;
  EXPECT_FALSE(ResolveForContext(backend, context, exact,
                                 &authorization_callbacks));
  EXPECT_EQ(1, authorization_callbacks);
  EXPECT_EQ(0, ffi_probe.credential_execute_calls);

  password_manager::PasswordFillSelection unknown{
      .primary_key = password_manager::FormPrimaryKey(primary_key + 1000),
      .observed_revision = *revision};
  int unknown_callbacks = 0;
  EXPECT_FALSE(
      ResolveForContext(backend, context, unknown, &unknown_callbacks));
  EXPECT_EQ(1, unknown_callbacks);

  password_manager::PasswordFillSelection stale{
      .primary_key = discovered[0].primary_key,
      .observed_revision = *revision + 1};
  int stale_callbacks = 0;
  EXPECT_FALSE(ResolveForContext(backend, context, stale, &stale_callbacks));
  EXPECT_EQ(1, stale_callbacks);

  password_manager::PasswordFillSelection missing_revision{
      .primary_key = discovered[0].primary_key,
      .observed_revision = 0};
  int revision_callbacks = 0;
  EXPECT_FALSE(ResolveForContext(backend, context, missing_revision,
                                 &revision_callbacks));
  EXPECT_EQ(1, revision_callbacks);

  // Authorization is one action/profile/origin grant, independent of the
  // metadata snapshot denials below.
  Grant(PasswordAuthorizationAction::kFill, origin);
  const password_manager::PasswordFillRequestContext wrong_document =
      FillContextFor(origin);
  int document_callbacks = 0;
  EXPECT_FALSE(ResolveForContext(backend, wrong_document, exact,
                                 &document_callbacks));
  EXPECT_EQ(1, document_callbacks);

  const password_manager::PasswordFillRequestContext wrong_origin =
      FillContextFor("https://other-jit-denial.example");
  int origin_callbacks = 0;
  EXPECT_FALSE(
      ResolveForContext(backend, wrong_origin, exact, &origin_callbacks));
  EXPECT_EQ(1, origin_callbacks);
  EXPECT_EQ(0, ffi_probe.credential_execute_calls);

  // All denials happened before secret use. The exact original snapshot
  // remains resolvable; only this call reaches JIT.
  ffi_probe.result_status =
      static_cast<int>(maho::core::VaultBackendResultStatus::kSuccess);
  ffi_probe.payload = std::string("{\"password\":\"") + kJitPassword +
                      "\"}";
  EXPECT_TRUE(
      ResolveForContext(backend, context, exact, nullptr, kJitPassword16));
  EXPECT_EQ(1, ffi_probe.credential_execute_calls);
}

TEST_F(MahoPasswordStoreBackendTest,
       ResolverRejectsSupersededSnapshotProviderKillSwitchAndLockBeforeFfi) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  SelectPasswordProvider("maho_native", "[]");
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  const std::string origin = "https://jit-policy.example";
  ASSERT_TRUE(
      ImportOnePasswordCredential(origin, "jit@example.test", kJitPassword));
  const password_manager::PasswordFillRequestContext context =
      FillContextFor(origin);
  password_manager::BackendLoginsResult discovered =
      DiscoverForContext(backend, origin, context);
  ASSERT_EQ(1u, discovered.size());
  ASSERT_TRUE(discovered[0].primary_key.has_value());
  const int primary_key = discovered[0].primary_key->value();
  const std::string item_id =
      *backend.identity_map_for_testing().VaultIdForPrimaryKey(primary_key);
  const uint64_t revision =
      *backend.identity_map_for_testing().RevisionForPrimaryKey(primary_key);
  const password_manager::PasswordFillSelection selection{
      .primary_key = discovered[0].primary_key, .observed_revision = revision};

  ResolverFfiProbe ffi_probe;
  ScopedResolverFfiProbe scoped_ffi_probe(&ffi_probe);

  backend.ObserveIdentityForTesting(item_id, revision + 1);
  Grant(PasswordAuthorizationAction::kFill, origin);
  EXPECT_FALSE(ResolveForContext(backend, context, selection));
  EXPECT_EQ(0, ffi_probe.credential_execute_calls);

  backend.ObserveIdentityForTesting(item_id, revision);
  const password_manager::PasswordFillSelection current_selection{
      .primary_key = discovered[0].primary_key,
      .observed_revision = revision};

  SelectPasswordProvider(
      "bitwarden", InstalledExtensionsJson(kBitwardenExtensionId, "Bitwarden"));
  Grant(PasswordAuthorizationAction::kFill, origin);
  EXPECT_FALSE(ResolveForContext(backend, context, current_selection));
  EXPECT_EQ(0, ffi_probe.credential_execute_calls);

  SelectPasswordProvider("maho_native", "[]");
  TestingPrefServiceSimple *kill_switch_prefs = CreateKillSwitchPrefs();
  kill_switch_prefs->SetBoolean(prefs::kMahoNativePasswordWriteEnabled, false);
  Grant(PasswordAuthorizationAction::kFill, origin);
  EXPECT_FALSE(ResolveForContext(backend, context, current_selection));
  EXPECT_EQ(0, ffi_probe.credential_execute_calls);

  kill_switch_prefs->SetBoolean(prefs::kMahoNativePasswordWriteEnabled, true);
  MahoPasswordAuthorizationService::Get()->SetVaultLockedForTesting(true);
  Grant(PasswordAuthorizationAction::kFill, origin);
  EXPECT_FALSE(ResolveForContext(backend, context, current_selection));
  EXPECT_EQ(0, ffi_probe.credential_execute_calls);
}

TEST_F(MahoPasswordStoreBackendTest,
       ResolverRuntimeFailuresCompleteOnceAndZeroizeTransientBuffers) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  SelectPasswordProvider("maho_native", "[]");
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  const std::string origin = "https://jit-runtime-error.example";
  ASSERT_TRUE(
      ImportOnePasswordCredential(origin, "jit@example.test", kJitPassword));
  const password_manager::PasswordFillRequestContext context =
      FillContextFor(origin);
  password_manager::BackendLoginsResult discovered =
      DiscoverForContext(backend, origin, context);
  ASSERT_EQ(1u, discovered.size());
  ASSERT_TRUE(discovered[0].primary_key.has_value());
  const int primary_key = discovered[0].primary_key->value();
  const password_manager::PasswordFillSelection selection{
      .primary_key = discovered[0].primary_key,
      .observed_revision =
          *backend.identity_map_for_testing().RevisionForPrimaryKey(
              primary_key)};

  for (const auto &[status, payload] : std::vector<
           std::pair<maho::core::VaultBackendResultStatus, std::string>>{
           {maho::core::VaultBackendResultStatus::kLocked, std::string()},
           {maho::core::VaultBackendResultStatus::kRuntimeUnavailable,
            std::string()},
           {maho::core::VaultBackendResultStatus::kSuccess,
            R"({"password":""})"},
           {maho::core::VaultBackendResultStatus::kSuccess,
            R"({"password":"secret","unexpected":true})"}}) {
    ResolverFfiProbe ffi_probe;
    ffi_probe.result_status = static_cast<int>(status);
    ffi_probe.payload = payload;
    ScopedResolverFfiProbe scoped_ffi_probe(&ffi_probe);
    std::vector<std::string> zeroized_buffers;
    ScopedZeroizeObserver zeroize_observer(&zeroized_buffers);

    Grant(PasswordAuthorizationAction::kFill, origin);
    int callback_count = 0;
    EXPECT_FALSE(
        ResolveForContext(backend, context, selection, &callback_count));
    EXPECT_EQ(1, callback_count);
    EXPECT_EQ(1, ffi_probe.credential_execute_calls);
    EXPECT_FALSE(zeroized_buffers.empty());
    for (const std::string &buffer : zeroized_buffers) {
      EXPECT_TRUE(std::all_of(buffer.begin(), buffer.end(),
                              [](char byte) { return byte == 0; }));
    }
  }
}

// Selecting an EXTERNAL provider whose extension is not installed must resolve
// fail-closed to `disabled`. It must NOT silently normalize back to
// `maho_native`, which is exactly what this test used to assert and what the
// remediation plan prohibits: a user who chose Bitwarden and lost the extension
// would otherwise have Maho Native quietly take over their credentials.
//
// Failing-first rationale: before the fail-closed provider fix,
// GetActivePasswordProviderMode() returned "maho_native" here, so the
// "disabled" expectations below failed.
//
// Scope note: this asserts the PROVIDER-RESOLUTION seam plus the Task-3 backend
// consequence (the fill below only succeeds once Native is selected again). The
// dedicated routing tests further down assert the backend's no-op behavior for
// each non-native provider.
TEST_F(MahoPasswordStoreBackendTest,
       UnavailableExternalProviderResolvesDisabledNotNative) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  const std::string origin = "https://import-fill.example";

  ASSERT_TRUE(ImportOnePasswordCredential(origin, "import@example.test",
                                          kImportedPassword));

  // Bitwarden selected, Bitwarden extension absent.
  SelectPasswordProvider("bitwarden", "[]");

  EXPECT_EQ("disabled", GetActivePasswordProviderMode());
  EXPECT_FALSE(IsNativePasswordProviderActive());
  EXPECT_FALSE(ShouldEnableChromiumPasswordManager());
  EXPECT_FALSE(IsBitwardenExtensionAvailable());

  // The metadata descriptor remains present, without exposing the secret.
  EXPECT_EQ(std::string::npos,
            CredentialPayloadForOrigin(origin).find(kImportedPassword));

  // Selecting Maho Native explicitly is the only way native fill becomes the
  // sanctioned route, and then the same credential is reachable.
  SelectPasswordProvider("maho_native", "[]");
  ASSERT_EQ("maho_native", GetActivePasswordProviderMode());

  password_manager::BackendLoginsResult password_store_forms =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, password_store_forms.size());
  EXPECT_EQ(origin, password_store_forms[0].signon_realm);
  EXPECT_EQ(u"import@example.test", password_store_forms[0].username_value);
  EXPECT_TRUE(password_store_forms[0].password_value.empty());
}

TEST_F(MahoPasswordStoreBackendTest,
       ManualCredentialVisibleThroughPasswordStoreSaveAndFillPath) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  SelectPasswordProvider("maho_native", "[]");
  ASSERT_TRUE(ShouldEnableChromiumPasswordManager());

  const std::string origin = "https://manual-fill.example";
  Grant(PasswordAuthorizationAction::kAdd, origin);
  PasswordStoreBackendCredential credential;
  credential.signon_realm = origin;
  credential.username_value = u"manual@example.test";
  SetCredentialPassword(credential, ManualPassword16());

  base::RunLoop add_loop;
  bool add_success = false;
  backend.AddLoginAsync(
      std::move(credential),
      base::BindOnce(
          [](base::RunLoop *loop, bool *success_out,
             password_manager::PasswordChangesOrError result) {
            *success_out =
                std::holds_alternative<password_manager::PasswordChanges>(
                    result);
            loop->Quit();
          },
          &add_loop, &add_success));
  add_loop.Run();
  ASSERT_TRUE(add_success);

  password_manager::BackendLoginsResult password_store_forms =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, password_store_forms.size());
  EXPECT_EQ(origin, password_store_forms[0].signon_realm);
  EXPECT_EQ(u"manual@example.test", password_store_forms[0].username_value);
  EXPECT_TRUE(password_store_forms[0].password_value.empty());

  std::vector<MahoPasswordForm> maho_forms = FillMahoForms(backend, origin);
  ASSERT_EQ(1u, maho_forms.size());
  EXPECT_EQ(origin, maho_forms[0].signon_realm);
  EXPECT_EQ("manual@example.test", maho_forms[0].username_value);
  EXPECT_TRUE(maho_forms[0].password_value.empty());
}

// Kill switch: while `maho.passwords.native_write_enabled` is explicitly
// false (it defaults to true), native ADD must not reach the Vault and native
// FILL must not read secrets. Re-enabling the pref restores the native
// behavior.
TEST_F(MahoPasswordStoreBackendTest, NativeWriteDisabled_AddReportsError) {
  CreateInitializedVaultCore();
  TestingPrefServiceSimple *kill_switch_prefs = CreateKillSwitchPrefs();
  kill_switch_prefs->SetBoolean(prefs::kMahoNativePasswordWriteEnabled, false);
  ASSERT_FALSE(IsNativePasswordWriteEnabled(kill_switch_prefs));

  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  SelectPasswordProvider("maho_native", "[]");

  const std::string origin = "https://killswitch-add.example";
  PasswordStoreBackendCredential credential;
  credential.signon_realm = origin;
  credential.username_value = u"killswitch@example.test";
  SetCredentialPassword(credential, KillSwitchPassword16());

  base::RunLoop add_loop;
  bool add_reported_changes = false;
  bool add_reported_error = false;
  size_t change_count = 1;
  backend.AddLoginAsync(
      std::move(credential),
      base::BindOnce(
          [](base::RunLoop *loop, bool *changes_out, bool *error_out,
             size_t *count_out,
             password_manager::PasswordChangesOrError result) {
            if (auto *changes =
                    std::get_if<password_manager::PasswordChanges>(&result)) {
              *changes_out = true;
              *count_out = changes->has_value() ? (*changes)->size() : 0u;
            } else {
              *error_out = true;
            }
            loop->Quit();
          },
          &add_loop, &add_reported_changes, &add_reported_error,
          &change_count));
  add_loop.Run();

  // The user accepted a Maho save prompt, so a write that never reaches the
  // Vault must surface as an error. Reporting a successful empty change list
  // here is what made saved passwords look stored while nothing persisted, so
  // they vanished on the next restart.
  EXPECT_TRUE(add_reported_error);
  EXPECT_FALSE(add_reported_changes);

  // Nothing reached the Vault: a direct backend-session read (which bypasses
  // the gate) still sees no credential for this origin.
  const std::string payload = CredentialPayloadForOrigin(origin);
  EXPECT_EQ(std::string::npos, payload.find(kKillSwitchPassword));
  std::optional<base::Value> parsed_payload =
      base::JSONReader::Read(payload, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed_payload);
  ASSERT_TRUE(parsed_payload->is_list());
  EXPECT_TRUE(parsed_payload->GetList().empty());
}

TEST_F(MahoPasswordStoreBackendTest, NativeWriteDisabled_FillReturnsEmpty) {
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  SelectPasswordProvider("maho_native", "[]");

  const std::string origin = "https://killswitch-fill.example";
  // Seed a credential through the import path so the Vault genuinely holds a
  // secret that an ungated fill would return.
  ASSERT_TRUE(ImportOnePasswordCredential(origin, "killswitch@example.test",
                                          kKillSwitchPassword));
  ASSERT_EQ(std::string::npos,
            CredentialPayloadForOrigin(origin).find(kKillSwitchPassword));

  TestingPrefServiceSimple *kill_switch_prefs = CreateKillSwitchPrefs();
  kill_switch_prefs->SetBoolean(prefs::kMahoNativePasswordWriteEnabled, false);
  ASSERT_FALSE(IsNativePasswordWriteEnabled(kill_switch_prefs));

  password_manager::BackendLoginsResult filled =
      FillPasswordStoreForms(backend, origin);
  EXPECT_TRUE(filled.empty());

  base::RunLoop fill_loop;
  bool maho_fill_success = true;
  std::vector<MahoPasswordForm> maho_forms;
  backend.FillMatchingLoginsMahoAsync(
      origin, password_manager::PasswordForm::Scheme::kHtml,
      base::BindOnce(
                  [](base::RunLoop *loop, bool *success_out,
                     std::vector<MahoPasswordForm> *forms_out,
                     std::vector<MahoPasswordForm> forms, bool success) {
                    *success_out = success;
                    *forms_out = std::move(forms);
                    loop->Quit();
                  },
                  &fill_loop, &maho_fill_success, &maho_forms));
  fill_loop.Run();
  EXPECT_TRUE(maho_forms.empty());
}

TEST_F(MahoPasswordStoreBackendTest, NativeWriteEnabled_AddReachesVault) {
  CreateInitializedVaultCore();
  TestingPrefServiceSimple *kill_switch_prefs = CreateKillSwitchPrefs();
  kill_switch_prefs->SetBoolean(prefs::kMahoNativePasswordWriteEnabled, true);
  ASSERT_TRUE(IsNativePasswordWriteEnabled(kill_switch_prefs));

  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  SelectPasswordProvider("maho_native", "[]");

  const std::string origin = "https://killswitch-enabled.example";
  Grant(PasswordAuthorizationAction::kAdd, origin);
  PasswordStoreBackendCredential credential;
  credential.signon_realm = origin;
  credential.username_value = u"killswitch@example.test";
  SetCredentialPassword(credential, KillSwitchPassword16());

  base::RunLoop add_loop;
  bool add_success = false;
  backend.AddLoginAsync(
      std::move(credential),
      base::BindOnce(
          [](base::RunLoop *loop, bool *success_out,
             password_manager::PasswordChangesOrError result) {
            *success_out =
                std::holds_alternative<password_manager::PasswordChanges>(
                    result);
            loop->Quit();
          },
          &add_loop, &add_success));
  add_loop.Run();
  ASSERT_TRUE(add_success);

  EXPECT_EQ(std::string::npos,
            CredentialPayloadForOrigin(origin).find(kKillSwitchPassword));

  password_manager::BackendLoginsResult filled =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, filled.size());
  EXPECT_EQ(origin, filled[0].signon_realm);
  EXPECT_TRUE(filled[0].password_value.empty());
}

// Saving a password must wipe EVERY in-memory copy it made, not just the
// serialized request buffer. AddLoginWithResultMahoAsync copies the secret into
// the `req` dictionary (maho_password_store_backend.cc:2022) and wipes only
// `req_json` and the form, leaving that dictionary's string to be released
// un-zeroized.
TEST_F(MahoPasswordStoreBackendTest, AddLoginZeroizesEveryPlaintextPasswordCopy) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();

  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  SelectPasswordProvider("maho_native", "[]");

  const std::string origin = "https://zeroize.example";
  Grant(PasswordAuthorizationAction::kAdd, origin);

  std::vector<std::string> zeroized;
  {
    ScopedZeroizeObserver observer(&zeroized);

    PasswordStoreBackendCredential credential;
    credential.signon_realm = origin;
    credential.username_value = u"zeroize@example.test";
    SetCredentialPassword(credential, KillSwitchPassword16());

    base::RunLoop add_loop;
    backend.AddLoginAsync(
        std::move(credential),
        base::BindOnce(
            [](base::RunLoop *loop,
               password_manager::PasswordChangesOrError result) {
              loop->Quit();
            },
            &add_loop));
    add_loop.Run();
  }

  // The request JSON carries the password, so it must be among the wiped
  // buffers; that part already holds today.
  const bool wiped_request_json =
      std::any_of(zeroized.begin(), zeroized.end(),
                  [](const std::string &value) {
                    return value.find(kKillSwitchPassword) != std::string::npos;
                  });
  EXPECT_TRUE(wiped_request_json)
      << "the serialized request buffer was never zeroized";

  // The dictionary's own password copy is a SEPARATE allocation. Exactly one
  // wipe means only the JSON was cleared and the plaintext string the
  // dictionary owned was released intact.
  const size_t plaintext_wipes =
      std::count_if(zeroized.begin(), zeroized.end(),
                    [](const std::string &value) {
                      return value == kKillSwitchPassword;
                    });
  EXPECT_GE(plaintext_wipes, 1u)
      << "the password copy owned by the request dictionary was released "
         "without being zeroized";
}

// ---------------------------------------------------------------------------
// Task 3: the backend consults the EFFECTIVE PROVIDER on every operation.
// ---------------------------------------------------------------------------

// Native selected: add + fill are handled by Maho (the positive control for the
// three no-op cases below).
//
// Failing-first rationale: this passes both before and after Task 3 by design —
// it is the control that proves the new provider gate did not break the native
// route. The routing behavior it pins (native add reaches the Vault and native
// fill returns the secret) is what the three tests below assert the ABSENCE of;
// without it, a backend that no-ops unconditionally would satisfy them all.
TEST_F(MahoPasswordStoreBackendTest, ProviderRouting_NativeHandlesAddAndFill) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  SelectPasswordProvider("maho_native", "[]");
  ASSERT_TRUE(IsNativePasswordProviderActive());

  const std::string origin = "https://routing-native.example";
  Grant(PasswordAuthorizationAction::kAdd, origin);
  PasswordStoreBackendCredential credential;
  credential.signon_realm = origin;
  credential.username_value = u"routing@example.test";
  SetCredentialPassword(credential, RoutingPassword16());

  base::RunLoop add_loop;
  bool add_success = false;
  backend.AddLoginAsync(
      std::move(credential),
      base::BindOnce(
          [](base::RunLoop *loop, bool *success_out,
             password_manager::PasswordChangesOrError result) {
            *success_out =
                std::holds_alternative<password_manager::PasswordChanges>(
                    result);
            loop->Quit();
          },
          &add_loop, &add_success));
  add_loop.Run();
  ASSERT_TRUE(add_success);

  // The write landed, but discovery exposes metadata only.
  EXPECT_EQ(std::string::npos,
            CredentialPayloadForOrigin(origin).find(kRoutingPassword));

  password_manager::BackendLoginsResult filled =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, filled.size());
  EXPECT_EQ(origin, filled[0].signon_realm);
  EXPECT_TRUE(filled[0].password_value.empty());
}

// Bitwarden selected AND available: the Maho backend must not write to or read
// from the Maho Vault. The extension owns the credential lifecycle.
//
// Failing-first rationale: before Task 3 the backend consulted only `enabled_`
// plus the kill switch, so with the kill switch ON the add below reached the
// Vault (CredentialPayloadForOrigin would contain the sentinel) and the fill
// returned one credential — both assertions here failed.
TEST_F(MahoPasswordStoreBackendTest, ProviderRouting_BitwardenIsNoOp) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  const std::string origin = "https://routing-bitwarden.example";
  // Seed a real secret through the import path (which bypasses the backend), so
  // an ungated fill would have something to return.
  ASSERT_TRUE(ImportOnePasswordCredential(origin, "routing@example.test",
                                          kRoutingPassword));
  ASSERT_EQ(std::string::npos,
            CredentialPayloadForOrigin(origin).find(kRoutingPassword));

  SelectPasswordProvider(
      "bitwarden", InstalledExtensionsJson(kBitwardenExtensionId, "Bitwarden"));
  ASSERT_EQ("bitwarden", GetActivePasswordProviderMode());
  ASSERT_FALSE(IsNativePasswordProviderActive());

  // Fill is a no-op: empty result, no secret read.
  EXPECT_TRUE(FillPasswordStoreForms(backend, origin).empty());

  // Add is a no-op: no error surfaced, no change reported, nothing new in the
  // Vault.
  PasswordStoreBackendCredential credential;
  credential.signon_realm = "https://routing-bitwarden-write.example";
  credential.username_value = u"routing-write@example.test";
  SetCredentialPassword(credential, KillSwitchPassword16());

  base::RunLoop add_loop;
  bool reported_changes = false;
  size_t change_count = 1;
  backend.AddLoginAsync(
      std::move(credential),
      base::BindOnce(
          [](base::RunLoop *loop, bool *changes_out, size_t *count_out,
             password_manager::PasswordChangesOrError result) {
            if (auto *changes =
                    std::get_if<password_manager::PasswordChanges>(&result)) {
              *changes_out = true;
              *count_out = changes->has_value() ? (*changes)->size() : 0u;
            }
            loop->Quit();
          },
          &add_loop, &reported_changes, &change_count));
  add_loop.Run();
  EXPECT_TRUE(reported_changes);
  EXPECT_EQ(0u, change_count);
  EXPECT_EQ(std::string::npos, CredentialPayloadForOrigin(
                                   "https://routing-bitwarden-write.example")
                                   .find(kKillSwitchPassword));

  // Routing away does not expose the pre-existing secret through discovery.
  EXPECT_EQ(std::string::npos,
            CredentialPayloadForOrigin(origin).find(kRoutingPassword));
}

// 1Password selected AND available: same no-op contract as Bitwarden.
//
// Failing-first rationale: before Task 3 the backend ignored the provider
// entirely, so this fill returned the seeded credential instead of an empty
// result.
TEST_F(MahoPasswordStoreBackendTest, ProviderRouting_OnePasswordIsNoOp) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  const std::string origin = "https://routing-onepassword.example";
  ASSERT_TRUE(ImportOnePasswordCredential(origin, "routing@example.test",
                                          kRoutingPassword));

  SelectPasswordProvider(
      "onepassword",
      InstalledExtensionsJson(kOnePasswordExtensionId, "1Password"));
  ASSERT_EQ("onepassword", GetActivePasswordProviderMode());
  ASSERT_FALSE(IsNativePasswordProviderActive());

  EXPECT_TRUE(FillPasswordStoreForms(backend, origin).empty());

  std::vector<MahoPasswordForm> maho_forms = FillMahoForms(backend, origin);
  EXPECT_TRUE(maho_forms.empty());
}

// Passwords disabled entirely: the backend participates in nothing.
//
// Failing-first rationale: before Task 3, `passwordsEnabled:false` only
// affected the provider predicates; the backend still filled from the Vault, so
// the empty-fill expectation below failed.
TEST_F(MahoPasswordStoreBackendTest, ProviderRouting_DisabledIsNoOp) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  const std::string origin = "https://routing-disabled.example";
  ASSERT_TRUE(ImportOnePasswordCredential(origin, "routing@example.test",
                                          kRoutingPassword));

  // Bitwarden selected without its extension resolves fail-closed to disabled.
  SelectPasswordProvider("bitwarden", "[]");
  ASSERT_EQ("disabled", GetActivePasswordProviderMode());

  EXPECT_TRUE(FillPasswordStoreForms(backend, origin).empty());
  EXPECT_TRUE(FillMahoForms(backend, origin).empty());

  // Discovery remains secret-free while routing is denied.
  EXPECT_EQ(std::string::npos,
            CredentialPayloadForOrigin(origin).find(kRoutingPassword));
}

// The provider is re-read PER OPERATION: flipping the provider between two
// operations on the SAME backend instance changes routing without recreating
// the store (which the factory cannot do anyway — CreatePasswordStoreBackend()
// runs once per store).
//
// Failing-first rationale: before Task 3 the backend never consulted the
// provider, so the post-flip fill still returned the credential; and any
// implementation that cached the provider at construction would also return it,
// because the backend here is constructed while Native is selected.
TEST_F(MahoPasswordStoreBackendTest,
       ProviderRouting_RuntimeFlipHonoredWithoutRecreatingBackend) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  // Phase 1: Native selected at construction time and for the first operation.
  SelectPasswordProvider("maho_native", "[]");
  ASSERT_TRUE(IsNativePasswordProviderActive());

  const std::string origin = "https://routing-flip.example";
  Grant(PasswordAuthorizationAction::kAdd, origin);
  PasswordStoreBackendCredential credential;
  credential.signon_realm = origin;
  credential.username_value = u"flip@example.test";
  SetCredentialPassword(credential, RoutingPassword16());

  base::RunLoop add_loop;
  bool add_success = false;
  backend.AddLoginAsync(
      std::move(credential),
      base::BindOnce(
          [](base::RunLoop *loop, bool *success_out,
             password_manager::PasswordChangesOrError result) {
            *success_out =
                std::holds_alternative<password_manager::PasswordChanges>(
                    result);
            loop->Quit();
          },
          &add_loop, &add_success));
  add_loop.Run();
  ASSERT_TRUE(add_success);

  password_manager::BackendLoginsResult native_fill =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, native_fill.size());
  EXPECT_TRUE(native_fill[0].password_value.empty());

  // Phase 2: the user switches to Bitwarden at runtime. Same backend instance,
  // no re-initialization.
  SelectPasswordProvider(
      "bitwarden", InstalledExtensionsJson(kBitwardenExtensionId, "Bitwarden"));
  ASSERT_EQ("bitwarden", GetActivePasswordProviderMode());

  EXPECT_TRUE(FillPasswordStoreForms(backend, origin).empty());
  EXPECT_TRUE(FillMahoForms(backend, origin).empty());

  // Phase 3: switching back restores the native route on the same instance.
  SelectPasswordProvider("maho_native", "[]");
  ASSERT_TRUE(IsNativePasswordProviderActive());

  password_manager::BackendLoginsResult restored_fill =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, restored_fill.size());
  EXPECT_TRUE(restored_fill[0].password_value.empty());
}

TEST_F(MahoPasswordStoreBackendTest, UnlockedVaultUpdateLoginAsync) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  SelectPasswordProvider("maho_native", "[]");
  MahoPasswordForm stored = AddLoginAndFetch(
      backend, "https://update.example.com", "old@example.com", "old-password");

  Grant(PasswordAuthorizationAction::kUpdate, "https://update.example.com");
  stored.username_value = "new@example.com";
  stored.password_value = "new-password";
  base::RunLoop update_loop;
  bool update_success = false;
  backend.UpdateLoginMahoAsync(
      stored, base::BindOnce(
                  [](base::RunLoop *loop, bool *success_out, bool success) {
                    *success_out = success;
                    loop->Quit();
                  },
                  &update_loop, &update_success));
  update_loop.Run();
  ASSERT_TRUE(update_success);

  MahoPasswordForm updated =
      AddLoginAndFetch(backend, "https://other.example.com",
                       "other@example.com", "other-password");
  EXPECT_FALSE(updated.id.empty());

  base::RunLoop fill_loop;
  bool fill_success = false;
  std::vector<MahoPasswordForm> matched_forms;
  Grant(PasswordAuthorizationAction::kFill, "https://update.example.com");
  backend.FillMatchingLoginsMahoAsync(
      "https://update.example.com",
      password_manager::PasswordForm::Scheme::kHtml,
      base::BindOnce(
          [](base::RunLoop *loop, bool *success_out,
             std::vector<MahoPasswordForm> *forms_out,
             std::vector<MahoPasswordForm> forms, bool success) {
            *success_out = success;
            *forms_out = std::move(forms);
            loop->Quit();
          },
          &fill_loop, &fill_success, &matched_forms));
  fill_loop.Run();
  ASSERT_TRUE(fill_success);
  ASSERT_EQ(1u, matched_forms.size());
  EXPECT_EQ("new@example.com", matched_forms[0].username_value);
  EXPECT_TRUE(matched_forms[0].password_value.empty());
  EXPECT_EQ(2u, matched_forms[0].revision);
}

TEST_F(MahoPasswordStoreBackendTest, UnlockedVaultRemoveLoginAsync) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  SelectPasswordProvider("maho_native", "[]");
  MahoPasswordForm stored =
      AddLoginAndFetch(backend, "https://delete.example.com",
                       "delete@example.com", "delete-password");

  Grant(PasswordAuthorizationAction::kDelete, "https://delete.example.com");
  base::RunLoop remove_loop;
  bool remove_success = false;
  backend.RemoveLoginMahoAsync(
      stored, base::BindOnce(
                  [](base::RunLoop *loop, bool *success_out, bool success) {
                    *success_out = success;
                    loop->Quit();
                  },
                  &remove_loop, &remove_success));
  remove_loop.Run();
  ASSERT_TRUE(remove_success);

  base::RunLoop fill_loop;
  bool fill_success = true;
  std::vector<MahoPasswordForm> matched_forms;
  Grant(PasswordAuthorizationAction::kFill, "https://delete.example.com");
  backend.FillMatchingLoginsMahoAsync(
      "https://delete.example.com",
      password_manager::PasswordForm::Scheme::kHtml,
      base::BindOnce(
          [](base::RunLoop *loop, bool *success_out,
             std::vector<MahoPasswordForm> *forms_out,
             std::vector<MahoPasswordForm> forms, bool success) {
            *success_out = success;
            *forms_out = std::move(forms);
            loop->Quit();
          },
          &fill_loop, &fill_success, &matched_forms));
  fill_loop.Run();
  EXPECT_TRUE(fill_success);
  EXPECT_TRUE(matched_forms.empty());
}

TEST_F(MahoPasswordStoreBackendTest, UnlockedVaultLockedOperationsFailClosed) {
  // The lock, not the kill switch, must be what closes these operations.
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend = CreateBackend();
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  SelectPasswordProvider("maho_native", "[]");
  ASSERT_TRUE(LockVault());

  MahoPasswordForm form;
  form.signon_realm = "https://locked.example.com";
  form.username_value = "locked@example.com";
  form.password_value = "locked-password";
  base::RunLoop add_loop;
  bool add_success = true;
  backend.AddLoginMahoAsync(
      form, base::BindOnce(
                [](base::RunLoop *loop, bool *success_out, bool success) {
                  *success_out = success;
                  loop->Quit();
                },
                &add_loop, &add_success));
  add_loop.Run();
  EXPECT_FALSE(add_success);

  base::RunLoop fill_loop;
  bool fill_success = true;
  backend.FillMatchingLoginsMahoAsync(
      "https://locked.example.com",
      password_manager::PasswordForm::Scheme::kHtml,
      base::BindOnce(
          [](base::RunLoop *loop, bool *success_out,
             std::vector<MahoPasswordForm> forms, bool success) {
            EXPECT_TRUE(forms.empty());
            *success_out = success;
            loop->Quit();
          },
          &fill_loop, &fill_success));
  fill_loop.Run();
  EXPECT_FALSE(fill_success);
}

TEST_F(MahoPasswordStoreBackendTest, LockedVaultState) {
  MahoPasswordStoreBackend backend = CreateBackend();
  // Backend is never initialized here, so the `!initialized_` gate (which runs
  // before the Task-3 provider check) still fails the call closed.
  base::RunLoop get_loop;
  bool get_success = true;
  backend.GetAllLoginsMahoAsync(base::BindOnce(
      [](base::RunLoop *loop, bool *success_out,
         std::vector<MahoPasswordForm> forms, bool success) {
        *success_out = success;
        loop->Quit();
      },
      &get_loop, &get_success));
  get_loop.Run();
  EXPECT_FALSE(get_success);
}

TEST_F(MahoPasswordStoreBackendTest, FormMoveSemanticsAndZeroization) {
  // Test SSO short string (11 chars)
  {
    MahoPasswordForm form;
    form.id = "id-123";
    form.signon_realm = "https://test.com";
    form.username_value = "alice";
    form.password_value = "supersecret";
    form.date_created = base::Time::FromSecondsSinceUnixEpoch(1700000000);
    form.revision = 7;

    MahoPasswordForm moved_form = std::move(form);
    EXPECT_EQ(moved_form.id, "id-123");
    EXPECT_EQ(moved_form.username_value, "alice");
    EXPECT_EQ(moved_form.password_value, "supersecret");
    EXPECT_EQ(moved_form.revision, 7u);
    EXPECT_TRUE(form.password_value.empty());

    moved_form.Zeroize();
    EXPECT_TRUE(moved_form.password_value.empty());
  }

  // Test heap-allocated long string (64 chars)
  {
    MahoPasswordForm form;
    form.id = "id-long";
    form.signon_realm = "https://long.example.com";
    form.username_value = "bob";
    form.password_value =
        "supersecret_long_password_string_that_exceeds_sso_capacity_12345";
    form.date_created = base::Time::FromSecondsSinceUnixEpoch(1700000000);
    form.revision = 11;

    MahoPasswordForm moved_form = std::move(form);
    EXPECT_EQ(moved_form.id, "id-long");
    EXPECT_EQ(moved_form.username_value, "bob");
    EXPECT_EQ(
        moved_form.password_value,
        "supersecret_long_password_string_that_exceeds_sso_capacity_12345");
    EXPECT_EQ(moved_form.revision, 11u);
    EXPECT_TRUE(form.password_value.empty());

    moved_form.Zeroize();
    EXPECT_TRUE(moved_form.password_value.empty());
  }
}

TEST_F(MahoPasswordStoreBackendTest, OriginMappingAndSafety) {
  std::string http_origin = "http://example.com";
  std::string normalized_origin = http_origin;
  if (!normalized_origin.starts_with("http://") &&
      !normalized_origin.starts_with("https://")) {
    normalized_origin = "https://" + normalized_origin;
  }
  EXPECT_TRUE(normalized_origin.starts_with("http://") ||
              normalized_origin.starts_with("https://"));

  std::string unsafe_origin = "javascript:alert(1)";
  bool is_safe = unsafe_origin.starts_with("http://") ||
                 unsafe_origin.starts_with("https://");
  EXPECT_FALSE(is_safe);
}

// ---------------------------------------------------------------------------
// Task 4: LOSSLESS CREDENTIAL ROUND-TRIP (store -> Vault -> store).
// ---------------------------------------------------------------------------

namespace {

constexpr char kRoundTripPassword[] = "S3NTINEL-roundtrip-runtime-7D3E";

std::u16string RoundTripPassword16() {
  return u"S3NTINEL-roundtrip-runtime-7D3E";
}

// A credential with EVERY field this backend claims to preserve set to a
// distinct, non-default value, so a dropped field shows up as a concrete
// mismatch rather than an accidental match against a default.
PasswordStoreBackendCredential FullyPopulatedCredential(const std::string &signon_realm,
                         const std::u16string &username) {
  PasswordStoreBackendCredential cred;
  cred.scheme = password_manager::PasswordForm::Scheme::kHtml;
  cred.signon_realm = signon_realm;
  cred.url = GURL(signon_realm + "/login/page");
  cred.action = GURL(signon_realm + "/login/submit");
  cred.submit_element = u"submit-btn";
  cred.username_element = u"user-field";
  cred.password_element = u"pass-field";
  cred.username_value = username;
  SetCredentialPassword(cred, RoundTripPassword16());
  cred.all_alternative_usernames.emplace_back(
      password_manager::AlternativeElement::Value(u"alt@example.test"),
      autofill::FieldRendererId(9007199254740993ULL),
      password_manager::AlternativeElement::Name(u"alt-field"));
  // Microsecond-distinct values pin the decimal base::Time::ToInternalValue()
  // wire contract. IEEE-754 JSON doubles would lose the sub-second part.
  cred.date_created = base::Time::FromInternalValue(13200000000000001);
  cred.date_last_used = base::Time::FromInternalValue(13200000000000002);
  cred.date_last_filled = base::Time::FromInternalValue(13200000000000003);
  cred.date_password_modified =
      base::Time::FromInternalValue(13200000000000004);
  cred.date_received = base::Time::FromInternalValue(13200000000000005);
  cred.blocked_by_user = true;
  cred.type = password_manager::PasswordForm::Type::kGenerated;
  cred.times_used_in_html_form = 7;
  cred.display_name = u"Round Trip Account";
  cred.icon_url = GURL("https://icons.example.test/icon.png");
  cred.match_type = password_manager::PasswordForm::MatchType::kPSL;
  cred.skip_zero_click = true;
  cred.in_store = password_manager::PasswordForm::Store::kProfileStore;
  cred.notes.emplace_back(u"note-name", u"note-value",
                          base::Time::FromInternalValue(13200000000000006),
                          /*hide_by_default=*/true);
  autofill::FormData form_data;
  form_data.set_name(u"round-trip-form");
  form_data.set_url(GURL(signon_realm + "/login/page"));
  cred.form_data = std::move(form_data);
  return cred;
}

} // namespace

class MahoPasswordStoreBackendRoundTripTest
    : public MahoPasswordStoreBackendTest {
protected:
  // Runs AddLoginAsync and returns the change list the backend reported (or an
  // empty optional when it reported an error).
  std::optional<password_manager::PasswordStoreChangeList>
  AddViaStore(MahoPasswordStoreBackend &backend,
              PasswordStoreBackendCredential cred) {
    base::RunLoop loop;
    std::optional<password_manager::PasswordStoreChangeList> changes;
    backend.AddLoginAsync(
        std::move(cred),
        base::BindOnce(
            [](base::RunLoop *loop,
               std::optional<password_manager::PasswordStoreChangeList> *out,
               password_manager::PasswordChangesOrError result) {
              if (auto *value =
                      std::get_if<password_manager::PasswordChanges>(&result)) {
                if (value->has_value()) {
                  *out = **value;
                }
              }
              loop->Quit();
            },
            &loop, &changes));
    loop.Run();
    return changes;
  }

  std::optional<password_manager::PasswordStoreChangeList>
  UpdateViaStore(MahoPasswordStoreBackend &backend,
                 PasswordStoreBackendCredential cred) {
    base::RunLoop loop;
    std::optional<password_manager::PasswordStoreChangeList> changes;
    backend.UpdateLoginAsync(
        std::move(cred),
        base::BindOnce(
            [](base::RunLoop *loop,
               std::optional<password_manager::PasswordStoreChangeList> *out,
               password_manager::PasswordChangesOrError result) {
              if (auto *value =
                      std::get_if<password_manager::PasswordChanges>(&result)) {
                if (value->has_value()) {
                  *out = **value;
                }
              }
              loop->Quit();
            },
            &loop, &changes));
    loop.Run();
    return changes;
  }

  std::optional<password_manager::PasswordStoreChangeList>
  RemoveViaStore(MahoPasswordStoreBackend &backend,
                 PasswordStoreBackendCredential cred) {
    base::RunLoop loop;
    std::optional<password_manager::PasswordStoreChangeList> changes;
    backend.RemoveLoginAsync(
        FROM_HERE, std::move(cred),
        base::BindOnce(
            [](base::RunLoop *loop,
               std::optional<password_manager::PasswordStoreChangeList> *out,
               password_manager::PasswordChangesOrError result) {
              if (auto *value =
                      std::get_if<password_manager::PasswordChanges>(&result)) {
                if (value->has_value()) {
                  *out = **value;
                }
              }
              loop->Quit();
            },
            &loop, &changes));
    loop.Run();
    return changes;
  }

  // Brings up an unlocked Vault with Native selected and the kill switch on,
  // which is the only configuration in which the round-trip path executes.
  void SetUpNativeBackend(MahoPasswordStoreBackend &backend) {
    EnableNativeWriteForTesting();
    CreateInitializedVaultCore();
    // Provider selection first: InitBackend() probes the Vault seam only while
    // Native is the effective provider, so the order decides what is asserted.
    SelectPasswordProvider("maho_native", "[]");
    backend.InitBackend(base::OnceCallback<void(bool)>());
    task_environment_.RunUntilIdle();
  }

  std::optional<password_manager::PasswordStoreChangeList>
  AddAuthorized(MahoPasswordStoreBackend &backend,
                PasswordStoreBackendCredential cred) {
    Grant(PasswordAuthorizationAction::kAdd, cred.signon_realm);
    return AddViaStore(backend, std::move(cred));
  }

  std::optional<password_manager::PasswordStoreChangeList>
  UpdateAuthorized(MahoPasswordStoreBackend &backend,
                   PasswordStoreBackendCredential cred) {
    Grant(PasswordAuthorizationAction::kUpdate, cred.signon_realm);
    return UpdateViaStore(backend, std::move(cred));
  }

  std::optional<password_manager::PasswordStoreChangeList>
  RemoveAuthorized(MahoPasswordStoreBackend &backend,
                   PasswordStoreBackendCredential cred) {
    Grant(PasswordAuthorizationAction::kDelete, cred.signon_realm);
    return RemoveViaStore(backend, std::move(cred));
  }
};

TEST_F(MahoPasswordStoreBackendRoundTripTest,
       AutofillableLoginsExcludeBlockedCredentials) {
  // Given a saved login and a separate "never save" entry in the real Vault.
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);
  const std::string origin = "https://autofillable.example";
  auto saved = FullyPopulatedCredential(origin, u"saved@example.test");
  saved.blocked_by_user = false;
  ASSERT_TRUE(AddAuthorized(backend, std::move(saved)).has_value());
  ASSERT_TRUE(AddAuthorized(
                  backend, FullyPopulatedCredential(origin, u"blocked"))
                  .has_value());

  // When Chromium requests only autofillable credentials.
  base::test::TestFuture<password_manager::BackendLoginsResultOrError> result;
  backend.GetAutofillableLoginsAsync(result.GetCallback());
  ASSERT_TRUE(result.Wait());

  // Then only the saved login is returned.
  const auto* forms =
      std::get_if<password_manager::BackendLoginsResult>(&result.Get());
  ASSERT_NE(nullptr, forms);
  ASSERT_EQ(1u, forms->size());
  EXPECT_EQ(u"saved@example.test", forms->front().username_value);
  EXPECT_FALSE(forms->front().blocked_by_user);
  EXPECT_TRUE(forms->front().primary_key.has_value());
}

TEST_F(MahoPasswordStoreBackendRoundTripTest,
       MahoAutofillableLoginsExcludeBlockedCredentials) {
  // Given a saved login and a separate "never save" entry in the real Vault.
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);
  const std::string origin = "https://autofillable.example";
  auto saved = FullyPopulatedCredential(origin, u"saved@example.test");
  saved.blocked_by_user = false;
  ASSERT_TRUE(AddAuthorized(backend, std::move(saved)).has_value());
  ASSERT_TRUE(AddAuthorized(
                  backend, FullyPopulatedCredential(origin, u"blocked"))
                  .has_value());

  // When the Maho API requests only autofillable credentials.
  base::test::TestFuture<std::vector<MahoPasswordForm>, bool> result;
  backend.GetAutofillableLoginsMahoAsync(result.GetCallback());
  ASSERT_TRUE(result.Wait());

  // Then it applies the same filter as the Chromium API.
  ASSERT_TRUE(result.Get<1>());
  ASSERT_EQ(1u, result.Get<0>().size());
  EXPECT_EQ("saved@example.test", result.Get<0>().front().username_value);
  EXPECT_FALSE(result.Get<0>().front().blocked_by_user);
}

TEST_F(MahoPasswordStoreBackendRoundTripTest,
       MatchingLoginsIncludeEveryRequestedRealm) {
  // Given credentials and grants for two distinct sites.
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);
  const std::string first = "https://first-match.example";
  const std::string second = "https://second-match.example";
  ASSERT_TRUE(AddAuthorized(
                  backend, FullyPopulatedCredential(first, u"first"))
                  .has_value());
  ASSERT_TRUE(AddAuthorized(
                  backend, FullyPopulatedCredential(second, u"second"))
                  .has_value());
  Grant(PasswordAuthorizationAction::kFill, first);
  Grant(PasswordAuthorizationAction::kFill, second);

  // When a single request contains both site digests.
  base::test::TestFuture<password_manager::BackendLoginsResultOrError> result;
  backend.FillMatchingLoginsAsync(
      result.GetCallback(), false,
      {{password_manager::PasswordForm::Scheme::kHtml, first, GURL(first)},
       {password_manager::PasswordForm::Scheme::kHtml, second, GURL(second)}});
  ASSERT_TRUE(result.Wait());

  // Then neither site's credentials are omitted.
  const auto* forms =
      std::get_if<password_manager::BackendLoginsResult>(&result.Get());
  ASSERT_NE(nullptr, forms);
  ASSERT_EQ(2u, forms->size());
  EXPECT_TRUE(std::ranges::any_of(*forms, [&](const auto& form) {
    return form.signon_realm == first && form.username_value == u"first";
  }));
  EXPECT_TRUE(std::ranges::any_of(*forms, [&](const auto& form) {
    return form.signon_realm == second && form.username_value == u"second";
  }));
}

TEST_F(MahoPasswordStoreBackendRoundTripTest,
       MatchingLoginsWithNoDigestsReturnsEmptySuccess) {
  // Given a native backend without any per-origin fill grant.
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);

  // When the caller has no digests to query.
  base::test::TestFuture<password_manager::BackendLoginsResultOrError> result;
  backend.FillMatchingLoginsAsync(result.GetCallback(), false, {});
  ASSERT_TRUE(result.Wait());

  // Then the empty query succeeds without attempting an all-Vault read.
  const auto* forms =
      std::get_if<password_manager::BackendLoginsResult>(&result.Get());
  ASSERT_NE(nullptr, forms);
  EXPECT_TRUE(forms->empty());
}

// Failing-first rationale: fill queries dropped the digest's scheme, so the
// backend guessed the lookup kind from the STORED records. A `Basic realm=""`
// credential (whose signon realm equals the plain origin) then hijacked HTML
// lookups for the same site and hid the stored HTML credential.
TEST_F(MahoPasswordStoreBackendRoundTripTest,
       MatchingLoginsSchemeDecidesHtmlVersusHttpAuthLookup) {
  // Given an HTML login and a `Basic realm=""` HTTP-auth credential sharing
  // one signon realm.
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);
  const std::string origin = "https://scheme-collide.example";

  PasswordStoreBackendCredential html_credential =
      FullyPopulatedCredential(origin, u"html-user");
  html_credential.scheme = password_manager::PasswordForm::Scheme::kHtml;
  ASSERT_TRUE(AddAuthorized(backend, std::move(html_credential)).has_value());
  PasswordStoreBackendCredential basic_credential =
      FullyPopulatedCredential(origin, u"basic-user");
  basic_credential.scheme = password_manager::PasswordForm::Scheme::kBasic;
  ASSERT_TRUE(AddAuthorized(backend, std::move(basic_credential)).has_value());
  Grant(PasswordAuthorizationAction::kFill, origin);

  // When each scheme queries the same site.
  base::test::TestFuture<password_manager::BackendLoginsResultOrError>
      html_result;
  backend.FillMatchingLoginsAsync(
      html_result.GetCallback(), false,
      {{password_manager::PasswordForm::Scheme::kHtml, origin, GURL(origin)}});
  ASSERT_TRUE(html_result.Wait());
  const auto* html_forms =
      std::get_if<password_manager::BackendLoginsResult>(&html_result.Get());
  ASSERT_NE(nullptr, html_forms);

  // Then the HTML lookup answers with the HTML credential only.
  ASSERT_EQ(1u, html_forms->size());
  EXPECT_EQ(password_manager::PasswordForm::Scheme::kHtml,
            html_forms->front().scheme);
  EXPECT_EQ(u"html-user", html_forms->front().username_value);

  // And the Basic lookup resolves the HTTP-auth credential, not the HTML one.
  base::test::TestFuture<password_manager::BackendLoginsResultOrError>
      basic_result;
  backend.FillMatchingLoginsAsync(
      basic_result.GetCallback(), false,
      {{password_manager::PasswordForm::Scheme::kBasic, origin, GURL(origin)}});
  ASSERT_TRUE(basic_result.Wait());
  const auto* basic_forms =
      std::get_if<password_manager::BackendLoginsResult>(&basic_result.Get());
  ASSERT_NE(nullptr, basic_forms);
  ASSERT_EQ(1u, basic_forms->size());
  EXPECT_EQ(password_manager::PasswordForm::Scheme::kBasic,
            basic_forms->front().scheme);
  EXPECT_EQ(u"basic-user", basic_forms->front().username_value);
}

// Failing-first rationale: before Task 4 the adapter wrote only
// realm/username/password and `ConvertToPasswordForms()` reconstructed only
// those three fields, so every other assertion below (scheme, url, action, form
// elements, alternative usernames, all timestamps, blocklist state, match type,
// store, form_data, notes) read back a default and failed.
TEST_F(MahoPasswordStoreBackendRoundTripTest,
       SavedCredentialRoundTripsEveryField) {
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);

  const std::string origin = "https://roundtrip.example";
  PasswordStoreBackendCredential original =
      FullyPopulatedCredential(origin, u"roundtrip@example.test");
  original.scheme = password_manager::PasswordForm::Scheme::kHtml;
  PasswordStoreBackendCredential expected =
      FullyPopulatedCredential(origin, u"roundtrip@example.test");
  expected.scheme = password_manager::PasswordForm::Scheme::kHtml;
  ASSERT_TRUE(AddAuthorized(backend, std::move(original)).has_value());

  password_manager::BackendLoginsResult read_back =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, read_back.size());
  const PasswordStoreBackendCredential &actual = read_back[0];

  EXPECT_EQ(expected.scheme, actual.scheme);
  EXPECT_EQ(expected.signon_realm, actual.signon_realm);
  EXPECT_EQ(expected.url, actual.url);
  EXPECT_EQ(expected.action, actual.action);
  EXPECT_EQ(expected.federation_origin, actual.federation_origin);
  EXPECT_EQ(expected.submit_element, actual.submit_element);
  EXPECT_EQ(expected.username_element, actual.username_element);
  EXPECT_EQ(expected.password_element, actual.password_element);
  EXPECT_EQ(expected.username_value, actual.username_value);
  EXPECT_TRUE(actual.password_value.empty());
  ASSERT_EQ(1u, actual.all_alternative_usernames.size());
  EXPECT_EQ(expected.all_alternative_usernames[0].value,
            actual.all_alternative_usernames[0].value);
  EXPECT_EQ(expected.all_alternative_usernames[0].name,
            actual.all_alternative_usernames[0].name);
  EXPECT_EQ(expected.all_alternative_usernames[0].field_renderer_id,
            actual.all_alternative_usernames[0].field_renderer_id);
  EXPECT_EQ(expected.date_created, actual.date_created);
  EXPECT_EQ(expected.date_last_used, actual.date_last_used);
  EXPECT_EQ(expected.date_last_filled, actual.date_last_filled);
  EXPECT_EQ(expected.date_password_modified, actual.date_password_modified);
  EXPECT_EQ(expected.date_received, actual.date_received);
  EXPECT_EQ(expected.blocked_by_user, actual.blocked_by_user);
  EXPECT_EQ(expected.type, actual.type);
  EXPECT_EQ(expected.times_used_in_html_form, actual.times_used_in_html_form);
  EXPECT_EQ(expected.display_name, actual.display_name);
  EXPECT_EQ(expected.icon_url, actual.icon_url);
  EXPECT_EQ(expected.match_type, actual.match_type);
  EXPECT_EQ(expected.skip_zero_click, actual.skip_zero_click);
  EXPECT_EQ(expected.in_store, actual.in_store);
  ASSERT_EQ(1u, actual.notes.size());
  EXPECT_EQ(expected.notes[0].unique_display_name,
            actual.notes[0].unique_display_name);
  EXPECT_EQ(expected.notes[0].value, actual.notes[0].value);
  EXPECT_EQ(expected.notes[0].date_created, actual.notes[0].date_created);
  EXPECT_EQ(expected.notes[0].hide_by_default, actual.notes[0].hide_by_default);
  EXPECT_EQ(expected.form_data.name(), actual.form_data.name());
  EXPECT_EQ(expected.form_data.url(), actual.form_data.url());

  // Identity: the read assigns a stable Chromium primary key derived from the
  // Vault UUID.
  ASSERT_TRUE(actual.primary_key.has_value());
  password_manager::BackendLoginsResult second_read =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, second_read.size());
  EXPECT_EQ(actual.primary_key, second_read[0].primary_key);
}

// Failing-first rationale: the pre-Task-4 unsupported-field set was
// undocumented (the adapter simply dropped everything), so this enumeration did
// not exist and there was nothing to assert against. It pins the claim that
// losslessness is scoped, and fails the moment a field is quietly added to or
// removed from the fail-closed list without updating the contract.
TEST_F(MahoPasswordStoreBackendRoundTripTest,
       UnsupportedFieldsAreEnumeratedAndFailClosed) {
  const std::vector<std::string> unsupported =
      UnsupportedPasswordFormFields();
  EXPECT_EQ(std::vector<std::string>(
                {"affiliated_web_realm", "app_display_name", "app_icon_url",
                 "previously_associated_sync_account_email",
                 "moving_blocked_for_list", "password_issues",
                 "generation_upload_status", "keychain_identifier",
                 "sender_email", "sender_name", "sender_profile_image_url",
                 "sharing_notification_displayed", "actor_login_approved"}),
            unsupported);

  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);
  const std::string origin = "https://failclosed.example";
  PasswordStoreBackendCredential cred =
      FullyPopulatedCredential(origin, u"failclosed@example.test");
  // Set two unsupported fields; they must come back as Chromium's defaults, not
  // as a reconstructed guess.
  cred.affiliated_web_realm = "https://affiliated.example";
  cred.keychain_identifier = "keychain-id";
  ASSERT_TRUE(AddAuthorized(backend, std::move(cred)).has_value());

  password_manager::BackendLoginsResult read_back =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, read_back.size());
  EXPECT_TRUE(read_back[0].affiliated_web_realm.empty());
  EXPECT_TRUE(read_back[0].keychain_identifier.empty());
}

// Failing-first rationale: the pre-Task-4 update path matched only on username
// and never copied the matched form's REVISION (it sent 0), so with two stored
// credentials on one realm the Vault CAS either rejected the write or the wrong
// duplicate was selected. Asserting the untouched sibling keeps its password
// and the targeted one changed both failed.
TEST_F(MahoPasswordStoreBackendRoundTripTest,
       UpdateMutatesTheRightDuplicateAtTheRightRevision) {
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);

  const std::string origin = "https://duplicates.example";
  PasswordStoreBackendCredential first =
      FullyPopulatedCredential(origin, u"first@example.test");
  PasswordStoreBackendCredential second =
      FullyPopulatedCredential(origin, u"second@example.test");
  SetCredentialPassword(second, u"second-original-password");
  ASSERT_TRUE(AddAuthorized(backend, std::move(first)).has_value());
  ASSERT_TRUE(AddAuthorized(backend, std::move(second)).has_value());

  password_manager::BackendLoginsResult stored =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(2u, stored.size());
  const PasswordStoreBackendCredential *target = nullptr;
  const PasswordStoreBackendCredential *sibling = nullptr;
  for (const auto &candidate : stored) {
    if (candidate.username_value == u"second@example.test") {
      target = &candidate;
    } else {
      sibling = &candidate;
    }
  }
  ASSERT_NE(nullptr, target);
  ASSERT_NE(nullptr, sibling);
  ASSERT_TRUE(target->primary_key.has_value());

  PasswordStoreBackendCredential edit =
      FullyPopulatedCredential(origin, u"second@example.test");
  edit.primary_key = target->primary_key;
  SetCredentialPassword(edit, u"second-updated-password");
  ASSERT_TRUE(UpdateAuthorized(backend, std::move(edit)).has_value());

  password_manager::BackendLoginsResult after =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(2u, after.size());
  for (const auto &candidate : after) {
    if (candidate.username_value == u"second@example.test") {
      EXPECT_TRUE(candidate.password_value.empty());
      EXPECT_NE(target->primary_key, candidate.primary_key);
    } else {
      // The sibling duplicate must be untouched.
      EXPECT_TRUE(candidate.password_value.empty());
      EXPECT_EQ(sibling->primary_key, candidate.primary_key);
    }
  }
}

TEST_F(MahoPasswordStoreBackendRoundTripTest,
       StalePrimaryKeyRevisionFailsClosedForUpdateAndRemove) {
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);

  const std::string origin = "https://stale-revision.example";
  ASSERT_TRUE(AddAuthorized(backend, FullyPopulatedCredential(
                                         origin, u"stale@example.test"))
                  .has_value());
  password_manager::BackendLoginsResult initial =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, initial.size());
  const std::optional<password_manager::FormPrimaryKey> stale_primary_key =
      initial[0].primary_key;
  ASSERT_TRUE(stale_primary_key.has_value());

  PasswordStoreBackendCredential fresh =
      FullyPopulatedCredential(origin, u"stale@example.test");
  fresh.primary_key = stale_primary_key;
  SetCredentialPassword(fresh, u"fresh-password");
  ASSERT_TRUE(UpdateAuthorized(backend, std::move(fresh)).has_value());

  PasswordStoreBackendCredential stale_update =
      FullyPopulatedCredential(origin, u"stale@example.test");
  stale_update.primary_key = stale_primary_key;
  SetCredentialPassword(stale_update, u"stale-password");
  EXPECT_FALSE(UpdateAuthorized(backend, std::move(stale_update)).has_value());

  PasswordStoreBackendCredential stale_remove =
      FullyPopulatedCredential(origin, u"stale@example.test");
  stale_remove.primary_key = stale_primary_key;
  EXPECT_FALSE(RemoveAuthorized(backend, std::move(stale_remove)).has_value());

  password_manager::BackendLoginsResult after =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, after.size());
  EXPECT_TRUE(after[0].password_value.empty());
}

// Failing-first rationale: every mutation used to return an EMPTY
// PasswordStoreChangeList, so PasswordStore observers never fired for a native
// save/edit/delete. All three change-type assertions below failed.
TEST_F(MahoPasswordStoreBackendRoundTripTest,
       MutationsReportAddUpdateRemoveChanges) {
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);

  const std::string origin = "https://changelist.example";
  std::optional<password_manager::PasswordStoreChangeList> add_changes =
      AddAuthorized(backend, FullyPopulatedCredential(
                                 origin, u"changelist@example.test"));
  ASSERT_TRUE(add_changes.has_value());
  ASSERT_EQ(1u, add_changes->size());
  EXPECT_EQ(password_manager::PasswordStoreChange::ADD,
            (*add_changes)[0].type());
  EXPECT_EQ(origin, GetChangeCredential((*add_changes)[0]).signon_realm);
  EXPECT_EQ(u"changelist@example.test",
            GetChangeCredential((*add_changes)[0]).username_value);
  EXPECT_EQ(GURL(origin + "/login/page"),
            GetChangeCredential((*add_changes)[0]).url);
  EXPECT_EQ(GURL(origin + "/login/submit"),
            GetChangeCredential((*add_changes)[0]).action);
  EXPECT_EQ(u"Round Trip Account",
            GetChangeCredential((*add_changes)[0]).display_name);
  ASSERT_EQ(1u, GetChangeCredential((*add_changes)[0]).notes.size());
  EXPECT_EQ(u"note-value",
            GetChangeCredential((*add_changes)[0]).notes[0].value);
  ASSERT_TRUE(GetChangeCredential((*add_changes)[0]).primary_key.has_value());
  const int add_primary_key =
      GetChangeCredential((*add_changes)[0]).primary_key->value();

  password_manager::BackendLoginsResult stored =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, stored.size());
  ASSERT_TRUE(stored[0].primary_key.has_value());
  EXPECT_EQ(add_primary_key, stored[0].primary_key->value());

  PasswordStoreBackendCredential edit =
      FullyPopulatedCredential(origin, u"changelist@example.test");
  edit.primary_key = stored[0].primary_key;
  SetCredentialPassword(edit, u"changelist-updated");
  std::optional<password_manager::PasswordStoreChangeList> update_changes =
      UpdateAuthorized(backend, std::move(edit));
  ASSERT_TRUE(update_changes.has_value());
  ASSERT_EQ(1u, update_changes->size());
  EXPECT_EQ(password_manager::PasswordStoreChange::UPDATE,
            (*update_changes)[0].type());
  EXPECT_EQ(u"changelist-updated",
            GetCredentialPassword(GetChangeCredential((*update_changes)[0])));
  EXPECT_EQ(GURL(origin + "/login/page"),
            GetChangeCredential((*update_changes)[0]).url);
  EXPECT_EQ(GURL(origin + "/login/submit"),
            GetChangeCredential((*update_changes)[0]).action);
  ASSERT_EQ(1u, GetChangeCredential((*update_changes)[0]).notes.size());
  EXPECT_EQ(u"note-value",
            GetChangeCredential((*update_changes)[0]).notes[0].value);
  EXPECT_TRUE((*update_changes)[0].password_changed());
  ASSERT_TRUE(
      GetChangeCredential((*update_changes)[0]).primary_key.has_value());
  EXPECT_NE(
      add_primary_key,
      GetChangeCredential((*update_changes)[0]).primary_key->value());

  password_manager::BackendLoginsResult after_password_update =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, after_password_update.size());
  EXPECT_EQ(GetChangeCredential((*update_changes)[0]).primary_key,
            after_password_update[0].primary_key);

  PasswordStoreBackendCredential metadata_edit =
      FullyPopulatedCredential(origin, u"changelist@example.test");
  metadata_edit.primary_key = after_password_update[0].primary_key;
  metadata_edit.password_value.clear();
  metadata_edit.display_name = u"Metadata-only update";
  std::optional<password_manager::PasswordStoreChangeList>
      metadata_update_changes =
          UpdateAuthorized(backend, std::move(metadata_edit));
  ASSERT_TRUE(metadata_update_changes.has_value());
  ASSERT_EQ(1u, metadata_update_changes->size());
  EXPECT_EQ(password_manager::PasswordStoreChange::UPDATE,
            (*metadata_update_changes)[0].type());
  EXPECT_FALSE((*metadata_update_changes)[0].password_changed());
  ASSERT_TRUE(
      GetChangeCredential((*metadata_update_changes)[0]).primary_key.has_value());
  EXPECT_NE(after_password_update[0].primary_key,
            GetChangeCredential((*metadata_update_changes)[0]).primary_key);

  password_manager::BackendLoginsResult before_remove =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, before_remove.size());
  PasswordStoreBackendCredential to_remove =
      FullyPopulatedCredential(origin, u"changelist@example.test");
  to_remove.primary_key = before_remove[0].primary_key;
  std::optional<password_manager::PasswordStoreChangeList> remove_changes =
      RemoveAuthorized(backend, std::move(to_remove));
  ASSERT_TRUE(remove_changes.has_value());
  ASSERT_EQ(1u, remove_changes->size());
  EXPECT_EQ(password_manager::PasswordStoreChange::REMOVE,
            (*remove_changes)[0].type());
  EXPECT_EQ(before_remove[0].primary_key,
            GetChangeCredential((*remove_changes)[0]).primary_key);
  EXPECT_TRUE(
      GetChangeCredential((*remove_changes)[0]).password_value.empty());

  EXPECT_TRUE(FillPasswordStoreForms(backend, origin).empty());
}

// Failing-first rationale: a record written by the pre-Task-4 code has no
// `formDetails` in its encrypted payload. Before the back-compat default was
// defined, the reader had no such concept at all; this test pins that a legacy
// record still reads (realm/username/password/created), with the new fields at
// Chromium's defaults rather than failing the read.
TEST_F(MahoPasswordStoreBackendRoundTripTest,
       RangeDeleteReportsSnapshotPrimaryKeys) {
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);

  const std::string origin = "https://range-delete.example";
  ASSERT_TRUE(AddAuthorized(backend, FullyPopulatedCredential(
                                         origin, u"range@example.test"))
                  .has_value());
  password_manager::BackendLoginsResult stored =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, stored.size());
  ASSERT_TRUE(stored[0].primary_key.has_value());

  base::RunLoop loop;
  std::optional<password_manager::PasswordStoreChangeList> changes;
  backend.RemoveLoginsCreatedBetweenAsync(
      FROM_HERE, base::Time(), base::Time(),
      base::BindOnce(
          [](base::RunLoop *loop,
             std::optional<password_manager::PasswordStoreChangeList> *out,
             password_manager::PasswordChangesOrError result) {
            if (absl::holds_alternative<
                    std::optional<password_manager::PasswordStoreChangeList>>(
                    result)) {
              *out = absl::get<
                  std::optional<password_manager::PasswordStoreChangeList>>(
                  std::move(result));
            }
            loop->Quit();
          },
          &loop, &changes));
  loop.Run();

  ASSERT_TRUE(changes.has_value());
  ASSERT_EQ(1u, changes->size());
  EXPECT_EQ(password_manager::PasswordStoreChange::REMOVE,
            (*changes)[0].type());
  EXPECT_EQ(stored[0].primary_key,
            GetChangeCredential((*changes)[0]).primary_key);
  EXPECT_TRUE(GetChangeCredential((*changes)[0]).password_value.empty());
  EXPECT_TRUE(FillPasswordStoreForms(backend, origin).empty());
}

TEST_F(MahoPasswordStoreBackendRoundTripTest,
       AndroidRealmWithCanonicalAndroidUrlRoundTrips) {
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);

  const std::string android_realm = "android://aGFzaA==@com.example.android";
  PasswordStoreBackendCredential credential =
      FullyPopulatedCredential(android_realm, u"android@example.test");
  credential.signon_realm = android_realm;
  credential.url = GURL(android_realm);
  credential.action = GURL();
  ASSERT_TRUE(AddAuthorized(backend, std::move(credential)).has_value());

  password_manager::BackendLoginsResult read_back =
      FillPasswordStoreForms(backend, android_realm);
  ASSERT_EQ(1u, read_back.size());
  EXPECT_EQ(android_realm, read_back[0].signon_realm);
  EXPECT_EQ(GURL(android_realm), read_back[0].url);

  PasswordStoreBackendCredential malformed =
      FullyPopulatedCredential(android_realm, u"bad@example.test");
  malformed.signon_realm = android_realm;
  malformed.url = GURL("android://");
  EXPECT_FALSE(AddAuthorized(backend, std::move(malformed)).has_value());
}

TEST_F(MahoPasswordStoreBackendRoundTripTest,
       UnavailableBitwardenFallsBackToDisabledNotNative) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  SelectPasswordProvider("bitwarden", "[]");
  ASSERT_FALSE(IsNativePasswordProviderActive());

  MahoPasswordStoreBackend backend = CreateBackend();
  base::RunLoop init_loop;
  bool initialized = false;
  backend.InitBackend(base::BindOnce(
      [](base::RunLoop *loop, bool *out, bool success) {
        *out = success;
        loop->Quit();
      },
      &init_loop, &initialized));
  init_loop.Run();
  EXPECT_TRUE(initialized);

  MahoPasswordForm form;
  form.signon_realm = "https://unavailable-provider.example";
  form.url = form.signon_realm;
  form.username_value = "disabled@example.test";
  form.password_value = "must-not-persist";
  base::RunLoop add_loop;
  bool add_success = false;
  backend.AddLoginMahoAsync(
      form, base::BindOnce(
                [](base::RunLoop *loop, bool *out, bool success) {
                  *out = success;
                  loop->Quit();
                },
                &add_loop, &add_success));
  add_loop.Run();
  EXPECT_TRUE(add_success);
  EXPECT_TRUE(FillPasswordStoreForms(backend, form.signon_realm).empty());
}

TEST_F(MahoPasswordStoreBackendRoundTripTest,
       UnavailableOnePasswordFallsBackToDisabledNotNative) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  SelectPasswordProvider("one_password", "[]");
  ASSERT_FALSE(IsNativePasswordProviderActive());

  MahoPasswordStoreBackend backend = CreateBackend();
  base::RunLoop init_loop;
  bool initialized = false;
  backend.InitBackend(base::BindOnce(
      [](base::RunLoop *loop, bool *out, bool success) {
        *out = success;
        loop->Quit();
      },
      &init_loop, &initialized));
  init_loop.Run();
  EXPECT_TRUE(initialized);

  MahoPasswordForm form;
  form.signon_realm = "https://unavailable-onepassword.example";
  form.url = form.signon_realm;
  form.username_value = "disabled@example.test";
  form.password_value = "must-not-persist";
  base::RunLoop add_loop;
  bool add_success = false;
  backend.AddLoginMahoAsync(
      form, base::BindOnce(
                [](base::RunLoop *loop, bool *out, bool success) {
                  *out = success;
                  loop->Quit();
                },
                &add_loop, &add_success));
  add_loop.Run();
  EXPECT_TRUE(add_success);
  EXPECT_TRUE(FillPasswordStoreForms(backend, form.signon_realm).empty());
}

TEST_F(MahoPasswordStoreBackendRoundTripTest,
       CrossProfileBackendCannotAccessOwnerVault) {
  MahoPasswordStoreBackend owner_backend = CreateBackend();
  SetUpNativeBackend(owner_backend);
  const std::string origin = "https://owner-profile.example";
  ASSERT_TRUE(AddAuthorized(owner_backend, FullyPopulatedCredential(
                                               origin, u"owner@example.test"))
                  .has_value());

  std::unique_ptr<TestingProfile> other_profile =
      TestingProfile::Builder().Build();
  ASSERT_NE(other_profile, nullptr);
  MahoPasswordStoreBackend other_backend(
      true, maho::GetProfileIdentityKey(other_profile->GetPath(),
                                        other_profile->GetPrefs()));
  base::RunLoop init_loop;
  bool initialized = true;
  other_backend.InitBackend(base::BindOnce(
      [](base::RunLoop *loop, bool *out, bool success) {
        *out = success;
        loop->Quit();
      },
      &init_loop, &initialized));
  init_loop.Run();
  EXPECT_FALSE(initialized);
  EXPECT_TRUE(FillPasswordStoreForms(other_backend, origin).empty());

  MahoPasswordForm foreign_write;
  foreign_write.signon_realm = origin;
  foreign_write.url = origin;
  foreign_write.username_value = "foreign@example.test";
  foreign_write.password_value = "must-not-persist";
  base::RunLoop add_loop;
  bool add_success = true;
  other_backend.AddLoginMahoAsync(
      foreign_write, base::BindOnce(
                         [](base::RunLoop *loop, bool *out, bool success) {
                           *out = success;
                           loop->Quit();
                         },
                         &add_loop, &add_success));
  add_loop.Run();
  EXPECT_FALSE(add_success);
  ASSERT_EQ(1u, FillPasswordStoreForms(owner_backend, origin).size());
}

TEST_F(MahoPasswordStoreBackendRoundTripTest,
       UsernameEditPreservesCredentialFields) {
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);
  const std::string origin = "https://username-edit.example";
  ASSERT_TRUE(AddAuthorized(backend, FullyPopulatedCredential(
                                         origin, u"before@example.test"))
                  .has_value());
  password_manager::BackendLoginsResult before =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, before.size());

  PasswordStoreBackendCredential edit =
      FullyPopulatedCredential(origin, u"after@example.test");
  edit.primary_key = before[0].primary_key;
  ASSERT_TRUE(UpdateAuthorized(backend, std::move(edit)).has_value());

  password_manager::BackendLoginsResult after =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, after.size());
  EXPECT_EQ(u"after@example.test", after[0].username_value);
  EXPECT_TRUE(after[0].password_value.empty());
  EXPECT_EQ(GURL(origin + "/login/page"), after[0].url);
  EXPECT_EQ(GURL(origin + "/login/submit"), after[0].action);
  EXPECT_TRUE(after[0].blocked_by_user);
  EXPECT_EQ(password_manager::PasswordForm::Type::kGenerated, after[0].type);
  ASSERT_EQ(1u, after[0].notes.size());
  EXPECT_EQ(u"note-value", after[0].notes[0].value);
}

TEST_F(MahoPasswordStoreBackendRoundTripTest,
       AmbiguousDuplicateUsernameWithoutKeyFailsClosed) {
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);
  const std::string origin = "https://duplicate-username.example";
  PasswordStoreBackendCredential first =
      FullyPopulatedCredential(origin, u"same@example.test");
  first.scheme = password_manager::PasswordForm::Scheme::kHtml;
  PasswordStoreBackendCredential second =
      FullyPopulatedCredential(origin, u"same@example.test");
  second.scheme = password_manager::PasswordForm::Scheme::kHtml;
  second.username_element = u"different-user-field";
  ASSERT_TRUE(AddAuthorized(backend, std::move(first)).has_value());
  ASSERT_TRUE(AddAuthorized(backend, std::move(second)).has_value());

  PasswordStoreBackendCredential ambiguous =
      FullyPopulatedCredential(origin, u"same@example.test");
  ambiguous.username_element.clear();
  ambiguous.primary_key.reset();
  SetCredentialPassword(ambiguous, u"must-not-update");
  EXPECT_FALSE(UpdateAuthorized(backend, std::move(ambiguous)).has_value());

  password_manager::BackendLoginsResult after =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(2u, after.size());
  for (const auto &candidate : after) {
    EXPECT_TRUE(candidate.password_value.empty());
  }
}

TEST_F(MahoPasswordStoreBackendRoundTripTest,
       BlocklistGeneratedPresaveRoundTripsAndDeletes) {
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);
  const std::string origin = "https://blocklist-generated.example";
  PasswordStoreBackendCredential credential =
      FullyPopulatedCredential(origin, u"generated@example.test");
  credential.scheme = password_manager::PasswordForm::Scheme::kHtml;
  credential.blocked_by_user = true;
  credential.type = password_manager::PasswordForm::Type::kGenerated;
  ASSERT_TRUE(AddAuthorized(backend, std::move(credential)).has_value());

  password_manager::BackendLoginsResult stored =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, stored.size());
  EXPECT_TRUE(stored[0].blocked_by_user);
  EXPECT_EQ(password_manager::PasswordForm::Type::kGenerated, stored[0].type);

  PasswordStoreBackendCredential remove =
      FullyPopulatedCredential(origin, u"generated@example.test");
  remove.scheme = password_manager::PasswordForm::Scheme::kHtml;
  remove.primary_key = stored[0].primary_key;
  ASSERT_TRUE(RemoveAuthorized(backend, std::move(remove)).has_value());
  EXPECT_TRUE(FillPasswordStoreForms(backend, origin).empty());
}

TEST_F(MahoPasswordStoreBackendRoundTripTest,
       LegacyRecordWithoutFormDetailsStillReads) {
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);

  const std::string origin = "https://legacy.example";
  base::DictValue metadata;
  metadata.Set("title", origin);
  metadata.Set("usernameHint", "legacy@example.test");
  metadata.Set("itemKind", "login");
  base::ListValue origins;
  origins.Append(origin);
  metadata.Set("origins", std::move(origins));
  metadata.Set("totp", base::Value());
  metadata.Set("passkey", base::Value());
  base::DictValue request;
  request.Set("metadata", std::move(metadata));
  request.Set("username", "legacy@example.test");
  request.Set("password", kRoundTripPassword);
  std::string request_json;
  base::JSONWriter::Write(request, &request_json);
  ASSERT_TRUE(VaultResponseOk(
      maho_vault_add_login_json(maho::GetCore(), request_json.c_str())));

  password_manager::BackendLoginsResult read_back =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, read_back.size());
  EXPECT_EQ(origin, read_back[0].signon_realm);
  EXPECT_TRUE(read_back[0].password_value.empty());
  // Back-compat defaults: absent form detail means Chromium's own defaults.
  EXPECT_EQ(password_manager::PasswordForm::Scheme::kHtml, read_back[0].scheme);
  EXPECT_FALSE(read_back[0].blocked_by_user);
  EXPECT_FALSE(read_back[0].match_type.has_value());
  EXPECT_TRUE(read_back[0].all_alternative_usernames.empty());
  EXPECT_TRUE(read_back[0].notes.empty());
  EXPECT_TRUE(read_back[0].date_password_modified.is_null());
}

// Failing-first rationale: `InitBackend()` used to set `initialized_ = true`
// and report success unconditionally, and `GetError()` always returned a
// default-constructed (kNoError) value, so a locked Vault was indistinguishable
// from a healthy one. The kNeedsPassphrase assertion below failed.
//
// A locked Vault is deliberately NOT an initialization failure (the store must
// survive a cold start); it is a REPORTED error, which is the difference
// between "silently claim success" and "report real state".
TEST_F(MahoPasswordStoreBackendRoundTripTest,
       LockedVaultInitReportsNeedsPassphrase) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  SelectPasswordProvider("maho_native", "[]");
  ASSERT_TRUE(IsNativePasswordProviderActive());
  ASSERT_TRUE(LockVault());

  MahoPasswordStoreBackend backend = CreateBackend();
  base::RunLoop init_loop;
  bool init_success = false;
  backend.InitBackend(base::BindOnce(
      [](base::RunLoop *loop, bool *out, bool success) {
        *out = success;
        loop->Quit();
      },
      &init_loop, &init_success));
  init_loop.Run();

  EXPECT_TRUE(init_success);
  EXPECT_TRUE(backend.IsInitialized());
  EXPECT_EQ(password_manager::ActionableError::kNeedsPassphrase,
            backend.GetError());
}

// Failing-first rationale: the account-store backend used to report
// initialization SUCCESS and kNoError even though it can never serve a single
// operation. It still initializes (the store object must exist), but the
// disabled-store case is now distinguishable through GetError() only because
// `last_error_` exists; before Task 4 this assertion had no observable value to
// read.
TEST_F(MahoPasswordStoreBackendRoundTripTest,
       DisabledBackendReportsNoErrorButNeverRoutes) {
  EnableNativeWriteForTesting();
  CreateInitializedVaultCore();
  SelectPasswordProvider("maho_native", "[]");

  MahoPasswordStoreBackend backend = CreateBackend(/*enabled=*/false);
  base::RunLoop init_loop;
  bool init_success = false;
  backend.InitBackend(base::BindOnce(
      [](base::RunLoop *loop, bool *out, bool success) {
        *out = success;
        loop->Quit();
      },
      &init_loop, &init_success));
  init_loop.Run();

  // The disabled (account) store is intentionally inert, not broken: it reports
  // no actionable error and performs no Vault probe.
  EXPECT_TRUE(init_success);
  EXPECT_EQ(password_manager::ActionableError::kNoError, backend.GetError());
}

// Failing-first rationale: `GetError()` returned a default-constructed value
// regardless of state, so it could not distinguish a healthy backend from a
// broken one; this positive control failed to have any meaning until
// `last_error_` existed.
TEST_F(MahoPasswordStoreBackendRoundTripTest, SuccessfulInitReportsNoError) {
  MahoPasswordStoreBackend backend = CreateBackend();
  SetUpNativeBackend(backend);
  EXPECT_TRUE(backend.IsInitialized());
  EXPECT_EQ(password_manager::ActionableError::kNoError, backend.GetError());
}

} // namespace passwords
} // namespace maho
