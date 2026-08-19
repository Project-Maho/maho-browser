#include "chrome/browser/ui/browser_window.h"
// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/password_manager/factories/account_password_store_factory.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/password_manager/password_manager_test_base.h"
#include "chrome/browser/password_manager/passwords_navigation_observer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/keyed_service/core/refcounted_keyed_service.h"
#include "components/device_reauth/mock_device_authenticator.h"
#include "components/password_manager/content/browser/content_password_manager_driver.h"
#include "components/password_manager/core/browser/password_form_digest.h"
#include "components/password_manager/core/browser/password_form_manager.h"
#include "components/password_manager/core/browser/password_store/password_form_converters.h"
#include "components/password_manager/core/browser/password_store/password_store.h"
#include "components/password_manager/core/browser/password_store/password_store_backend.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_store/stored_credential.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/passwords/maho_password_authorization_service.h"
#include "maho/browser/passwords/maho_password_provider_utils.h"
#include "maho/browser/passwords/maho_password_store_backend.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace maho {
namespace passwords {

namespace {

constexpr char kMasterPassphrase[] = "correct horse battery staple";
constexpr char kRecoverySecret[] = "test recovery phrase";
constexpr char kUsername[] = "native-fill@example.test";
constexpr char kPassword[] = "S3NTINEL-native-browser-fill-8D3A";
constexpr char kFreshPassword[] = "S3NTINEL-current-revision-6B2F";
constexpr char kRestartStableHost[] = "restart-fill.test";
constexpr int kRestartStableHttpsPort = 49291;

std::optional<base::Value> TakeMahoJson(char *raw) {
  if (!raw) {
    return std::nullopt;
  }
  std::string json(raw);
  maho_string_free(raw);
  return base::JSONReader::Read(json, base::JSON_PARSE_RFC);
}

bool VaultOperationSucceeded(char *raw) {
  std::optional<base::Value> response = TakeMahoJson(raw);
  return response && response->is_dict() &&
         response->GetDict().FindBool("ok").value_or(false);
}

std::optional<std::string> VaultLockState(MahoCore *core) {
  std::optional<base::Value> response =
      TakeMahoJson(maho_vault_status_json(core));
  if (!response || !response->is_dict() ||
      !response->GetDict().FindBool("ok").value_or(false)) {
    return std::nullopt;
  }
  const base::DictValue *data = response->GetDict().FindDict("data");
  const std::string *lock_state =
      data ? data->FindString("lockState") : nullptr;
  return lock_state ? std::optional<std::string>(*lock_state) : std::nullopt;
}

std::string ProfileKey(Profile *profile) {
  return maho::GetProfileIdentityKey(profile->GetPath(), profile->GetPrefs());
}

scoped_refptr<RefcountedKeyedService>
BuildMahoPasswordStore(bool enabled, content::BrowserContext *context) {
  Profile *profile = Profile::FromBrowserContext(context);
  scoped_refptr<password_manager::PasswordStore> store =
      new password_manager::PasswordStore(
          std::make_unique<MahoPasswordStoreBackend>(enabled,
                                                     ProfileKey(profile)));
  store->Init(/*affiliated_match_helper=*/nullptr);
  return store;
}

struct MatchingResult {
  bool success = false;
  password_manager::BackendLoginsResult forms;
};

} // namespace

class MahoPasswordFillBrowserTest : public PasswordManagerBrowserTestBase {
public:
  MahoPasswordFillBrowserTest() {
    // Waiting for server predictions is covered by PasswordFormManager unit
    // tests. Disabling it here makes the renderer-fill transition
    // deterministic.
    password_manager::PasswordFormManager::
        set_wait_for_server_predictions_for_filling(false);
  }

  ~MahoPasswordFillBrowserTest() override = default;

protected:
  void SetUp() override {
    // PRE_ and non-PRE tests run in separate browser processes. Bind the
    // restart-pair fixture to one stable origin so the credential's
    // origin-scoped signon realm remains valid after the process restart.
    ASSERT_TRUE(https_test_server().InitializeAndListen(TestServerPort()));
    CertVerifierBrowserTest::SetUp();
  }

  void SetUpInProcessBrowserTestFixture() override {
    CertVerifierBrowserTest::SetUpInProcessBrowserTestFixture();
    create_services_subscription_ =
        BrowserContextDependencyManager::GetInstance()
            ->RegisterCreateServicesCallbackForTesting(
                base::BindRepeating([](content::BrowserContext *context) {
                  ProfilePasswordStoreFactory::GetInstance()->SetTestingFactory(
                      context,
                      base::BindRepeating(&BuildMahoPasswordStore, /*enabled=*/true));
                }));
  }

  void SetUpOnMainThread() override {
    if (!browser()) {
      Profile *p = maho::GetCoreOwnerProfile();
      if (!p && g_browser_process && g_browser_process->profile_manager()) {
        p = g_browser_process->profile_manager()->GetLastUsedProfile();
      }
      if (p) {
        Browser *b = CreateBrowser(p);
        SetBrowser(b);
      }
    }

    PasswordManagerBrowserTestBase::SetUpOnMainThread();

    core_ = maho::GetCore();
    ASSERT_NE(nullptr, core_);
    ASSERT_EQ(browser()->profile(), maho::GetCoreOwnerProfile());
    ASSERT_TRUE(EnsureVaultUnlocked());

    PrefService *local_state = g_browser_process->local_state();
    ASSERT_NE(nullptr, local_state);
    ASSERT_NE(nullptr, local_state->FindPreference(
                           prefs::kMahoNativePasswordWriteEnabled));
    local_state->SetBoolean(prefs::kMahoNativePasswordWriteEnabled, true);

    maho_core_set_installed_extensions(core_, "[]");
    maho_core_set_installed_extensions_for_profile(
        core_, ProfileKey(browser()->profile()).c_str(), "[]");
    maho_core_update_settings(
        core_,
        R"({"autofill":{"passwordsEnabled":true,"passwordProvider":"maho_native"}})");
    ASSERT_EQ(EffectivePasswordProvider::kMahoNative,
              GetEffectivePasswordProviderForProfileKey(
                  ProfileKey(browser()->profile())));

    authorization_service()->ResetForTesting();
    authorization_service()->SetVaultLockedForTesting(false);
    authorization_service()->SetInteractiveAuthorizationEnabledForTesting(false);
    maho::NotifyVaultLockStateChanged(/*locked=*/false);

    GrantReadAll(browser()->profile());
    PasswordManagerBrowserTestBase::WaitForPasswordStore(browser());
  }

  void TearDownOnMainThread() override {
    authorization_service()->ResetForTesting();
    if (g_browser_process && g_browser_process->local_state()) {
      g_browser_process->local_state()->SetBoolean(
          prefs::kMahoNativePasswordWriteEnabled, false);
    }
    core_ = nullptr;
    PasswordManagerBrowserTestBase::TearDownOnMainThread();
  }

  MahoPasswordAuthorizationService *authorization_service() const {
    return MahoPasswordAuthorizationService::Get();
  }

  bool EnsureVaultUnlocked() {
    std::optional<std::string> lock_state = VaultLockState(core_);
    if (lock_state == "unlocked") {
      return true;
    }

    // A fresh browser-test profile has an uninitialized Vault. On a persisted
    // profile initialization fails harmlessly and the unlock below handles the
    // already-initialized state.
    const std::string initialize_request =
        std::string(R"({"masterPassphrase":")") + kMasterPassphrase +
        R"(","recoverySecret":")" + kRecoverySecret + R"("})";
    VaultOperationSucceeded(
        maho_vault_initialize_json(core_, initialize_request.c_str()));
    if (VaultLockState(core_) == "unlocked") {
      return true;
    }

    return UnlockVault() && VaultLockState(core_) == "unlocked";
  }

  bool UnlockVault() {
    const std::string request =
        std::string(R"({"masterPassphrase":")") + kMasterPassphrase + R"("})";
    return VaultOperationSucceeded(
        maho_vault_unlock_json(core_, request.c_str()));
  }

  void Grant(Profile *profile, PasswordAuthorizationAction action,
             const std::string &origin_scope) {
    authorization_service()->GrantForTesting(
        ProfileKey(profile), origin_scope, action,
        base::TimeTicks::Now() +
            MahoPasswordAuthorizationService::kAuthorizationLifetime);
  }

  void GrantReadAll(Profile *profile) {
    Grant(profile, PasswordAuthorizationAction::kReadAll,
          MahoPasswordAuthorizationService::kAllOriginsScope);
  }

  GURL PasswordFormUrl(const std::string &host) {
    return https_test_server().GetURL(host, "/password/simple_password.html");
  }

  static std::string OriginFor(const GURL &url) {
    return url.DeprecatedGetOriginAsURL().spec();
  }

  MahoPasswordStoreBackend *BackendForProfile(Profile *profile) {
    scoped_refptr<password_manager::PasswordStoreInterface> store =
        ProfilePasswordStoreFactory::GetForProfile(
            profile, ServiceAccessType::IMPLICIT_ACCESS);
    EXPECT_TRUE(store);
    if (!store) {
      return nullptr;
    }
    return static_cast<MahoPasswordStoreBackend *>(
        store->GetBackendForTesting());
  }

  void NavigateToPasswordForm(Browser *target_browser, const GURL &url) {
    content::WebContents *web_contents =
        target_browser->tab_strip_model()->GetActiveWebContents();
    ASSERT_NE(nullptr, web_contents);
    PasswordsNavigationObserver navigation_observer(web_contents);
    ASSERT_TRUE(ui_test_utils::NavigateToURL(target_browser, url));
    ASSERT_TRUE(navigation_observer.Wait());
    ASSERT_TRUE(content::WaitForLoadStop(web_contents));
  }

  void ReloadPasswordForm(Browser *target_browser) {
    content::WebContents *web_contents =
        target_browser->tab_strip_model()->GetActiveWebContents();
    ASSERT_NE(nullptr, web_contents);
    PasswordsNavigationObserver navigation_observer(web_contents);
    web_contents->GetController().Reload(content::ReloadType::NORMAL,
                                         /*check_for_repost=*/false);
    ASSERT_TRUE(navigation_observer.Wait());
    ASSERT_TRUE(content::WaitForLoadStop(web_contents));
  }

  std::pair<std::string, std::string> ReadFields(
      content::RenderFrameHost *frame, const std::string &username_id,
      const std::string &password_id) {
    const std::string read_script = content::JsReplace(
        R"(
          (() => {
            const u = document.getElementById($1);
            const p = document.getElementById($2);
            return JSON.stringify([u ? u.value : "", p ? p.value : ""]);
          })()
        )",
        username_id, password_id);
    const std::string values_json =
        content::EvalJs(frame, read_script,
                        content::EXECUTE_SCRIPT_NO_USER_GESTURE)
            .ExtractString();
    std::optional<base::Value> values =
        base::JSONReader::Read(values_json, base::JSON_PARSE_RFC);
    if (!values || !values->is_list() || values->GetList().size() != 2u) {
      ADD_FAILURE() << "Password-field read returned malformed data";
      return {};
    }
    return {values->GetList()[0].GetString(), values->GetList()[1].GetString()};
  }

  void ExpectMainFrameFilled(Browser *target_browser,
                             const std::string &username,
                             const std::string &password) {
    content::WebContents *web_contents =
        target_browser->tab_strip_model()->GetActiveWebContents();
    ASSERT_NE(nullptr, web_contents);
    SetWebContents(web_contents);
    WaitForElementValue("username_field", username);
    WaitForElementValue("password_field", password);
    EXPECT_EQ(std::make_pair(username, password),
              ReadFields(web_contents->GetPrimaryMainFrame(), "username_field",
                         "password_field"));
  }

  void ExpectMainFrameEmpty(Browser *target_browser) {
    content::WebContents *web_contents =
        target_browser->tab_strip_model()->GetActiveWebContents();
    ASSERT_NE(nullptr, web_contents);
    EXPECT_EQ(std::make_pair(std::string(), std::string()),
              ReadFields(web_contents->GetPrimaryMainFrame(), "username_field",
                         "password_field"));
  }

  bool AddCredential(Profile *profile, const GURL &form_url,
                     const std::string &username, const std::string &password) {
    MahoPasswordStoreBackend *backend = BackendForProfile(profile);
    if (!backend) {
      return false;
    }

    password_manager::StoredCredential credential;
    credential.scheme = password_manager::PasswordForm::Scheme::kHtml;
    credential.signon_realm = OriginFor(form_url);
    credential.url = form_url;
    credential.action = form_url.Resolve("done.html");
    credential.username_element = u"username_field";
    credential.password_element = u"password_field";
    credential.username_value = base::UTF8ToUTF16(username);
    credential.password_value = base::UTF8ToUTF16(password);
    credential.date_created = base::Time::Now();
    credential.in_store = password_manager::PasswordForm::Store::kProfileStore;

    Grant(profile, PasswordAuthorizationAction::kAdd, credential.signon_realm);
    base::RunLoop loop;
    bool success = false;
    backend->AddLoginAsync(
        std::move(credential),
        base::BindOnce(
            [](base::RunLoop *loop, bool *success,
               password_manager::PasswordChangesOrError result) {
              *success = !std::holds_alternative<
                  password_manager::PasswordStoreBackendError>(result);
              loop->Quit();
            },
            &loop, &success));
    loop.Run();
    return success;
  }

  bool UpdateCredential(Profile *profile,
                        password_manager::StoredCredential credential) {
    MahoPasswordStoreBackend *backend = BackendForProfile(profile);
    if (!backend) {
      return false;
    }

    Grant(profile, PasswordAuthorizationAction::kUpdate,
          credential.signon_realm);
    base::RunLoop loop;
    bool success = false;
    backend->UpdateLoginAsync(
        std::move(credential),
        base::BindOnce(
            [](base::RunLoop *loop, bool *success,
               password_manager::PasswordChangesOrError result) {
              *success = !std::holds_alternative<
                  password_manager::PasswordStoreBackendError>(result);
              loop->Quit();
            },
            &loop, &success));
    loop.Run();
    return success;
  }

  MatchingResult ReadMatching(MahoPasswordStoreBackend &backend,
                              const GURL &form_url) {
    MatchingResult output;
    std::vector<password_manager::PasswordFormDigest> digests;
    digests.emplace_back(password_manager::PasswordForm::Scheme::kHtml,
                         OriginFor(form_url), form_url);

    base::RunLoop loop;
    backend.FillMatchingLoginsAsync(
        base::BindOnce(&MahoPasswordFillBrowserTest::CaptureMatchingResult,
                       &loop, &output),
        /*include_psl=*/false, digests);
    loop.Run();
    return output;
  }

  MatchingResult DiscoverForContext(
      MahoPasswordStoreBackend &backend, const GURL &form_url,
      const password_manager::PasswordFillRequestContext &context) {
    MatchingResult output;
    password_manager::PasswordFormDigest digest(
        password_manager::PasswordForm::Scheme::kHtml, OriginFor(form_url),
        form_url);
    base::RunLoop loop;
    static_cast<password_manager::PasswordStoreBackend &>(backend)
        .GetGroupedMatchingLoginsAsync(
            digest, context,
            base::BindOnce(&MahoPasswordFillBrowserTest::CaptureMatchingResult,
                           &loop, &output));
    loop.Run();
    return output;
  }

  static void CaptureMatchingResult(
      base::RunLoop *loop, MatchingResult *output,
      password_manager::BackendLoginsResultOrError result) {
    if (auto *forms = std::get_if<password_manager::BackendLoginsResult>(
            &result)) {
      output->success = true;
      output->forms = std::move(*forms);
    }
    loop->Quit();
  }

  password_manager::ContentPasswordManagerDriver *DriverFor(
      content::RenderFrameHost *frame) {
    auto *driver = password_manager::ContentPasswordManagerDriver::
        GetForRenderFrameHost(frame);
    EXPECT_NE(nullptr, driver);
    return driver;
  }

  std::optional<password_manager::PasswordFillSelection> DiscoverSelection(
      MahoPasswordStoreBackend &backend, const GURL &form_url,
      const password_manager::PasswordFillRequestContext &context) {
    MatchingResult discovered = DiscoverForContext(backend, form_url, context);
    EXPECT_TRUE(discovered.success);
    EXPECT_EQ(1u, discovered.forms.size());
    if (!discovered.success || discovered.forms.size() != 1u ||
        !discovered.forms[0].primary_key) {
      return std::nullopt;
    }
    const int key = discovered.forms[0].primary_key->value();
    std::optional<uint64_t> revision =
        backend.identity_map_for_testing().RevisionForPrimaryKey(key);
    EXPECT_TRUE(revision.has_value());
    if (!revision) {
      return std::nullopt;
    }
    return password_manager::PasswordFillSelection{
        .primary_key = discovered.forms[0].primary_key,
        .observed_revision = *revision};
  }

  bool ResolveAndDispatch(
      MahoPasswordStoreBackend &backend,
      password_manager::ContentPasswordManagerDriver &driver,
      const password_manager::PasswordFillRequestContext &context,
      const password_manager::PasswordFillSelection &selection,
      const std::string &username) {
    if (content::RenderFrameHost *rfh = driver.render_frame_host()) {
      if (content::WebContents *web_contents =
              content::WebContents::FromRenderFrameHost(rfh)) {
        web_contents->Focus();
      }
    }
    base::RunLoop loop;
    bool filled = false;
    password_manager::PasswordFillResolver resolver =
        driver.CreatePasswordFillResolver(
            context, base::UTF8ToUTF16(username),
            base::BindOnce(
                [](base::RunLoop *loop, bool *filled, bool success) {
                  *filled = success;
                  loop->Quit();
                },
                &loop, &filled));
    static_cast<password_manager::PasswordStoreBackend &>(backend)
        .ResolvePasswordFill(context, selection, std::move(resolver));
    loop.Run();
    return filled;
  }

  void SaveCredentialFromPage(const GURL &form_url, const std::string &username,
                              const std::string &password) {
    ASSERT_TRUE(
        AddCredential(browser()->profile(), form_url, username, password));
  }

  virtual int TestServerPort() const { return 0; }

  MahoCore *core_ = nullptr;
  base::CallbackListSubscription create_services_subscription_;
};

class MahoPasswordFillRestartBrowserTest
    : public MahoPasswordFillBrowserTest {
 protected:
  int TestServerPort() const override { return kRestartStableHttpsPort; }
};

IN_PROC_BROWSER_TEST_F(
    MahoPasswordFillRestartBrowserTest,
    PRE_NativeSaveSurvivesBrowserProcessRestartAndReloadFillsTargetRenderer) {
  const GURL form_url = PasswordFormUrl(kRestartStableHost);
  SaveCredentialFromPage(form_url, kUsername, kPassword);
}

IN_PROC_BROWSER_TEST_F(
    MahoPasswordFillRestartBrowserTest,
    NativeSaveSurvivesBrowserProcessRestartAndReloadFillsTargetRenderer) {
  const GURL form_url = PasswordFormUrl(kRestartStableHost);
  const std::string origin = OriginFor(form_url);

  NavigateToPasswordForm(browser(), form_url);
  content::RenderFrameHost *frame = browser()
                                        ->tab_strip_model()
                                        ->GetActiveWebContents()
                                        ->GetPrimaryMainFrame();
  auto *driver = DriverFor(frame);
  ASSERT_NE(nullptr, driver);
  const password_manager::PasswordFillRequestContext context =
      driver->GetPasswordFillRequestContext();
  MahoPasswordStoreBackend *backend = BackendForProfile(browser()->profile());
  ASSERT_NE(nullptr, backend);

  Grant(browser()->profile(), PasswordAuthorizationAction::kFill, origin);
  std::optional<password_manager::PasswordFillSelection> selection =
      DiscoverSelection(*backend, form_url, context);
  ASSERT_TRUE(selection.has_value());
  EXPECT_TRUE(
      ResolveAndDispatch(*backend, *driver, context, *selection, kUsername));
  ExpectMainFrameFilled(browser(), kUsername, kPassword);

  ReloadPasswordForm(browser());
  frame = browser()
              ->tab_strip_model()
              ->GetActiveWebContents()
              ->GetPrimaryMainFrame();
  driver = DriverFor(frame);
  ASSERT_NE(nullptr, driver);
  const password_manager::PasswordFillRequestContext reloaded_context =
      driver->GetPasswordFillRequestContext();
  Grant(browser()->profile(), PasswordAuthorizationAction::kFill, origin);
  selection = DiscoverSelection(*backend, form_url, reloaded_context);
  ASSERT_TRUE(selection.has_value());
  EXPECT_TRUE(ResolveAndDispatch(*backend, *driver, reloaded_context,
                                 *selection, kUsername));
  ExpectMainFrameFilled(browser(), kUsername, kPassword);
}

IN_PROC_BROWSER_TEST_F(MahoPasswordFillBrowserTest,
                       DeniedDeviceReauthLeavesUnlockedVaultAndRendererEmpty) {
  const GURL form_url = PasswordFormUrl("denied-auth.test");
  NavigateToPasswordForm(browser(), form_url);
  ASSERT_TRUE(
      AddCredential(browser()->profile(), form_url, kUsername, kPassword));
  ASSERT_EQ(std::optional<std::string>("unlocked"), VaultLockState(core_));

  content::RenderFrameHost *frame = browser()
                                        ->tab_strip_model()
                                        ->GetActiveWebContents()
                                        ->GetPrimaryMainFrame();
  auto *driver = DriverFor(frame);
  ASSERT_NE(nullptr, driver);
  const password_manager::PasswordFillRequestContext context =
      driver->GetPasswordFillRequestContext();
  MahoPasswordStoreBackend *backend = BackendForProfile(browser()->profile());
  ASSERT_NE(nullptr, backend);
  Grant(browser()->profile(), PasswordAuthorizationAction::kFill,
        OriginFor(form_url));
  std::optional<password_manager::PasswordFillSelection> selection =
      DiscoverSelection(*backend, form_url, context);
  ASSERT_TRUE(selection.has_value());

  authorization_service()->ResetForTesting();
  authorization_service()->SetVaultLockedForTesting(false);
  authorization_service()->SetInteractiveAuthorizationEnabledForTesting(false);
  auto authenticator =
      std::make_unique<device_reauth::MockDeviceAuthenticator>();
  auto *authenticator_ptr = authenticator.get();
  EXPECT_CALL(*authenticator_ptr, AuthenticateWithMessage)
      .WillOnce([](const std::u16string &,
                   device_reauth::DeviceAuthenticator::AuthenticateCallback
                       callback) { std::move(callback).Run(false); });

  bool authorized = true;
  authorization_service()->Authorize(
      ProfileKey(browser()->profile()), OriginFor(form_url),
      PasswordAuthorizationAction::kFill, std::move(authenticator),
      u"Fill password",
      base::BindOnce([](bool *authorized, bool success) {
        *authorized = success;
      }, &authorized));

  EXPECT_FALSE(authorized);
  EXPECT_EQ(0u, authorization_service()->grant_count_for_testing());
  EXPECT_EQ(std::optional<std::string>("unlocked"), VaultLockState(core_));
  EXPECT_FALSE(
      ResolveAndDispatch(*backend, *driver, context, *selection, kUsername));
  ExpectMainFrameEmpty(browser());
}

IN_PROC_BROWSER_TEST_F(MahoPasswordFillBrowserTest,
                       WrongOriginLeavesRendererEmpty) {
  const GURL saved_url = PasswordFormUrl("saved-origin.test");
  const GURL wrong_url = PasswordFormUrl("wrong-origin.test");
  ASSERT_TRUE(
      AddCredential(browser()->profile(), saved_url, kUsername, kPassword));
  NavigateToPasswordForm(browser(), saved_url);

  content::WebContents *saved_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  auto *saved_driver = DriverFor(saved_contents->GetPrimaryMainFrame());
  ASSERT_NE(nullptr, saved_driver);
  const password_manager::PasswordFillRequestContext saved_context =
      saved_driver->GetPasswordFillRequestContext();
  MahoPasswordStoreBackend *backend = BackendForProfile(browser()->profile());
  ASSERT_NE(nullptr, backend);
  Grant(browser()->profile(), PasswordAuthorizationAction::kFill,
        OriginFor(saved_url));
  std::optional<password_manager::PasswordFillSelection> selection =
      DiscoverSelection(*backend, saved_url, saved_context);
  ASSERT_TRUE(selection.has_value());

  NavigateToPasswordForm(browser(), wrong_url);
  auto *wrong_driver = DriverFor(browser()
                                     ->tab_strip_model()
                                     ->GetActiveWebContents()
                                     ->GetPrimaryMainFrame());
  ASSERT_NE(nullptr, wrong_driver);
  const password_manager::PasswordFillRequestContext wrong_context =
      wrong_driver->GetPasswordFillRequestContext();
  Grant(browser()->profile(), PasswordAuthorizationAction::kFill,
        OriginFor(wrong_url));
  EXPECT_FALSE(ResolveAndDispatch(*backend, *wrong_driver, wrong_context,
                                  *selection, kUsername));
  ExpectMainFrameEmpty(browser());

  NavigateToPasswordForm(browser(), saved_url);
  auto *current_driver = DriverFor(browser()
                                       ->tab_strip_model()
                                       ->GetActiveWebContents()
                                       ->GetPrimaryMainFrame());
  ASSERT_NE(nullptr, current_driver);
  const password_manager::PasswordFillRequestContext current_context =
      current_driver->GetPasswordFillRequestContext();
  Grant(browser()->profile(), PasswordAuthorizationAction::kFill,
        OriginFor(saved_url));
  std::optional<password_manager::PasswordFillSelection> current_selection =
      DiscoverSelection(*backend, saved_url, current_context);
  ASSERT_TRUE(current_selection.has_value());
  EXPECT_TRUE(ResolveAndDispatch(*backend, *current_driver, current_context,
                                 *current_selection, kUsername));
  ExpectMainFrameFilled(browser(), kUsername, kPassword);
}

IN_PROC_BROWSER_TEST_F(MahoPasswordFillBrowserTest,
                       StaleSelectedRevisionIsRejectedBeforeDispatch) {
  const GURL form_url = PasswordFormUrl("stale-revision.test");
  const std::string origin = OriginFor(form_url);
  ASSERT_TRUE(
      AddCredential(browser()->profile(), form_url, kUsername, kPassword));
  NavigateToPasswordForm(browser(), form_url);

  content::RenderFrameHost *frame = browser()
                                        ->tab_strip_model()
                                        ->GetActiveWebContents()
                                        ->GetPrimaryMainFrame();
  auto *driver = DriverFor(frame);
  ASSERT_NE(nullptr, driver);
  const password_manager::PasswordFillRequestContext context =
      driver->GetPasswordFillRequestContext();
  MahoPasswordStoreBackend *backend = BackendForProfile(browser()->profile());
  ASSERT_NE(nullptr, backend);

  Grant(browser()->profile(), PasswordAuthorizationAction::kFill, origin);
  std::optional<password_manager::PasswordFillSelection> stale_selection =
      DiscoverSelection(*backend, form_url, context);
  ASSERT_TRUE(stale_selection.has_value());

  MatchingResult initial = ReadMatching(*backend, form_url);
  ASSERT_TRUE(initial.success);
  ASSERT_EQ(1u, initial.forms.size());
  password_manager::StoredCredential current =
      password_manager::FromPasswordForm(
          password_manager::ToPasswordForm(initial.forms[0]));
  current.password_value = u"S3NTINEL-current-revision-6B2F";
  ASSERT_TRUE(UpdateCredential(browser()->profile(), std::move(current)));

  Grant(browser()->profile(), PasswordAuthorizationAction::kFill, origin);
  EXPECT_FALSE(ResolveAndDispatch(*backend, *driver, context,
                                  *stale_selection, kUsername));
  ExpectMainFrameEmpty(browser());

  Grant(browser()->profile(), PasswordAuthorizationAction::kFill, origin);
  std::optional<password_manager::PasswordFillSelection> current_selection =
      DiscoverSelection(*backend, form_url, context);
  ASSERT_TRUE(current_selection.has_value());
  EXPECT_NE(stale_selection->observed_revision,
            current_selection->observed_revision);
  EXPECT_TRUE(ResolveAndDispatch(*backend, *driver, context,
                                 *current_selection, kUsername));
  ExpectMainFrameFilled(browser(), kUsername, kFreshPassword);
}

IN_PROC_BROWSER_TEST_F(
    MahoPasswordFillBrowserTest,
    CurrentSelectionDispatchesOnlyToCapturedCurrentDocument) {
  const GURL form_url = PasswordFormUrl("document-target.test");
  const GURL cross_origin_url = PasswordFormUrl("document-cross-origin.test");
  ASSERT_TRUE(
      AddCredential(browser()->profile(), form_url, kUsername, kPassword));
  NavigateToPasswordForm(browser(), form_url);

  content::WebContents *target_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  auto *target_driver = DriverFor(target_contents->GetPrimaryMainFrame());
  ASSERT_NE(nullptr, target_driver);
  const password_manager::PasswordFillRequestContext target_context =
      target_driver->GetPasswordFillRequestContext();

  content::WebContents *sibling_contents = chrome::AddSelectedTabWithURL(
      browser(), form_url, ui::PAGE_TRANSITION_LINK);
  ASSERT_NE(nullptr, sibling_contents);
  ASSERT_TRUE(content::WaitForLoadStop(sibling_contents));
  auto *sibling_driver = DriverFor(sibling_contents->GetPrimaryMainFrame());
  ASSERT_NE(nullptr, sibling_driver);

  content::WebContents *cross_contents = chrome::AddSelectedTabWithURL(
      browser(), cross_origin_url, ui::PAGE_TRANSITION_LINK);
  ASSERT_NE(nullptr, cross_contents);
  ASSERT_TRUE(content::WaitForLoadStop(cross_contents));
  auto *cross_origin_driver = DriverFor(cross_contents->GetPrimaryMainFrame());
  ASSERT_NE(nullptr, cross_origin_driver);

  MahoPasswordStoreBackend *backend = BackendForProfile(browser()->profile());
  ASSERT_NE(nullptr, backend);
  Grant(browser()->profile(), PasswordAuthorizationAction::kFill,
        OriginFor(form_url));
  std::optional<password_manager::PasswordFillSelection> selection =
      DiscoverSelection(*backend, form_url, target_context);
  ASSERT_TRUE(selection.has_value());
  EXPECT_FALSE(ResolveAndDispatch(*backend, *sibling_driver, target_context,
                                  *selection, kUsername));
  EXPECT_FALSE(ResolveAndDispatch(*backend, *cross_origin_driver,
                                  target_context, *selection, kUsername));

  browser()->tab_strip_model()->ActivateTabAt(0);
  ReloadPasswordForm(browser());
  target_driver = DriverFor(browser()->tab_strip_model()->GetActiveWebContents()->GetPrimaryMainFrame());
  ASSERT_NE(nullptr, target_driver);
  EXPECT_FALSE(ResolveAndDispatch(*backend, *target_driver, target_context,
                                  *selection, kUsername));
  ExpectMainFrameEmpty(browser());
  ASSERT_NE(nullptr, target_driver);
  const password_manager::PasswordFillRequestContext current_context =
      target_driver->GetPasswordFillRequestContext();
  Grant(browser()->profile(), PasswordAuthorizationAction::kFill,
        OriginFor(form_url));
  selection = DiscoverSelection(*backend, form_url, current_context);
  ASSERT_TRUE(selection.has_value());
  EXPECT_TRUE(ResolveAndDispatch(*backend, *target_driver, current_context,
                                 *selection, kUsername));
  ExpectMainFrameFilled(browser(), kUsername, kPassword);
}

IN_PROC_BROWSER_TEST_F(MahoPasswordFillBrowserTest,
                       CrossProfileDoesNotReceiveOwnerCredential) {
  const GURL form_url = PasswordFormUrl("cross-profile.test");
  const std::string origin = OriginFor(form_url);
  ASSERT_TRUE(
      AddCredential(browser()->profile(), form_url, kUsername, kPassword));
  NavigateToPasswordForm(browser(), form_url);
  auto *owner_driver = DriverFor(browser()
                                     ->tab_strip_model()
                                     ->GetActiveWebContents()
                                     ->GetPrimaryMainFrame());
  ASSERT_NE(nullptr, owner_driver);
  const password_manager::PasswordFillRequestContext owner_context =
      owner_driver->GetPasswordFillRequestContext();
  MahoPasswordStoreBackend *owner_backend =
      BackendForProfile(browser()->profile());
  ASSERT_NE(nullptr, owner_backend);
  Grant(browser()->profile(), PasswordAuthorizationAction::kFill, origin);
  std::optional<password_manager::PasswordFillSelection> selection =
      DiscoverSelection(*owner_backend, form_url, owner_context);
  ASSERT_TRUE(selection.has_value());

  ProfileManager *profile_manager = g_browser_process->profile_manager();
  ASSERT_NE(nullptr, profile_manager);
  Profile &other_profile = profiles::testing::CreateProfileSync(
      profile_manager, profile_manager->user_data_dir().AppendASCII(
                           "MahoPasswordFillOtherProfile"));
  ASSERT_FALSE(maho::IsPasswordManagerAllowedForProfile(&other_profile));
  Browser *other_browser = CreateBrowser(&other_profile);
  ASSERT_NE(nullptr, other_browser);

  NavigateToPasswordForm(other_browser, form_url);
  MahoPasswordStoreBackend *other_backend = BackendForProfile(&other_profile);
  ASSERT_NE(nullptr, other_backend);
  EXPECT_EQ(password_manager::ActionableError::kInactionable,
            other_backend->GetError());
  auto *other_driver = DriverFor(other_browser->tab_strip_model()
                                     ->GetActiveWebContents()
                                     ->GetPrimaryMainFrame());
  ASSERT_NE(nullptr, other_driver);
  EXPECT_FALSE(ResolveAndDispatch(*other_backend, *other_driver,
                                  other_driver->GetPasswordFillRequestContext(),
                                  *selection, kUsername));
  ExpectMainFrameEmpty(other_browser);

  Grant(browser()->profile(), PasswordAuthorizationAction::kFill, origin);
  EXPECT_TRUE(ResolveAndDispatch(*owner_backend, *owner_driver, owner_context,
                                 *selection, kUsername));
  ExpectMainFrameFilled(browser(), kUsername, kPassword);
}

IN_PROC_BROWSER_TEST_F(MahoPasswordFillBrowserTest,
                       LockedVaultLeavesRendererEmpty) {
  const GURL form_url = PasswordFormUrl("locked-vault.test");
  const std::string origin = OriginFor(form_url);
  ASSERT_TRUE(
      AddCredential(browser()->profile(), form_url, kUsername, kPassword));
  NavigateToPasswordForm(browser(), form_url);
  auto *driver = DriverFor(browser()
                               ->tab_strip_model()
                               ->GetActiveWebContents()
                               ->GetPrimaryMainFrame());
  ASSERT_NE(nullptr, driver);
  const password_manager::PasswordFillRequestContext context =
      driver->GetPasswordFillRequestContext();
  MahoPasswordStoreBackend *backend = BackendForProfile(browser()->profile());
  ASSERT_NE(nullptr, backend);
  Grant(browser()->profile(), PasswordAuthorizationAction::kFill, origin);
  std::optional<password_manager::PasswordFillSelection> selection =
      DiscoverSelection(*backend, form_url, context);
  ASSERT_TRUE(selection.has_value());

  ASSERT_TRUE(VaultOperationSucceeded(maho_vault_lock_json(core_)));
  ASSERT_EQ(std::optional<std::string>("locked"), VaultLockState(core_));
  EXPECT_FALSE(
      ResolveAndDispatch(*backend, *driver, context, *selection, kUsername));
  ExpectMainFrameEmpty(browser());

  ASSERT_TRUE(UnlockVault());
  ASSERT_EQ(std::optional<std::string>("unlocked"), VaultLockState(core_));
  authorization_service()->SetVaultLockedForTesting(false);
  maho::NotifyVaultLockStateChanged(/*locked=*/false);
  Grant(browser()->profile(), PasswordAuthorizationAction::kFill, origin);
  EXPECT_TRUE(
      ResolveAndDispatch(*backend, *driver, context, *selection, kUsername));
  ExpectMainFrameFilled(browser(), kUsername, kPassword);
}

} // namespace passwords
} // namespace maho