#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "ui/base/clipboard/test/test_clipboard.h"
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_routines_scheduler.h"
#include "maho/browser/passwords/maho_password_authorization_service.h"
#include "maho/browser/maho_space_profile_hydration.h"
#include "maho/browser/ui/webui/maho_subscription_checkout.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings_page_handler.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings_password_helpers.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "components/prefs/testing_pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_prefs_registration.h"



namespace maho_settings_password_helpers {
namespace {

constexpr char kSentinel[] = "S3NTINEL-maho-vault-9F4C";
constexpr char kSettingsAuthorizationProfile[] = "settings-test-profile";

void GrantSettingsAuthorization(
    maho::passwords::PasswordAuthorizationAction action) {
  auto* service =
      maho::passwords::MahoPasswordAuthorizationService::Get();
  service->SetVaultLockedForTesting(false);
  service->GrantForTesting(
      kSettingsAuthorizationProfile,
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      action,
      base::TimeTicks::Now() +
          maho::passwords::MahoPasswordAuthorizationService::
              kAuthorizationLifetime);
}

maho_settings::mojom::VaultOperationResultPtr AddVaultLoginInCore(
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::string& password,
    const std::optional<std::string>& notes = std::nullopt) {
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kAdd);
  return maho_settings_password_helpers::AddVaultLoginInCore(
      kSettingsAuthorizationProfile, title, origins, username, password, notes);
}

maho_settings::mojom::VaultOperationResultPtr UpdateVaultLoginInCore(
    const std::string& item_id,
    uint64_t expected_revision,
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::optional<std::string>& password,
    const std::optional<std::string>& notes = std::nullopt) {
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  return maho_settings_password_helpers::UpdateVaultLoginInCore(
      kSettingsAuthorizationProfile, item_id, expected_revision, title,
      origins, username, password, notes);
}

maho_settings::mojom::VaultOperationResultPtr DeleteVaultItemInCore(
    const std::string& item_id,
    uint64_t expected_revision) {
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kDelete);
  return maho_settings_password_helpers::DeleteVaultItemInCore(
      kSettingsAuthorizationProfile, item_id, expected_revision);
}

maho_settings::mojom::VaultOperationResultPtr UseVaultSecretInCore(
    const std::string& item_id,
    uint64_t expected_revision,
    maho_settings::mojom::SecretAction action) {
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kCopy);
  return maho_settings_password_helpers::UseVaultSecretInCore(
      kSettingsAuthorizationProfile, item_id, expected_revision, action);
}

maho_settings::mojom::PasswordImportOperationResultPtr
CommitPasswordImportInCore(MahoPasswordImportJob* job,
                           const std::string& preview_token) {
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kImportCommit);
  return maho_settings_password_helpers::CommitPasswordImportInCore(
      kSettingsAuthorizationProfile, job, preview_token);
}

maho_settings::mojom::VaultOperationResultPtr
AddSavedPasswordLibraryLoginInCore(
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::string& password) {
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kAdd);
  return maho_settings_password_helpers::AddSavedPasswordLibraryLoginInCore(
      kSettingsAuthorizationProfile, title, origins, username, password);
}

maho_settings::mojom::VaultOperationResultPtr
UpdateSavedPasswordLibraryLoginInCore(
    const std::string& item_id,
    uint64_t expected_revision,
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::optional<std::string>& password) {
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  return maho_settings_password_helpers::UpdateSavedPasswordLibraryLoginInCore(
      kSettingsAuthorizationProfile, item_id, expected_revision, title,
      origins, username, password);
}

maho_settings::mojom::VaultOperationResultPtr
DeleteSavedPasswordLibraryItemInCore(const std::string& item_id,
                                     uint64_t expected_revision) {
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kDelete);
  return maho_settings_password_helpers::DeleteSavedPasswordLibraryItemInCore(
      kSettingsAuthorizationProfile, item_id, expected_revision);
}

maho_settings::mojom::VaultOperationResultPtr
UseSavedPasswordLibrarySecretInCore(
    const std::string& item_id,
    uint64_t expected_revision,
    maho_settings::mojom::SecretAction action) {
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kCopy);
  return maho_settings_password_helpers::UseSavedPasswordLibrarySecretInCore(
      kSettingsAuthorizationProfile, item_id, expected_revision, action);
}

bool AddSavedPasswordCompatibilityShimInCore(const std::string& domain,
                                             const std::string& username,
                                             const std::string& password) {
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kAdd);
  return maho_settings_password_helpers::
      AddSavedPasswordCompatibilityShimInCore(
          kSettingsAuthorizationProfile, domain, username, password);
}

bool UpdateSavedPasswordUsernameCompatibilityShimInCore(
    const std::string& password_id,
    const std::string& username) {
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  return maho_settings_password_helpers::
      UpdateSavedPasswordUsernameCompatibilityShimInCore(
          kSettingsAuthorizationProfile, password_id, username);
}

bool DeleteSavedPasswordCompatibilityShimInCore(
    const std::string& password_id) {
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kDelete);
  return maho_settings_password_helpers::
      DeleteSavedPasswordCompatibilityShimInCore(
          kSettingsAuthorizationProfile, password_id);
}

maho_settings::mojom::VaultPolicyStatusPtr SetVaultPolicyInCore(
    maho_settings::mojom::VaultAgentPolicy policy,
    const std::optional<std::string>& item_id,
    const std::optional<std::string>& origin,
    const std::optional<std::string>& expires_at) {
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kPolicyUpdate);
  return maho_settings_password_helpers::SetVaultPolicyInCore(
      kSettingsAuthorizationProfile, policy, item_id, origin, expires_at);
}

// Installs a core and restores BOTH the previous core and its previous owner
// profile. maho::SetCore() clears the owner as a side effect, so saving only the
// core would silently drop an owner binding established by an enclosing scope.
//
// NOTE (profile authorization): the Vault mutation helpers exercised below do
// NOT perform their own caller-profile check. The authoritative
// maho::IsPasswordManagerAllowedForProfile() enforcement lives at the handler
// layer (MahoSettingsPageHandler / MahoWelcomePageHandler), which knows the
// profile that actually issued the request; this layer could only re-derive the
// core's owner and would wrongly admit requests from other profiles. Owner /
// non-owner denial coverage therefore lives in
// maho_vault_runtime_bridge_unittest.cc (MahoPasswordProfileOwnershipTest),
// whose GN target links //chrome/test:test_support and can build real
// TestingProfiles.
class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core)
      : saved_core_(maho::GetCore()),
        saved_owner_(maho::GetCoreOwnerProfile()) {
    maho::SetCoreForProfile(core, saved_owner_);
  }
  ~ScopedCoreOverride() {
    maho::SetCoreForProfile(saved_core_, saved_owner_);
  }

 private:
  MahoCore* saved_core_;
  Profile* saved_owner_;
};

class ScopedPasswordImportJob {
 public:
  ScopedPasswordImportJob() : job_(CreatePasswordImportJob()) {}
  ~ScopedPasswordImportJob() { FreePasswordImportJob(job_); }

  MahoPasswordImportJob* get() const { return job_; }

 private:
  MahoPasswordImportJob* job_ = nullptr;
};

base::FilePath WriteOnePasswordExport(const base::FilePath& dir,
                                      const std::string& origin) {
  const base::FilePath path = dir.AppendASCII("onepassword-import.csv");
  const std::string csv =
      "Title,Website,Username,Password,One-time password,Favorite status,"
      "Archived status,Tags,Notes\n"
      "Example," +
      origin + ",user@example.test," + kSentinel +
      ",,false,false,synthetic,Synthetic note\n";
  EXPECT_TRUE(base::WriteFile(path, csv));
  return path;
}

std::string PreviewSurface(
    const maho_settings::mojom::PasswordImportPreview& preview) {
  std::string surface = preview.source_label;
  for (const std::string& message : preview.safe_messages) {
    surface += message;
  }
  for (const std::string& error : preview.safe_errors) {
    surface += error;
  }
  return surface;
}

class MahoSettingsVaultHandlerTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(maho_storage_set_sqlcipher_key(
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    const base::FilePath database_path =
        temp_dir_.GetPath().AppendASCII("vault-handler.sqlite");
    core_ = maho_core_new_with_storage(database_path.AsUTF8Unsafe().c_str());
    ASSERT_NE(core_, nullptr);
    core_override_ = std::make_unique<ScopedCoreOverride>(core_);
    ui::TestClipboard::CreateForCurrentThread();
    // The authorization service is a process-wide singleton: start every test
    // with no grants and device re-auth required so unauthorized calls are
    // rejected regardless of test order.
    auto* authorization =
        maho::passwords::MahoPasswordAuthorizationService::Get();
    authorization->ResetForTesting();
    authorization->SetDeviceReauthRequiredForTesting(true);
  }

  void TearDown() override {
    maho::passwords::MahoPasswordAuthorizationService::Get()->ResetForTesting();
    ui::Clipboard::DestroyClipboardForCurrentThread();
    core_override_.reset();
    maho_core_free(core_);
    core_ = nullptr;
  }

  const base::FilePath& temp_path() const { return temp_dir_.GetPath(); }
  MahoCore* core() const { return core_; }

 private:
  base::ScopedTempDir temp_dir_;
  MahoCore* core_ = nullptr;
  std::unique_ptr<ScopedCoreOverride> core_override_;
};

TEST(SubscriptionCheckoutMappingTest, MapsOnlyMonthlySubscriptionTiers) {
  EXPECT_EQ(maho::webui::SubscriptionCheckoutUrlForTier("pro"),
            maho::webui::kProMonthlyCheckoutUrl);
  EXPECT_EQ(maho::webui::SubscriptionCheckoutUrlForTier("max"),
            maho::webui::kMaxMonthlyCheckoutUrl);
  EXPECT_TRUE(
      maho::webui::SubscriptionCheckoutUrlForTier("free").empty());
  EXPECT_TRUE(
      maho::webui::SubscriptionCheckoutUrlForTier("enterprise").empty());
}

TEST_F(MahoSettingsVaultHandlerTest,
       InitializeUnlockListAndStatusResponsesRemainSecretFree) {
  auto initial_status = GetVaultStatusFromCore();
  ASSERT_TRUE(initial_status->success);
  ASSERT_TRUE(initial_status->status);
  EXPECT_EQ(maho_settings::mojom::VaultLockState::kUninitialized,
            initial_status->status->lock_state);

  auto initialized =
      InitializeVaultInCore("correct passphrase", "recovery key");
  ASSERT_TRUE(initialized->success);
  ASSERT_TRUE(initialized->status);
  EXPECT_EQ(maho_settings::mojom::VaultLockState::kUnlocked,
            initialized->status->lock_state);

  auto added = AddVaultLoginInCore("Example account", {"https://example.test"},
                                   "person@example.test", kSentinel);
  ASSERT_TRUE(added->success);
  ASSERT_TRUE(added->item);
  const std::string item_id = added->item->id;

  auto locked = LockVaultInCore();
  ASSERT_TRUE(locked->success);
  EXPECT_EQ(maho_settings::mojom::VaultLockState::kLocked,
            locked->status->lock_state);

  auto unlocked = UnlockVaultInCore("correct passphrase");
  ASSERT_TRUE(unlocked->success);
  EXPECT_EQ(maho_settings::mojom::VaultLockState::kUnlocked,
            unlocked->status->lock_state);

  auto listed = ListVaultItemsFromCore(
      std::nullopt, {maho_settings::mojom::VaultItemKind::kLogin}, std::nullopt,
      50);
  ASSERT_TRUE(listed->success);
  ASSERT_EQ(1u, listed->items.size());
  EXPECT_EQ(item_id, listed->items[0]->id);
  EXPECT_EQ("Example account", listed->items[0]->title);
  EXPECT_NE(kSentinel, listed->items[0]->title);
  EXPECT_NE(kSentinel, listed->items[0]->username_hint);

  auto searched =
      SearchVaultItemsFromCore("https://example.test", std::nullopt,
                               {maho_settings::mojom::VaultItemKind::kLogin});
  ASSERT_TRUE(searched->success);
  ASSERT_EQ(1u, searched->items.size());
  EXPECT_EQ(item_id, searched->items[0]->id);
}

TEST_F(MahoSettingsVaultHandlerTest,
       ListAndSearchMojoResultsRemainMetadataOnly) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);
  auto added = AddVaultLoginInCore("Example account", {"https://example.test"},
                                   "person@example.test", kSentinel);
  ASSERT_TRUE(added->success);
  ASSERT_TRUE(added->item);

  auto listed = ListVaultItemsFromCore(
      std::nullopt, {maho_settings::mojom::VaultItemKind::kLogin}, std::nullopt,
      50);
  auto searched =
      SearchVaultItemsFromCore("https://example.test", std::nullopt,
                               {maho_settings::mojom::VaultItemKind::kLogin});

  ASSERT_TRUE(listed->success);
  ASSERT_TRUE(searched->success);
  ASSERT_EQ(1u, listed->items.size());
  ASSERT_EQ(1u, searched->items.size());
  for (const auto& item : {listed->items[0].get(), searched->items[0].get()}) {
    SCOPED_TRACE(item->id);
    EXPECT_EQ(added->item->id, item->id);
    EXPECT_NE(kSentinel, item->id);
    EXPECT_NE(kSentinel, item->title);
    EXPECT_NE(kSentinel, item->username_hint);
    EXPECT_NE(kSentinel, item->created_at);
    EXPECT_NE(kSentinel, item->updated_at);
    ASSERT_EQ(1u, item->origins.size());
    EXPECT_NE(kSentinel, item->origins[0]);
    if (item->last_used_at) {
      EXPECT_NE(kSentinel, *item->last_used_at);
    }
  }
}

TEST_F(MahoSettingsVaultHandlerTest,
       SecretBearingListPayloadsAreRejectedBeforeMojoProjection) {
  constexpr std::string_view kSecretFields[] = {
      "password",   "username", "secret", "encryptedPayload", "envelope",
      "ciphertext", "nonce",    "tag",    "privateKey",       "seed"};

  for (std::string_view field : kSecretFields) {
    SCOPED_TRACE(field);
    const std::string json = R"({
      "ok": true,
      "data": [{
        "schemaVersion": 1,
        "id": "item-1",
        "revision": 1,
        "provider": "maho_native",
        "itemKind": "login",
        "title": "Example",
        "origins": ["https://example.test"],
        "usernameHint": "p***@example.test",
        "createdAt": "2026-07-24T00:00:00Z",
        "updatedAt": "2026-07-24T00:00:00Z",
        "lastUsedAt": null,
        "totp": null,
        "passkey": null,
        ")" + std::string(field) +
                             R"(": "S3NTINEL-maho-vault-9F4C"
      }]
    })";

    auto result = ParseVaultItemListResultJson(json);
    EXPECT_FALSE(result->success);
    EXPECT_TRUE(result->items.empty());
    EXPECT_EQ("invalid_response", result->error_code);
  }
}

TEST_F(MahoSettingsVaultHandlerTest, WrongPassphraseReturnsSafeFailure) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);
  ASSERT_TRUE(LockVaultInCore()->success);
  auto result = UnlockVaultInCore("wrong passphrase");

  EXPECT_FALSE(result->success);
  EXPECT_EQ("invalid_credentials", result->error_code);
  EXPECT_EQ("Vault operation failed.", result->error_message);
  EXPECT_FALSE(result->status);
}

TEST_F(MahoSettingsVaultHandlerTest, VaultTrustSettingsDefaultAndRoundTrip) {
  ASSERT_NE(nullptr, core());

  EXPECT_EQ(15u,
            ReadVaultAutoLockMinutesFromCore().value_or(0u));
  EXPECT_TRUE(ReadVaultDeviceAuthRequiredFromCore());

  EXPECT_TRUE(UpdateVaultAutoLockMinutesInCore(60u));
  EXPECT_EQ(60u, ReadVaultAutoLockMinutesFromCore().value_or(0u));
  EXPECT_FALSE(UpdateVaultAutoLockMinutesInCore(7u));
  EXPECT_EQ(60u, ReadVaultAutoLockMinutesFromCore().value_or(0u));

  EXPECT_TRUE(UpdateVaultDeviceAuthRequiredInCore(false));
  EXPECT_FALSE(ReadVaultDeviceAuthRequiredFromCore());

  EXPECT_TRUE(UpdateVaultAutoLockMinutesInCore(0u));
  EXPECT_EQ(0u, ReadVaultAutoLockMinutesFromCore().value_or(1u));
}

TEST_F(MahoSettingsVaultHandlerTest,
       PasswordSetupCompletionRequiresApprovedProviderAndInitializedVault) {
  auto uninitialized = GetVaultStatusFromCore();
  ASSERT_TRUE(uninitialized->success);
  ASSERT_TRUE(uninitialized->status);
  EXPECT_FALSE(
      IsPasswordSetupCompleteForTesting("maho_native", uninitialized.get()));

  auto unlocked = InitializeVaultInCore("correct passphrase", "recovery key");
  ASSERT_TRUE(unlocked->success);
  ASSERT_TRUE(unlocked->status);

  for (const std::string_view provider :
       {"maho_native", "bitwarden", "onepassword"}) {
    SCOPED_TRACE(provider);
    EXPECT_TRUE(IsPasswordSetupCompleteForTesting(std::string(provider),
                                                  unlocked.get()));
  }

  EXPECT_FALSE(IsPasswordSetupCompleteForTesting("disabled", unlocked.get()));
  EXPECT_FALSE(IsPasswordSetupCompleteForTesting("unknown", unlocked.get()));
  EXPECT_FALSE(IsPasswordSetupCompleteForTesting("", unlocked.get()));

  auto locked = LockVaultInCore();
  ASSERT_TRUE(locked->success);
  ASSERT_TRUE(locked->status);
  EXPECT_TRUE(IsPasswordSetupCompleteForTesting("maho_native", locked.get()));

  auto auto_locked = maho_settings::mojom::VaultOperationResult::New();
  auto_locked->success = true;
  auto_locked->status = maho_settings::mojom::VaultStatus::New();
  auto_locked->status->lock_state =
      maho_settings::mojom::VaultLockState::kAutoLocked;
  EXPECT_TRUE(
      IsPasswordSetupCompleteForTesting("maho_native", auto_locked.get()));

  auto failed = maho_settings::mojom::VaultOperationResult::New();
  failed->success = false;
  EXPECT_FALSE(IsPasswordSetupCompleteForTesting("maho_native", failed.get()));

  auto missing_status = maho_settings::mojom::VaultOperationResult::New();
  missing_status->success = true;
  EXPECT_FALSE(
      IsPasswordSetupCompleteForTesting("maho_native", missing_status.get()));
  EXPECT_FALSE(IsPasswordSetupCompleteForTesting("maho_native", nullptr));
}

TEST_F(MahoSettingsVaultHandlerTest, RecoveryUnlockUsesDistinctLifecyclePath) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);
  ASSERT_TRUE(LockVaultInCore()->success);

  auto result = UnlockVaultWithRecoveryInCore("recovery key");
  ASSERT_TRUE(result->success);
  ASSERT_TRUE(result->status);
  EXPECT_EQ(maho_settings::mojom::VaultLockState::kUnlocked,
            result->status->lock_state);
}

TEST_F(MahoSettingsVaultHandlerTest,
       SuccessfulUnlockPublishesCanonicalUnlockedStateSynchronously) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);
  ASSERT_TRUE(LockVaultInCore()->success);

  auto* authorization =
      maho::passwords::MahoPasswordAuthorizationService::Get();
  authorization->ResetForTesting();
  maho::NotifyVaultLockStateChanged(/*locked=*/true);
  ASSERT_TRUE(maho::IsVaultLockedForUi());

  int notifications = 0;
  bool published_locked = true;
  base::CallbackListSubscription subscription =
      maho::AddVaultLockStateChangeCallback(base::BindRepeating(
          [](int* count, bool* last_locked, bool locked) {
            ++*count;
            *last_locked = locked;
          },
          &notifications, &published_locked));

  auto operation = UnlockVaultInCore("correct passphrase");
  ASSERT_TRUE(operation->success);
  auto result = MahoSettingsPageHandler::FinalizeVaultLifecycleOperation(
      std::move(operation), GetVaultStatusFromCore(),
      [](bool locked) { maho::NotifyVaultLockStateChanged(locked); });

  ASSERT_TRUE(result->success);
  ASSERT_TRUE(result->status);
  EXPECT_EQ(maho_settings::mojom::VaultLockState::kUnlocked,
            result->status->lock_state);
  EXPECT_EQ(1, notifications);
  EXPECT_FALSE(published_locked);
  EXPECT_FALSE(maho::IsVaultLockedForUi());

  maho::NotifyVaultLockStateChanged(/*locked=*/true);
  authorization->ResetForTesting();
}

TEST_F(MahoSettingsVaultHandlerTest,
       ExplicitLockPublishesAndRevokesAuthorizationSynchronously) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);

  auto* authorization =
      maho::passwords::MahoPasswordAuthorizationService::Get();
  authorization->ResetForTesting();
  maho::NotifyVaultLockStateChanged(/*locked=*/false);
  authorization->GrantForTesting(
      kSettingsAuthorizationProfile,
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kCopy,
      base::TimeTicks::Now() +
          maho::passwords::MahoPasswordAuthorizationService::
              kAuthorizationLifetime);
  ASSERT_EQ(1u, authorization->grant_count_for_testing());

  int notifications = 0;
  bool published_locked = false;
  base::CallbackListSubscription subscription =
      maho::AddVaultLockStateChangeCallback(base::BindRepeating(
          [](int* count, bool* last_locked, bool locked) {
            ++*count;
            *last_locked = locked;
          },
          &notifications, &published_locked));

  auto operation = LockVaultInCore();
  ASSERT_TRUE(operation->success);
  auto result = MahoSettingsPageHandler::FinalizeVaultLifecycleOperation(
      std::move(operation), GetVaultStatusFromCore(),
      [](bool locked) { maho::NotifyVaultLockStateChanged(locked); });

  ASSERT_TRUE(result->success);
  ASSERT_TRUE(result->status);
  EXPECT_EQ(maho_settings::mojom::VaultLockState::kLocked,
            result->status->lock_state);
  EXPECT_EQ(1, notifications);
  EXPECT_TRUE(published_locked);
  EXPECT_TRUE(maho::IsVaultLockedForUi());
  EXPECT_EQ(0u, authorization->grant_count_for_testing());
  EXPECT_FALSE(authorization->ConsumeAuthorization(
      kSettingsAuthorizationProfile,
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kCopy));

  authorization->ResetForTesting();
}

TEST(MahoSettingsVaultLifecyclePublicationTest,
     MissingCanonicalStatusFailsClosedAndPublishesLocked) {
  auto operation = maho_settings::mojom::VaultOperationResult::New();
  operation->success = true;
  bool published = false;
  bool published_locked = false;

  auto result = MahoSettingsPageHandler::FinalizeVaultLifecycleOperation(
      std::move(operation), nullptr,
      [&published, &published_locked](bool locked) {
        published = true;
        published_locked = locked;
      });

  ASSERT_TRUE(result);
  EXPECT_FALSE(result->success);
  EXPECT_FALSE(result->status);
  EXPECT_EQ("status_unavailable", result->error_code);
  EXPECT_TRUE(published);
  EXPECT_TRUE(published_locked);
}

TEST_F(MahoSettingsVaultHandlerTest, LockedCrudReturnsDenial) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);
  ASSERT_TRUE(LockVaultInCore()->success);

  auto result = ListVaultItemsFromCore(std::nullopt, {}, std::nullopt, 50);

  EXPECT_FALSE(result->success);
  EXPECT_TRUE(result->items.empty());
  EXPECT_EQ("locked", result->error_code);
  EXPECT_EQ("Vault operation failed.", result->error_message);

  auto add_result =
      AddVaultLoginInCore("Locked account", {"https://locked.example"},
                          "locked@example.test", kSentinel);
  EXPECT_FALSE(add_result->success);
  EXPECT_EQ("locked", add_result->error_code);
  EXPECT_FALSE(add_result->item);
}

TEST_F(MahoSettingsVaultHandlerTest,
       PasswordImportPreviewCommitUsesExplicitTokenAndLockedVaultDenies) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);
  ScopedPasswordImportJob import_job;
  ASSERT_NE(import_job.get(), nullptr);
  const base::FilePath export_path =
      WriteOnePasswordExport(temp_path(), "https://import.example");

  auto preview = PreviewPasswordImportFromPath(
      import_job.get(),
      maho_settings::mojom::PasswordImportSourceFormat::kOnePasswordCsv,
      export_path.AsUTF8Unsafe());
  ASSERT_TRUE(preview->success);
  ASSERT_TRUE(preview->preview);
  EXPECT_EQ(1u, preview->preview->imported);
  EXPECT_EQ(1u, preview->terminal_result_count);
  EXPECT_EQ(std::string::npos,
            PreviewSurface(*preview->preview).find(kSentinel));

  auto before_commit = ListVaultItemsFromCore(
      std::nullopt, {maho_settings::mojom::VaultItemKind::kLogin}, std::nullopt,
      50);
  ASSERT_TRUE(before_commit->success);
  EXPECT_TRUE(before_commit->items.empty());

  ASSERT_TRUE(LockVaultInCore()->success);
  auto locked_commit =
      CommitPasswordImportInCore(import_job.get(), preview->preview->preview_token);
  EXPECT_FALSE(locked_commit->success);
  EXPECT_EQ("locked", locked_commit->error_code);

  ASSERT_TRUE(UnlockVaultInCore("correct passphrase")->success);
  auto after_locked = ListVaultItemsFromCore(
      std::nullopt, {maho_settings::mojom::VaultItemKind::kLogin}, std::nullopt,
      50);
  ASSERT_TRUE(after_locked->success);
  EXPECT_TRUE(after_locked->items.empty());

  auto committed =
      CommitPasswordImportInCore(import_job.get(), preview->preview->preview_token);
  ASSERT_TRUE(committed->success);
  EXPECT_EQ(1u, committed->committed);
  EXPECT_EQ(1u, committed->terminal_result_count);
}

TEST_F(MahoSettingsVaultHandlerTest, UpdateAndDeleteReturnOnlyMetadata) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);
  auto added = AddVaultLoginInCore("Original", {"https://example.test"},
                                   "person@example.test", kSentinel);
  ASSERT_TRUE(added->success);
  ASSERT_TRUE(added->item);

  auto updated = UpdateVaultLoginInCore(added->item->id, added->item->revision,
                                        "Updated", {"https://example.test"},
                                        "renamed@example.test", std::nullopt);
  ASSERT_TRUE(updated->success);
  ASSERT_TRUE(updated->item);
  // Mutation responses carry only id + revision (VaultMutationResult); the
  // updated metadata is observable through a metadata list read.
  EXPECT_GT(updated->item->revision, added->item->revision);
  EXPECT_NE(kSentinel, updated->item->username_hint);
  auto after_update = ListVaultItemsFromCore(std::nullopt, {}, std::nullopt, 50);
  ASSERT_TRUE(after_update->success);
  ASSERT_EQ(1u, after_update->items.size());
  EXPECT_EQ("Updated", after_update->items[0]->title);

  auto deleted =
      DeleteVaultItemInCore(updated->item->id, updated->item->revision);
  ASSERT_TRUE(deleted->success);
  auto listed = ListVaultItemsFromCore(std::nullopt, {}, std::nullopt, 50);
  ASSERT_TRUE(listed->success);
  EXPECT_TRUE(listed->items.empty());
}

TEST(MahoSettingsClipboardContractTest,
     ClearsOnlyClipboardTextStillOwnedByMaho) {
  using Service = maho::passwords::MahoPasswordAuthorizationService;

  EXPECT_TRUE(Service::ShouldClearMahoOwnedClipboardForTesting(
      u"copied secret", u"copied secret"));
  EXPECT_FALSE(Service::ShouldClearMahoOwnedClipboardForTesting(
      u"copied secret", u"replacement text"));
  EXPECT_FALSE(Service::ShouldClearMahoOwnedClipboardForTesting(
      u"copied secret", std::u16string()));
}

TEST(MahoSettingsClipboardContractTest, ClipboardExpiryMatchesSixtySecondGrant) {
  EXPECT_EQ(base::Seconds(60),
            maho::passwords::MahoPasswordAuthorizationService::
                kAuthorizationLifetime);
}

TEST_F(MahoSettingsVaultHandlerTest,
       RawSensitiveHelpersRejectMissingExpiredAndLockedAuthorization) {
  using Action = maho::passwords::PasswordAuthorizationAction;
  auto* service =
      maho::passwords::MahoPasswordAuthorizationService::Get();
  service->ResetForTesting();
  service->SetVaultLockedForTesting(false);
  service->SetDeviceReauthRequiredForTesting(true);

  auto missing = maho_settings_password_helpers::AddVaultLoginInCore(
      kSettingsAuthorizationProfile, "Denied", {"https://denied.example"},
      "person@example.test", kSentinel);
  EXPECT_FALSE(missing->success);
  EXPECT_EQ("reauth_required", missing->error_code);

  service->GrantForTesting(
      kSettingsAuthorizationProfile,
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      Action::kDelete, base::TimeTicks::Now());
  auto expired = maho_settings_password_helpers::DeleteVaultItemInCore(
      kSettingsAuthorizationProfile, "item-1", 1);
  EXPECT_FALSE(expired->success);
  EXPECT_EQ("reauth_required", expired->error_code);

  service->SetVaultLockedForTesting(false);
  service->GrantForTesting(
      kSettingsAuthorizationProfile,
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      Action::kPolicyUpdate,
      base::TimeTicks::Now() +
          maho::passwords::MahoPasswordAuthorizationService::
              kAuthorizationLifetime);
  service->SetVaultLockedForTesting(true);
  auto locked_policy = maho_settings_password_helpers::SetVaultPolicyInCore(
      kSettingsAuthorizationProfile,
      maho_settings::mojom::VaultAgentPolicy::kAskEveryUse, std::nullopt,
      std::nullopt, std::nullopt);
  EXPECT_FALSE(locked_policy->is_available);
  EXPECT_EQ("reauth_required", locked_policy->unavailable_reason);

  service->ResetForTesting();
}

TEST_F(MahoSettingsVaultHandlerTest, UseVaultSecretReturnsOnlyStatus) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);
  auto added = AddVaultLoginInCore("Secret use", {"https://example.test"},
                                   "person@example.test", kSentinel);
  ASSERT_TRUE(added->success);
  ASSERT_TRUE(added->item);

  auto used = UseVaultSecretInCore(added->item->id, added->item->revision,
                                   maho_settings::mojom::SecretAction::kCopy);

  ASSERT_TRUE(used->success);
  EXPECT_FALSE(used->status);
  EXPECT_FALSE(used->item);
  EXPECT_FALSE(used->error_code);
  auto listed = ListVaultItemsFromCore(std::nullopt, {}, std::nullopt, 50);
  ASSERT_TRUE(listed->success);
  ASSERT_EQ(1u, listed->items.size());
  EXPECT_GT(listed->items[0]->revision, added->item->revision);
  EXPECT_NE(kSentinel, listed->items[0]->title);
  EXPECT_NE(kSentinel, listed->items[0]->username_hint);
}

TEST_F(MahoSettingsVaultHandlerTest,
       TrashRestoreAndEmptyTrashCycleWithExclusion) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);

  auto login1 = AddVaultLoginInCore("Item 1", {"https://one.example"},
                                    "user1@example.test", kSentinel);
  ASSERT_TRUE(login1->success);
  ASSERT_TRUE(login1->item);

  auto login2 = AddVaultLoginInCore("Item 2", {"https://two.example"},
                                    "user2@example.test", kSentinel);
  ASSERT_TRUE(login2->success);
  ASSERT_TRUE(login2->item);

  // 1. Unauthorized trash fails with reauth_required.
  auto unauth_trash = maho_settings_password_helpers::TrashVaultItemInCore(
      kSettingsAuthorizationProfile, login1->item->id, login1->item->revision);
  EXPECT_FALSE(unauth_trash->success);
  EXPECT_EQ("reauth_required", unauth_trash->error_code);

  // 2. CAS conflict on trash with full uint64 revision.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kDelete);
  auto conflict_trash = maho_settings_password_helpers::TrashVaultItemInCore(
      kSettingsAuthorizationProfile, login1->item->id,
      std::numeric_limits<uint64_t>::max());
  EXPECT_FALSE(conflict_trash->success);
  EXPECT_EQ("revision_conflict", conflict_trash->error_code);

  // 3. Authorized trash succeeds and marks trashed_at.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kDelete);
  auto trashed = maho_settings_password_helpers::TrashVaultItemInCore(
      kSettingsAuthorizationProfile, login1->item->id, login1->item->revision);
  ASSERT_TRUE(trashed->success);
  ASSERT_TRUE(trashed->item);
  EXPECT_GT(trashed->item->revision, login1->item->revision);

  // 4. Trashed item is excluded from normal list, included in trash_only list.
  auto normal_list = ListVaultItemsFromCore(
      std::nullopt, {}, std::nullopt, 50, /*trash_only=*/false,
      /*favorites_only=*/false);
  ASSERT_TRUE(normal_list->success);
  ASSERT_EQ(1u, normal_list->items.size());
  EXPECT_EQ(login2->item->id, normal_list->items[0]->id);

  auto trash_list = ListVaultItemsFromCore(
      std::nullopt, {}, std::nullopt, 50, /*trash_only=*/true,
      /*favorites_only=*/false);
  ASSERT_TRUE(trash_list->success);
  ASSERT_EQ(1u, trash_list->items.size());
  EXPECT_EQ(login1->item->id, trash_list->items[0]->id);
  EXPECT_TRUE(trash_list->items[0]->trashed_at.has_value());

  // 5. Unauthorized restore fails with reauth_required.
  auto unauth_restore = maho_settings_password_helpers::RestoreVaultItemInCore(
      kSettingsAuthorizationProfile, login1->item->id, trashed->item->revision);
  EXPECT_FALSE(unauth_restore->success);
  EXPECT_EQ("reauth_required", unauth_restore->error_code);

  // 6. CAS conflict on restore.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  auto conflict_restore = maho_settings_password_helpers::RestoreVaultItemInCore(
      kSettingsAuthorizationProfile, login1->item->id,
      std::numeric_limits<uint64_t>::max());
  EXPECT_FALSE(conflict_restore->success);
  EXPECT_EQ("revision_conflict", conflict_restore->error_code);

  // 7. Authorized restore succeeds and clears trashed_at.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  auto restored = maho_settings_password_helpers::RestoreVaultItemInCore(
      kSettingsAuthorizationProfile, login1->item->id, trashed->item->revision);
  ASSERT_TRUE(restored->success);
  ASSERT_TRUE(restored->item);
  EXPECT_FALSE(restored->item->trashed_at.has_value());
  EXPECT_GT(restored->item->revision, trashed->item->revision);

  auto normal_list_after_restore = ListVaultItemsFromCore(
      std::nullopt, {}, std::nullopt, 50, /*trash_only=*/false,
      /*favorites_only=*/false);
  ASSERT_TRUE(normal_list_after_restore->success);
  EXPECT_EQ(2u, normal_list_after_restore->items.size());

  auto trash_list_after_restore = ListVaultItemsFromCore(
      std::nullopt, {}, std::nullopt, 50, /*trash_only=*/true,
      /*favorites_only=*/false);
  ASSERT_TRUE(trash_list_after_restore->success);
  EXPECT_TRUE(trash_list_after_restore->items.empty());

  // 8. Re-trash both items and test EmptyVaultTrashInCore.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kDelete);
  ASSERT_TRUE(maho_settings_password_helpers::TrashVaultItemInCore(
                  kSettingsAuthorizationProfile, login1->item->id,
                  restored->item->revision)
                  ->success);

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kDelete);
  ASSERT_TRUE(maho_settings_password_helpers::TrashVaultItemInCore(
                  kSettingsAuthorizationProfile, login2->item->id,
                  login2->item->revision)
                  ->success);

  auto full_trash = ListVaultItemsFromCore(
      std::nullopt, {}, std::nullopt, 50, /*trash_only=*/true,
      /*favorites_only=*/false);
  ASSERT_TRUE(full_trash->success);
  EXPECT_EQ(2u, full_trash->items.size());

  // Empty trash without authorization fails. Grants stay valid for their
  // lifetime, so revoke the kDelete grants used above first.
  auto* authorization =
      maho::passwords::MahoPasswordAuthorizationService::Get();
  authorization->ResetForTesting();
  authorization->SetVaultLockedForTesting(false);
  authorization->SetDeviceReauthRequiredForTesting(true);
  auto unauth_empty = maho_settings_password_helpers::EmptyVaultTrashInCore(
      kSettingsAuthorizationProfile);
  EXPECT_FALSE(unauth_empty->success);
  EXPECT_EQ("reauth_required", unauth_empty->error_code);

  // Empty trash with authorization permanently tombstones all trashed items.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kDelete);
  auto emptied = maho_settings_password_helpers::EmptyVaultTrashInCore(
      kSettingsAuthorizationProfile);
  EXPECT_TRUE(emptied->success);

  auto trash_after_empty = ListVaultItemsFromCore(
      std::nullopt, {}, std::nullopt, 50, /*trash_only=*/true,
      /*favorites_only=*/false);
  ASSERT_TRUE(trash_after_empty->success);
  EXPECT_TRUE(trash_after_empty->items.empty());

  auto normal_after_empty = ListVaultItemsFromCore(
      std::nullopt, {}, std::nullopt, 50, /*trash_only=*/false,
      /*favorites_only=*/false);
  ASSERT_TRUE(normal_after_empty->success);
  EXPECT_TRUE(normal_after_empty->items.empty());
}

TEST_F(MahoSettingsVaultHandlerTest, FavoriteSettingAndFilteringWithCas) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);

  auto login1 = AddVaultLoginInCore("Fav 1", {"https://fav1.example"},
                                    "user1@example.test", kSentinel);
  ASSERT_TRUE(login1->success);
  ASSERT_TRUE(login1->item);
  EXPECT_FALSE(login1->item->favorite);

  auto login2 = AddVaultLoginInCore("Fav 2", {"https://fav2.example"},
                                    "user2@example.test", kSentinel);
  ASSERT_TRUE(login2->success);

  // 1. Unauthorized favorite mutation fails.
  auto unauth = maho_settings_password_helpers::SetVaultItemFavoriteInCore(
      kSettingsAuthorizationProfile, login1->item->id, login1->item->revision,
      true);
  EXPECT_FALSE(unauth->success);
  EXPECT_EQ("reauth_required", unauth->error_code);

  // 2. CAS revision conflict with full uint64 revision.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  auto conflict = maho_settings_password_helpers::SetVaultItemFavoriteInCore(
      kSettingsAuthorizationProfile, login1->item->id,
      std::numeric_limits<uint64_t>::max(), true);
  EXPECT_FALSE(conflict->success);
  EXPECT_EQ("revision_conflict", conflict->error_code);

  // 3. Authorized favorite mutation succeeds.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  auto favorited = maho_settings_password_helpers::SetVaultItemFavoriteInCore(
      kSettingsAuthorizationProfile, login1->item->id, login1->item->revision,
      true);
  ASSERT_TRUE(favorited->success);
  ASSERT_TRUE(favorited->item);
  EXPECT_GT(favorited->item->revision, login1->item->revision);

  // 4. List with favorites_only = true returns only login1.
  auto fav_only = ListVaultItemsFromCore(
      std::nullopt, {}, std::nullopt, 50, /*trash_only=*/false,
      /*favorites_only=*/true);
  ASSERT_TRUE(fav_only->success);
  ASSERT_EQ(1u, fav_only->items.size());
  EXPECT_EQ(login1->item->id, fav_only->items[0]->id);
  EXPECT_TRUE(fav_only->items[0]->favorite);

  // 5. Unfavorite login1.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  auto unfav = maho_settings_password_helpers::SetVaultItemFavoriteInCore(
      kSettingsAuthorizationProfile, login1->item->id, favorited->item->revision,
      false);
  ASSERT_TRUE(unfav->success);
  ASSERT_TRUE(unfav->item);
  EXPECT_FALSE(unfav->item->favorite);

  auto fav_none = ListVaultItemsFromCore(
      std::nullopt, {}, std::nullopt, 50, /*trash_only=*/false,
      /*favorites_only=*/true);
  ASSERT_TRUE(fav_none->success);
  EXPECT_TRUE(fav_none->items.empty());
}

TEST_F(MahoSettingsVaultHandlerTest, LoginNotesAddUpdateClearAndRead) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);

  // 1. Add login with notes.
  auto added = AddVaultLoginInCore(
      "Note Login", {"https://notes.example"}, "user@example.test", kSentinel,
      std::optional<std::string>("Initial login note secret"));
  ASSERT_TRUE(added->success);
  ASSERT_TRUE(added->item);
  auto metadata = ListVaultItemsFromCore(std::nullopt, {}, std::nullopt, 50);
  ASSERT_TRUE(metadata->success);
  ASSERT_EQ(1u, metadata->items.size());
  EXPECT_TRUE(metadata->items[0]->has_notes);

  // 2. Read notes from core.
  auto notes_res = GetVaultItemNotesFromCore(added->item->id);
  EXPECT_TRUE(notes_res.success);
  ASSERT_TRUE(notes_res.notes.has_value());
  EXPECT_EQ("Initial login note secret", *notes_res.notes);
  // The detail/edit surfaces need the stored username, not the masked hint.
  ASSERT_TRUE(notes_res.username.has_value());
  EXPECT_EQ("user@example.test", *notes_res.username);

  // 3. Update login notes.
  auto updated = UpdateVaultLoginInCore(
      added->item->id, added->item->revision, "Note Login",
      {"https://notes.example"}, "user@example.test", std::nullopt,
      std::optional<std::string>("Revised login note secret"));
  ASSERT_TRUE(updated->success) << updated->error_code.value_or("");
  ASSERT_TRUE(updated->item);
  metadata = ListVaultItemsFromCore(std::nullopt, {}, std::nullopt, 50);
  ASSERT_TRUE(metadata->success);
  ASSERT_EQ(1u, metadata->items.size());
  EXPECT_TRUE(metadata->items[0]->has_notes);

  auto updated_notes_res = GetVaultItemNotesFromCore(added->item->id);
  EXPECT_TRUE(updated_notes_res.success);
  ASSERT_TRUE(updated_notes_res.notes.has_value());
  EXPECT_EQ("Revised login note secret", *updated_notes_res.notes);

  // Omitting notes preserves the existing text rather than clearing it.
  auto preserved = UpdateVaultLoginInCore(
      added->item->id, updated->item->revision, "Renamed login",
      {"https://notes.example"}, "user@example.test", std::nullopt);
  ASSERT_TRUE(preserved->success);
  ASSERT_TRUE(preserved->item);
  auto preserved_notes = GetVaultItemNotesFromCore(added->item->id);
  ASSERT_TRUE(preserved_notes.success);
  ASSERT_TRUE(preserved_notes.notes);
  EXPECT_EQ("Revised login note secret", *preserved_notes.notes);

  // 4. Clear notes by passing empty string.
  auto cleared = UpdateVaultLoginInCore(
      added->item->id, preserved->item->revision, "Note Login",
      {"https://notes.example"}, "user@example.test", std::nullopt,
      std::optional<std::string>(""));
  ASSERT_TRUE(cleared->success);
  ASSERT_TRUE(cleared->item);
  metadata = ListVaultItemsFromCore(std::nullopt, {}, std::nullopt, 50);
  ASSERT_TRUE(metadata->success);
  ASSERT_EQ(1u, metadata->items.size());
  EXPECT_FALSE(metadata->items[0]->has_notes);

  auto cleared_notes_res = GetVaultItemNotesFromCore(added->item->id);
  EXPECT_TRUE(cleared_notes_res.success);
  ASSERT_TRUE(cleared_notes_res.notes.has_value());
  EXPECT_TRUE(cleared_notes_res.notes->empty());
}

TEST_F(MahoSettingsVaultHandlerTest, SecureNoteAddUpdateAndNotesIsolation) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);

  // 1. Unauthorized add fails.
  auto unauth_add = maho_settings_password_helpers::AddVaultSecureNoteInCore(
      kSettingsAuthorizationProfile, "Confidential note",
      "High security secret body");
  EXPECT_FALSE(unauth_add->success);
  EXPECT_EQ("reauth_required", unauth_add->error_code);

  // 2. Authorized add succeeds.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kAdd);
  auto added = maho_settings_password_helpers::AddVaultSecureNoteInCore(
      kSettingsAuthorizationProfile, "Confidential note",
      "High security secret body");
  ASSERT_TRUE(added->success);
  ASSERT_TRUE(added->item);
  auto metadata = ListVaultItemsFromCore(std::nullopt, {}, std::nullopt, 50);
  ASSERT_TRUE(metadata->success);
  ASSERT_EQ(1u, metadata->items.size());
  EXPECT_EQ("Confidential note", metadata->items[0]->title);
  EXPECT_EQ(maho_settings::mojom::VaultItemKind::kSecureItem,
            metadata->items[0]->item_kind);
  EXPECT_TRUE(metadata->items[0]->has_notes);
  EXPECT_NE("High security secret body", added->item->title);
  EXPECT_NE("High security secret body", added->item->username_hint);

  // 3. Read notes via GetVaultItemNotesFromCore.
  auto read_notes = GetVaultItemNotesFromCore(added->item->id);
  EXPECT_TRUE(read_notes.success);
  ASSERT_TRUE(read_notes.notes.has_value());
  EXPECT_EQ("High security secret body", *read_notes.notes);

  // 4. Unauthorized update fails.
  auto unauth_update =
      maho_settings_password_helpers::UpdateVaultSecureNoteInCore(
          kSettingsAuthorizationProfile, added->item->id, added->item->revision,
          "Updated title", "Updated secret body");
  EXPECT_FALSE(unauth_update->success);
  EXPECT_EQ("reauth_required", unauth_update->error_code);

  // 5. CAS revision conflict on update.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  auto conflict = maho_settings_password_helpers::UpdateVaultSecureNoteInCore(
      kSettingsAuthorizationProfile, added->item->id,
      std::numeric_limits<uint64_t>::max(), "Updated title",
      "Updated secret body");
  EXPECT_FALSE(conflict->success);
  EXPECT_EQ("revision_conflict", conflict->error_code);

  // 6. Authorized update succeeds.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  auto updated = maho_settings_password_helpers::UpdateVaultSecureNoteInCore(
      kSettingsAuthorizationProfile, added->item->id, added->item->revision,
      "Updated title", "Updated secret body");
  ASSERT_TRUE(updated->success);
  ASSERT_TRUE(updated->item);
  metadata = ListVaultItemsFromCore(std::nullopt, {}, std::nullopt, 50);
  ASSERT_TRUE(metadata->success);
  ASSERT_EQ(1u, metadata->items.size());
  EXPECT_EQ("Updated title", metadata->items[0]->title);
  EXPECT_GT(updated->item->revision, added->item->revision);

  auto read_updated = GetVaultItemNotesFromCore(added->item->id);
  EXPECT_TRUE(read_updated.success);
  ASSERT_TRUE(read_updated.notes.has_value());
  EXPECT_EQ("Updated secret body", *read_updated.notes);

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kDelete);
  auto trashed = maho_settings_password_helpers::TrashVaultItemInCore(
      kSettingsAuthorizationProfile, added->item->id, updated->item->revision);
  ASSERT_TRUE(trashed->success);
  auto trashed_notes = GetVaultItemNotesFromCore(added->item->id);
  EXPECT_FALSE(trashed_notes.success);
  EXPECT_FALSE(trashed_notes.notes);
  EXPECT_EQ("not_found", trashed_notes.error_code);
}

TEST_F(MahoSettingsVaultHandlerTest, TotpSetReadLifecycleAndTrashedExclusion) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);

  auto added = AddVaultLoginInCore("Authenticator item",
                                   {"https://totp.example"},
                                   "user@example.test", kSentinel);
  ASSERT_TRUE(added->success);
  ASSERT_TRUE(added->item);
  EXPECT_FALSE(added->item->has_totp);

  // 1. Unauthorized TOTP set fails.
  auto unauth = maho_settings_password_helpers::SetVaultLoginTotpInCore(
      kSettingsAuthorizationProfile, added->item->id, added->item->revision,
      "JBSWY3DPEHPK3PXP");
  EXPECT_FALSE(unauth->success);
  EXPECT_EQ("reauth_required", unauth->error_code);

  // 2. CAS revision conflict on TOTP set.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  auto conflict = maho_settings_password_helpers::SetVaultLoginTotpInCore(
      kSettingsAuthorizationProfile, added->item->id,
      std::numeric_limits<uint64_t>::max(), "JBSWY3DPEHPK3PXP");
  EXPECT_FALSE(conflict->success);
  EXPECT_EQ("revision_conflict", conflict->error_code);

  // 3. Authorized TOTP set succeeds.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  auto set_totp = maho_settings_password_helpers::SetVaultLoginTotpInCore(
      kSettingsAuthorizationProfile, added->item->id, added->item->revision,
      "JBSWY3DPEHPK3PXP");
  ASSERT_TRUE(set_totp->success);
  ASSERT_TRUE(set_totp->item);
  auto metadata = ListVaultItemsFromCore(std::nullopt, {}, std::nullopt, 50);
  ASSERT_TRUE(metadata->success);
  ASSERT_EQ(1u, metadata->items.size());
  EXPECT_TRUE(metadata->items[0]->has_totp);
  EXPECT_GT(set_totp->item->revision, added->item->revision);

  // 4. Read TOTP code.
  auto code_res = GetVaultTotpCodeFromCore(added->item->id);
  ASSERT_TRUE(code_res->success);
  ASSERT_TRUE(code_res->code.has_value());
  EXPECT_EQ(6u, code_res->code->size());
  for (char c : *code_res->code) {
    EXPECT_TRUE(c >= '0' && c <= '9');
  }
  EXPECT_GT(code_res->seconds_remaining, 0u);
  EXPECT_LE(code_res->seconds_remaining, 30u);
  EXPECT_EQ(30u, code_res->period);

  // 5. Trashed item excludes TOTP generation.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kDelete);
  auto trashed = maho_settings_password_helpers::TrashVaultItemInCore(
      kSettingsAuthorizationProfile, added->item->id, set_totp->item->revision);
  ASSERT_TRUE(trashed->success);

  auto trashed_totp = GetVaultTotpCodeFromCore(added->item->id);
  EXPECT_FALSE(trashed_totp->success);
  EXPECT_EQ("not_found", trashed_totp->error_code);
}

TEST_F(MahoSettingsVaultHandlerTest, PasswordGenerationAndStrengthEvaluation) {
  // 1. Password strength estimation works without unlock.
  auto weak = EstimatePasswordStrengthInCore("123456");
  EXPECT_LE(weak.first, 1u);
  EXPECT_GE(weak.second, 0.0);

  auto strong = EstimatePasswordStrengthInCore(
      "Tr0ub4dor&3#CorrectHorseBatteryStaple99!");
  EXPECT_GE(strong.first, 3u);
  EXPECT_GT(strong.second, 40.0);

  // 2. Generate password mode.
  maho_settings::mojom::PasswordGeneratorOptions options;
  options.mode = "password";
  options.length = 24;
  options.include_lowercase = true;
  options.include_uppercase = true;
  options.include_digits = true;
  options.include_symbols = true;
  options.avoid_ambiguous = false;

  auto gen = GeneratePasswordInCore(options);
  ASSERT_TRUE(gen->success);
  ASSERT_TRUE(gen->password.has_value());
  EXPECT_EQ(24u, gen->password->size());
  EXPECT_LE(gen->strength_score, 4u);
  EXPECT_EQ(EstimatePasswordStrengthInCore(*gen->password).first,
            gen->strength_score);
  EXPECT_NE(std::string::npos, gen->password->find_first_of("abcdefghijklmnopqrstuvwxyz"));
  EXPECT_NE(std::string::npos, gen->password->find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ"));
  EXPECT_NE(std::string::npos, gen->password->find_first_of("0123456789"));
  EXPECT_NE(std::string::npos, gen->password->find_first_not_of(
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"));

  // 3. Generate passphrase mode.
  maho_settings::mojom::PasswordGeneratorOptions passphrase_opts;
  passphrase_opts.mode = "passphrase";
  passphrase_opts.word_count = 5;
  passphrase_opts.separator = "-";
  passphrase_opts.capitalize = true;
  passphrase_opts.include_number = true;

  auto gen_passphrase = GeneratePasswordInCore(passphrase_opts);
  ASSERT_TRUE(gen_passphrase->success);
  ASSERT_TRUE(gen_passphrase->password.has_value());
  EXPECT_FALSE(gen_passphrase->password->empty());

  // 4. Invalid options rejection.
  options.length = 129;
  auto invalid_len = GeneratePasswordInCore(options);
  EXPECT_FALSE(invalid_len->success);

  passphrase_opts.word_count = 13;
  auto invalid_words = GeneratePasswordInCore(passphrase_opts);
  EXPECT_FALSE(invalid_words->success);
}

TEST_F(MahoSettingsVaultHandlerTest,
       VaultHealthReportIdentifiesWeakReusedAndExcludesTrashed) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);

  auto login1 = AddVaultLoginInCore("Weak One", {"https://weak1.example"},
                                    "u1@example.test", "password");
  ASSERT_TRUE(login1->success);
  ASSERT_TRUE(login1->item);

  auto login2 = AddVaultLoginInCore("Weak Two", {"https://weak2.example"},
                                    "u2@example.test", "password");
  ASSERT_TRUE(login2->success);
  ASSERT_TRUE(login2->item);

  auto login3 = AddVaultLoginInCore(
      "Strong Three", {"https://strong3.example"}, "u3@example.test",
      "Tr0ub4dor&3#CorrectHorseBatteryStaple99!");
  ASSERT_TRUE(login3->success);
  ASSERT_TRUE(login3->item);

  // 1. Initial health report has 3 logins, 2 weak, 1 reused group with 2 items.
  auto report = GetVaultHealthReportFromCore();
  ASSERT_TRUE(report->success);
  EXPECT_EQ(3u, report->total_logins);
  EXPECT_EQ(2u, report->weak_item_ids.size());
  ASSERT_EQ(1u, report->reused_groups.size());
  EXPECT_EQ(2u, report->reused_groups[0].size());

  // 2. Trash login2. Trashed items must be completely excluded from health audit.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kDelete);
  auto trashed = maho_settings_password_helpers::TrashVaultItemInCore(
      kSettingsAuthorizationProfile, login2->item->id, login2->item->revision);
  ASSERT_TRUE(trashed->success);

  auto report_after_trash = GetVaultHealthReportFromCore();
  ASSERT_TRUE(report_after_trash->success);
  EXPECT_EQ(2u, report_after_trash->total_logins);
  EXPECT_EQ(1u, report_after_trash->weak_item_ids.size());
  EXPECT_EQ(login1->item->id, report_after_trash->weak_item_ids[0]);
  EXPECT_TRUE(report_after_trash->reused_groups.empty());
}

TEST_F(MahoSettingsVaultHandlerTest,
       SecretActionRevealCallbackExecutesWithoutClipboardLeak) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);

  auto added = AddVaultLoginInCore("Secret item", {"https://reveal.example"},
                                   "user@example.test", kSentinel);
  ASSERT_TRUE(added->success);
  ASSERT_TRUE(added->item);

  // 1. Unauthorized reveal fails with reauth_required.
  auto unauth = maho_settings_password_helpers::UseVaultSecretInCore(
      kSettingsAuthorizationProfile, added->item->id, added->item->revision,
      maho_settings::mojom::SecretAction::kReveal,
      base::BindOnce([](const char*) { return true; }));
  EXPECT_FALSE(unauth->success);
  EXPECT_EQ("reauth_required", unauth->error_code);

  base::test::TestFuture<std::u16string> initial_clipboard_future;
  ui::Clipboard::GetForCurrentThread()->ReadText(
      ui::ClipboardBuffer::kCopyPaste, /*data_dst=*/std::nullopt,
      initial_clipboard_future.GetCallback());
  const std::u16string initial_clipboard = initial_clipboard_future.Get();

  // 2. Authorized reveal invokes callback with plaintext, but does not touch clipboard.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kCopy);
  std::string revealed_secret;
  auto revealed = maho_settings_password_helpers::UseVaultSecretInCore(
      kSettingsAuthorizationProfile, added->item->id, added->item->revision,
      maho_settings::mojom::SecretAction::kReveal,
      base::BindOnce(
          [](std::string* out, const char* secret) {
            if (secret) {
              *out = secret;
            }
            return true;
          },
          &revealed_secret));
  ASSERT_TRUE(revealed->success);
  EXPECT_EQ(kSentinel, revealed_secret);

  base::test::TestFuture<std::u16string> after_clipboard_future;
  ui::Clipboard::GetForCurrentThread()->ReadText(
      ui::ClipboardBuffer::kCopyPaste, /*data_dst=*/std::nullopt,
      after_clipboard_future.GetCallback());
  const std::u16string after_clipboard = after_clipboard_future.Get();
  EXPECT_EQ(initial_clipboard, after_clipboard);
  EXPECT_EQ(std::string::npos,
            base::UTF16ToUTF8(after_clipboard).find(kSentinel));
  auto current = ListVaultItemsFromCore(std::nullopt, {}, std::nullopt, 50);
  ASSERT_TRUE(current->success);
  ASSERT_EQ(1u, current->items.size());

  // 3. Callback returning false reports action_failed.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kCopy);
  auto rejected = maho_settings_password_helpers::UseVaultSecretInCore(
      kSettingsAuthorizationProfile, added->item->id, current->items[0]->revision,
      maho_settings::mojom::SecretAction::kReveal,
      base::BindOnce([](const char*) { return false; }));
  EXPECT_FALSE(rejected->success);
  EXPECT_EQ("action_failed", rejected->error_code);
  current = ListVaultItemsFromCore(std::nullopt, {}, std::nullopt, 50);
  ASSERT_TRUE(current->success);
  ASSERT_EQ(1u, current->items.size());

  // 4. Trashed item secret cannot be revealed.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kDelete);
  auto trashed = maho_settings_password_helpers::TrashVaultItemInCore(
      kSettingsAuthorizationProfile, added->item->id, current->items[0]->revision);
  ASSERT_TRUE(trashed->success);

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kCopy);
  auto trashed_reveal = maho_settings_password_helpers::UseVaultSecretInCore(
      kSettingsAuthorizationProfile, added->item->id, trashed->item->revision,
      maho_settings::mojom::SecretAction::kReveal,
      base::BindOnce([](const char*) { return true; }));
  EXPECT_FALSE(trashed_reveal->success);
  EXPECT_EQ("secret_unavailable", trashed_reveal->error_code);
}

TEST_F(MahoSettingsVaultHandlerTest,
       LockedVaultDeniesAllNewVaultOperations) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);

  auto login = AddVaultLoginInCore("Account", {"https://example.test"},
                                   "user@example.test", kSentinel);
  ASSERT_TRUE(login->success);
  ASSERT_TRUE(login->item);

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kAdd);
  auto note = maho_settings_password_helpers::AddVaultSecureNoteInCore(
      kSettingsAuthorizationProfile, "Note title", "Note content");
  ASSERT_TRUE(note->success);
  ASSERT_TRUE(note->item);

  // Lock the vault.
  ASSERT_TRUE(LockVaultInCore()->success);

  // Verify all data helpers return "locked" error code.
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kDelete);
  EXPECT_EQ("locked", maho_settings_password_helpers::TrashVaultItemInCore(
                          kSettingsAuthorizationProfile, login->item->id,
                          login->item->revision)
                          ->error_code);

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  EXPECT_EQ("locked", maho_settings_password_helpers::RestoreVaultItemInCore(
                          kSettingsAuthorizationProfile, login->item->id,
                          login->item->revision)
                          ->error_code);

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kDelete);
  EXPECT_EQ("locked", maho_settings_password_helpers::EmptyVaultTrashInCore(
                          kSettingsAuthorizationProfile)
                          ->error_code);

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  EXPECT_EQ("locked",
            maho_settings_password_helpers::SetVaultItemFavoriteInCore(
                kSettingsAuthorizationProfile, login->item->id,
                login->item->revision, true)
                ->error_code);

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kAdd);
  EXPECT_EQ("locked", maho_settings_password_helpers::AddVaultSecureNoteInCore(
                          kSettingsAuthorizationProfile, "Locked note", "Body")
                          ->error_code);

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  EXPECT_EQ("locked",
            maho_settings_password_helpers::UpdateVaultSecureNoteInCore(
                kSettingsAuthorizationProfile, note->item->id,
                note->item->revision, "New title", "New body")
                ->error_code);

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  EXPECT_EQ("locked", maho_settings_password_helpers::SetVaultLoginTotpInCore(
                          kSettingsAuthorizationProfile, login->item->id,
                          login->item->revision, "JBSWY3DPEHPK3PXP")
                          ->error_code);

  EXPECT_EQ("locked", GetVaultItemNotesFromCore(login->item->id).error_code);
  EXPECT_EQ("locked", GetVaultTotpCodeFromCore(login->item->id)->error_code);
  EXPECT_EQ("locked", GetVaultHealthReportFromCore()->error_code);

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kCopy);
  EXPECT_EQ("locked",
            maho_settings_password_helpers::UseVaultSecretInCore(
                kSettingsAuthorizationProfile, login->item->id,
                login->item->revision,
                maho_settings::mojom::SecretAction::kReveal,
                base::BindOnce([](const char*) { return true; }))
                ->error_code);

  // Generator and strength evaluation still succeed while locked.
  maho_settings::mojom::PasswordGeneratorOptions options;
  options.length = 16;
  auto gen = GeneratePasswordInCore(options);
  EXPECT_TRUE(gen->success);

  auto strength = EstimatePasswordStrengthInCore("password-candidate");
  EXPECT_GE(strength.first, 0u);
}

TEST_F(MahoSettingsVaultHandlerTest,
       FullUint64RevisionDecimalRoundTripCAS) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);

  auto login = AddVaultLoginInCore("CAS item", {"https://cas.example"},
                                   "cas@example.test", kSentinel);
  ASSERT_TRUE(login->success);
  ASSERT_TRUE(login->item);

  const uint64_t full_uint64_max = std::numeric_limits<uint64_t>::max();

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  EXPECT_EQ("revision_conflict",
            maho_settings_password_helpers::SetVaultItemFavoriteInCore(
                kSettingsAuthorizationProfile, login->item->id, full_uint64_max,
                true)
                ->error_code);

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kDelete);
  EXPECT_EQ("revision_conflict",
            maho_settings_password_helpers::TrashVaultItemInCore(
                kSettingsAuthorizationProfile, login->item->id, full_uint64_max)
                ->error_code);

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  EXPECT_EQ("revision_conflict",
            maho_settings_password_helpers::RestoreVaultItemInCore(
                kSettingsAuthorizationProfile, login->item->id, full_uint64_max)
                ->error_code);

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  EXPECT_EQ("revision_conflict",
            maho_settings_password_helpers::SetVaultLoginTotpInCore(
                kSettingsAuthorizationProfile, login->item->id, full_uint64_max,
                "JBSWY3DPEHPK3PXP")
                ->error_code);

  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  EXPECT_EQ("revision_conflict",
            maho_settings_password_helpers::UpdateVaultSecureNoteInCore(
                kSettingsAuthorizationProfile, login->item->id, full_uint64_max,
                "Title", "Notes")
                ->error_code);
}

TEST_F(MahoSettingsVaultHandlerTest,
       SavedPasswordLibraryAdapterListsAndSearchesMetadataOnly) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);
  auto added = AddSavedPasswordLibraryLoginInCore(
      "Library account", {"https://library.example"},
      "person@example.test", kSentinel);
  ASSERT_TRUE(added->success);
  ASSERT_TRUE(added->item);

  auto listed = ListSavedPasswordLibraryItemsFromCore(std::nullopt,
                                                      std::nullopt, 50);
  auto searched = SearchSavedPasswordLibraryItemsFromCore("library.example",
                                                          std::nullopt);

  ASSERT_TRUE(listed->success);
  ASSERT_TRUE(searched->success);
  ASSERT_EQ(1u, listed->items.size());
  ASSERT_EQ(1u, searched->items.size());
  for (const auto& item : {listed->items[0].get(), searched->items[0].get()}) {
    SCOPED_TRACE(item->id);
    EXPECT_EQ(added->item->id, item->id);
    EXPECT_EQ(maho_settings::mojom::VaultItemKind::kLogin, item->item_kind);
    EXPECT_NE(kSentinel, item->id);
    EXPECT_NE(kSentinel, item->title);
    EXPECT_NE(kSentinel, item->username_hint);
    ASSERT_EQ(1u, item->origins.size());
    EXPECT_NE(kSentinel, item->origins[0]);
    EXPECT_FALSE(item->has_totp);
    EXPECT_FALSE(item->has_passkey);
  }
}

TEST_F(MahoSettingsVaultHandlerTest,
       SavedPasswordLibraryAdapterCrudRoutesThroughVault) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);
  auto added = AddSavedPasswordLibraryLoginInCore(
      "Original", {"https://crud.example"}, "first@example.test",
      kSentinel);
  ASSERT_TRUE(added->success);
  ASSERT_TRUE(added->item);

  auto updated = UpdateSavedPasswordLibraryLoginInCore(
      added->item->id, added->item->revision, "Updated",
      {"https://crud.example"}, "second@example.test", std::nullopt);
  ASSERT_TRUE(updated->success);
  ASSERT_TRUE(updated->item);
  EXPECT_GT(updated->item->revision, added->item->revision);
  auto after_update = ListSavedPasswordLibraryItemsFromCore(std::nullopt,
                                                            std::nullopt, 50);
  ASSERT_TRUE(after_update->success);
  ASSERT_EQ(1u, after_update->items.size());
  EXPECT_EQ("Updated", after_update->items[0]->title);

  auto deleted = DeleteSavedPasswordLibraryItemInCore(updated->item->id,
                                                      updated->item->revision);
  ASSERT_TRUE(deleted->success);
  ASSERT_TRUE(deleted->item);
  auto listed = ListSavedPasswordLibraryItemsFromCore(std::nullopt,
                                                      std::nullopt, 50);
  ASSERT_TRUE(listed->success);
  EXPECT_TRUE(listed->items.empty());
}

TEST_F(MahoSettingsVaultHandlerTest,
       SavedPasswordLibrarySecretUseReturnsStatusOnly) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);
  auto added = AddSavedPasswordLibraryLoginInCore(
      "Secret action", {"https://secret.example"}, "person@example.test",
      kSentinel);
  ASSERT_TRUE(added->success);
  ASSERT_TRUE(added->item);

  auto used = UseSavedPasswordLibrarySecretInCore(
      added->item->id, added->item->revision,
      maho_settings::mojom::SecretAction::kCopy);

  ASSERT_TRUE(used->success);
  EXPECT_FALSE(used->status);
  EXPECT_FALSE(used->item);
  EXPECT_FALSE(used->error_code);
  auto listed = ListSavedPasswordLibraryItemsFromCore(std::nullopt,
                                                      std::nullopt, 50);
  ASSERT_TRUE(listed->success);
  ASSERT_EQ(1u, listed->items.size());
  EXPECT_GT(listed->items[0]->revision, added->item->revision);
  EXPECT_NE(kSentinel, listed->items[0]->title);
  EXPECT_NE(kSentinel, listed->items[0]->username_hint);
}

TEST_F(MahoSettingsVaultHandlerTest,
       LegacySavedPasswordMethodsAreCompatibilityShimsOverVault) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);
  ASSERT_TRUE(AddSavedPasswordCompatibilityShimInCore(
      "legacy.example", "legacy@example.test", kSentinel));

  auto saved = GetSavedPasswordsFromCore();
  ASSERT_EQ(1u, saved.size());
  EXPECT_EQ("https://legacy.example", saved[0]->domain);
  EXPECT_NE("legacy@example.test", saved[0]->username);
  EXPECT_NE(kSentinel, saved[0]->domain);
  EXPECT_NE(kSentinel, saved[0]->username);

  ASSERT_TRUE(UpdateSavedPasswordUsernameCompatibilityShimInCore(
      saved[0]->id, "renamed@example.test"));
  auto searched = SearchSavedPasswordsFromCore("legacy.example");
  ASSERT_EQ(1u, searched.size());
  EXPECT_NE("renamed@example.test", searched[0]->username);

  ASSERT_TRUE(DeleteSavedPasswordCompatibilityShimInCore(searched[0]->id));
  EXPECT_TRUE(GetSavedPasswordsFromCore().empty());
}

TEST_F(MahoSettingsVaultHandlerTest, ProviderSelectionAvailabilityMatrix) {
  using Provider = maho_settings::mojom::PasswordProviderKind;
  struct ProviderCase {
    std::string_view selected;
    bool bitwarden_available;
    bool one_password_available;
    Provider expected_selected;
    Provider expected_effective;
    bool expected_selected_available;
  };
  constexpr ProviderCase kCases[] = {
      {"maho_native", false, false, Provider::kMahoNative,
       Provider::kMahoNative, true},
      {"bitwarden", false, false, Provider::kBitwarden, Provider::kDisabled,
       false},
      {"bitwarden", false, true, Provider::kBitwarden, Provider::kDisabled,
       false},
      {"bitwarden", true, false, Provider::kBitwarden, Provider::kBitwarden,
       true},
      {"bitwarden", true, true, Provider::kBitwarden, Provider::kBitwarden,
       true},
      {"onepassword", false, false, Provider::kOnePassword,
       Provider::kDisabled, false},
      {"onepassword", true, false, Provider::kOnePassword,
       Provider::kDisabled, false},
      {"onepassword", false, true, Provider::kOnePassword,
       Provider::kOnePassword, true},
      {"onepassword", true, true, Provider::kOnePassword,
       Provider::kOnePassword, true},
  };

  for (const ProviderCase& test_case : kCases) {
    SCOPED_TRACE(test_case.selected);
    auto status = BuildVaultProviderStatusForTesting(
        std::string(test_case.selected), test_case.bitwarden_available,
        test_case.one_password_available);

    EXPECT_EQ(test_case.expected_selected, status->selected_provider);
    EXPECT_EQ(test_case.expected_effective, status->effective_provider);
    EXPECT_EQ(test_case.expected_selected_available,
              status->selected_provider_is_available);
    ASSERT_EQ(3u, status->providers.size());
    EXPECT_TRUE(status->providers[0]->is_available);
    EXPECT_EQ(test_case.bitwarden_available,
              status->providers[1]->is_available);
    EXPECT_EQ(test_case.one_password_available,
              status->providers[2]->is_available);
    EXPECT_TRUE(status->providers[0]->capabilities->can_list_saved_passwords);
    EXPECT_FALSE(status->providers[1]->capabilities->can_list_saved_passwords);
    EXPECT_FALSE(status->providers[2]->capabilities->can_list_saved_passwords);
  }
}

TEST_F(MahoSettingsVaultHandlerTest,
       AutoLockedRateLimitedStatusProjectsCanonicalFields) {
  auto result = ParseVaultOperationResultJson(R"({
    "ok": true,
    "data": {
      "lockState": "auto_locked",
      "selectedProvider": "onepassword",
      "effectiveProvider": "maho_native",
      "itemCount": 7,
      "agentPolicyDefault": "ask_every_use",
      "autoLockMinutes": 15,
      "failedUnlockCount": 5,
      "retryAt": "2026-01-01T00:00:00Z"
    }
  })");

  ASSERT_TRUE(result->success);
  ASSERT_TRUE(result->status);
  EXPECT_EQ(maho_settings::mojom::VaultLockState::kAutoLocked,
            result->status->lock_state);
  EXPECT_EQ(maho_settings::mojom::PasswordProviderKind::kOnePassword,
            result->status->selected_provider);
  EXPECT_EQ(maho_settings::mojom::PasswordProviderKind::kMahoNative,
            result->status->effective_provider);
  EXPECT_EQ(5u, result->status->failed_unlock_count);
  ASSERT_TRUE(result->status->retry_at_timestamp);
  EXPECT_EQ(1767225600000LL, *result->status->retry_at_timestamp);
}

TEST_F(MahoSettingsVaultHandlerTest, CanonicalAbiErrorsRemainDistinct) {
  constexpr std::string_view kErrorCodes[] = {
      "invalid_credentials",    "rate_limited",
      "provider_unavailable",   "unsupported_schema_version",
      "revision_conflict",      "invalid_origin",
      "invalid_grant",          "grant_expired",
      "grant_revoked",          "grant_exhausted",
      "secret_field_forbidden", "not_found",
      "storage_failure",
  };

  for (std::string_view error_code : kErrorCodes) {
    SCOPED_TRACE(error_code);
    const std::string json = "{\"ok\":false,\"error\":{\"code\":\"" +
                             std::string(error_code) + "\"}}";
    auto result = ParseVaultOperationResultJson(json);

    EXPECT_FALSE(result->success);
    EXPECT_EQ(error_code, result->error_code);
    EXPECT_EQ("Vault operation failed.", result->error_message);
    EXPECT_FALSE(result->status);
    EXPECT_FALSE(result->item);
  }
}

TEST_F(MahoSettingsVaultHandlerTest, MalformedStatusResponsesFailClosed) {
  constexpr std::string_view kMalformedResponses[] = {
      "not-json",
      R"({"ok":true})",
      R"({"ok":false,"error":{}})",
      R"({"ok":true,"data":{"lockState":"unknown","selectedProvider":"maho_native","effectiveProvider":"maho_native","itemCount":0,"agentPolicyDefault":"deny","autoLockMinutes":15,"failedUnlockCount":0,"retryAt":null}})",
      R"({"ok":true,"data":{"lockState":"locked","selectedProvider":"unknown","effectiveProvider":"maho_native","itemCount":0,"agentPolicyDefault":"deny","autoLockMinutes":15,"failedUnlockCount":0,"retryAt":null}})",
      R"({"ok":true,"data":{"lockState":"locked","selectedProvider":"maho_native","effectiveProvider":"maho_native","itemCount":0,"agentPolicyDefault":"unknown","autoLockMinutes":15,"failedUnlockCount":0,"retryAt":null}})",
      R"({"ok":true,"data":{"lockState":"locked","selectedProvider":"maho_native","effectiveProvider":"maho_native","itemCount":0.5,"agentPolicyDefault":"deny","autoLockMinutes":15,"failedUnlockCount":0,"retryAt":null}})",
      R"({"ok":true,"data":{"lockState":"locked","selectedProvider":"maho_native","effectiveProvider":"maho_native","itemCount":0,"agentPolicyDefault":"deny","autoLockMinutes":15,"failedUnlockCount":0,"retryAt":"not-a-time"}})",
  };

  for (std::string_view response : kMalformedResponses) {
    SCOPED_TRACE(response);
    auto result = ParseVaultOperationResultJson(std::string(response));
    EXPECT_FALSE(result->success);
    EXPECT_EQ("invalid_response", result->error_code);
    EXPECT_FALSE(result->status);
    EXPECT_FALSE(result->item);
  }
}

TEST_F(MahoSettingsVaultHandlerTest, MalformedItemListResponsesFailClosed) {
  constexpr std::string_view kMalformedResponses[] = {
      R"({"ok":true,"data":{}})",
      R"({"ok":true,"data":[null]})",
      R"({"ok":true,"data":[{"id":"item-1"}]})",
      R"({"ok":true,"data":[{"id":"item-1","revision":1,"provider":"unknown","itemKind":"login","title":"Example","origins":["https://example.test"],"usernameHint":"p***@example.test","createdAt":"2026-07-24T00:00:00Z","updatedAt":"2026-07-24T00:00:00Z","lastUsedAt":null,"totp":null,"passkey":null}]})",
      R"({"ok":true,"data":[{"id":"item-1","revision":1.5,"provider":"maho_native","itemKind":"login","title":"Example","origins":["https://example.test"],"usernameHint":"p***@example.test","createdAt":"2026-07-24T00:00:00Z","updatedAt":"2026-07-24T00:00:00Z","lastUsedAt":null,"totp":null,"passkey":null}]})",
  };

  for (std::string_view response : kMalformedResponses) {
    SCOPED_TRACE(response);
    auto result = ParseVaultItemListResultJson(std::string(response));
    EXPECT_FALSE(result->success);
    EXPECT_TRUE(result->items.empty());
    EXPECT_EQ("invalid_response", result->error_code);
  }
}

TEST_F(MahoSettingsVaultHandlerTest,
       CoreUnavailableAndLocalValidationErrorsAreExplicit) {
  {
    ScopedCoreOverride no_core(nullptr);
    auto status = GetVaultStatusFromCore();
    auto list = ListVaultItemsFromCore(std::nullopt, {}, std::nullopt, 50);
    EXPECT_FALSE(status->success);
    EXPECT_EQ("core_unavailable", status->error_code);
    EXPECT_FALSE(list->success);
    EXPECT_EQ("core_unavailable", list->error_code);
  }

  auto invalid_limit = ListVaultItemsFromCore(
      std::nullopt, {}, std::nullopt, std::numeric_limits<uint32_t>::max());
  EXPECT_FALSE(invalid_limit->success);
  EXPECT_EQ("invalid_limit", invalid_limit->error_code);

  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);
  auto added = AddVaultLoginInCore("Example account", {"https://example.test"},
                                   "person@example.test", kSentinel);
  ASSERT_TRUE(added->success);
  ASSERT_TRUE(added->item);
  const std::string item_id = added->item->id;

  auto conflict_update =
      UpdateVaultLoginInCore(item_id, std::numeric_limits<uint64_t>::max(),
                             "Title", {}, "user", std::nullopt);
  EXPECT_FALSE(conflict_update->success);
  EXPECT_EQ("revision_conflict", conflict_update->error_code);

  auto conflict_delete =
      DeleteVaultItemInCore(item_id, std::numeric_limits<uint64_t>::max());
  EXPECT_FALSE(conflict_delete->success);
  EXPECT_EQ("revision_conflict", conflict_delete->error_code);
}

TEST_F(MahoSettingsVaultHandlerTest, SecretBearingBridgeItemIsRejected) {
  auto result = ParseVaultItemListResultJson(R"({
    "ok": true,
    "data": [{
      "schemaVersion": 1,
      "id": "item-1",
      "revision": 1,
      "provider": "maho_native",
      "itemKind": "login",
      "title": "Example",
      "origins": ["https://example.test"],
      "usernameHint": "p***@example.test",
      "createdAt": "2026-07-24T00:00:00Z",
      "updatedAt": "2026-07-24T00:00:00Z",
      "lastUsedAt": null,
      "totp": null,
      "passkey": null,
      "password": "S3NTINEL-maho-vault-9F4C"
    }]
  })");

  EXPECT_FALSE(result->success);
  EXPECT_TRUE(result->items.empty());
  EXPECT_EQ("invalid_response", result->error_code);
}

TEST_F(MahoSettingsVaultHandlerTest, InvalidAuditCursorIsExplicitlyRejected) {
  auto page = GetVaultAuditPageFromCore("unexpected-cursor", 25);

  EXPECT_FALSE(page->is_available);
  EXPECT_TRUE(page->entries.empty());
  EXPECT_EQ("invalid_cursor", page->error_code);
  EXPECT_FALSE(page->next_cursor);
}

TEST_F(MahoSettingsVaultHandlerTest,
       PolicyAndAuditBackendReturnsRealRowsWithoutAliases) {
  auto initial_policy = GetVaultPolicyStatusFromCore();
  ASSERT_TRUE(initial_policy->is_available);
  EXPECT_EQ(maho_settings::mojom::VaultAgentPolicy::kDeny,
            initial_policy->policy);

  auto policy_mutation = SetVaultPolicyInCore(
      maho_settings::mojom::VaultAgentPolicy::kAskEveryUse, std::nullopt,
      std::nullopt, std::nullopt);
  ASSERT_TRUE(policy_mutation->is_available);
  EXPECT_EQ(maho_settings::mojom::VaultAgentPolicy::kAskEveryUse,
            policy_mutation->policy);

  auto audit = GetVaultAuditPageFromCore(std::nullopt, 10);

  ASSERT_TRUE(audit->is_available);
  ASSERT_EQ(1u, audit->entries.size());
  EXPECT_EQ("policy_changed", audit->entries[0]->operation);
  ASSERT_TRUE(audit->entries[0]->policy);
  EXPECT_EQ(maho_settings::mojom::VaultAgentPolicy::kAskEveryUse,
            *audit->entries[0]->policy);
  EXPECT_FALSE(audit->entries[0]->item_alias);
  EXPECT_FALSE(audit->next_cursor);
}

TEST_F(MahoSettingsVaultHandlerTest,
       PolicyAndAuditReportUnavailableWhenCoreIsAbsent) {
  ScopedCoreOverride no_core(nullptr);

  auto policy = GetVaultPolicyStatusFromCore();
  auto policy_mutation = SetVaultPolicyInCore(
      maho_settings::mojom::VaultAgentPolicy::kAlwaysAllow, std::nullopt,
      std::nullopt, std::nullopt);
  auto audit = GetVaultAuditPageFromCore(std::nullopt, 10);

  EXPECT_FALSE(policy->is_available);
  EXPECT_EQ(maho_settings::mojom::VaultAgentPolicy::kDeny, policy->policy);
  EXPECT_EQ("core_unavailable", policy->unavailable_reason);
  EXPECT_FALSE(policy_mutation->is_available);
  EXPECT_EQ(maho_settings::mojom::VaultAgentPolicy::kDeny,
            policy_mutation->policy);
  EXPECT_FALSE(audit->is_available);
  EXPECT_TRUE(audit->entries.empty());
  EXPECT_EQ("core_unavailable", audit->error_code);
  EXPECT_EQ("core_unavailable", audit->unavailable_reason);
}

// ── C5: atc.open_external_links_in_maho_mini single source of truth ──
//
// maho-core is canonical for this setting because it is the only value
// ATCManager::decide_link_destination consults. The Chromium profile pref
// (sidebar_prefs::kOpenExternalLinksInMahoMini) is legacy: it is seeded into
// core exactly once, guarded by a marker pref, and never read again.

// Drives MahoSettingsPageHandler::RunAtcExternalMiniMigration — the actual
// production migration sequence — against a real MahoCore. Only the two pref
// stores are faked, and deliberately with the SHAPE the production code must
// have: exactly one process-global marker cell (local state) shared by every
// profile, plus a per-profile legacy pref. A profile-scoped marker cannot be
// expressed here, which is the point.
class AtcExternalMiniMigrationHarness {
 public:
  explicit AtcExternalMiniMigrationHarness(MahoCore* core) : core_(core) {}

  // Simulates one profile opening Settings. Returns true iff core was seeded.
  bool RunForProfile(bool legacy_profile_pref_value) {
    return MahoSettingsPageHandler::RunAtcExternalMiniMigration(
        core_, [this] { return process_global_marker_; },
        [legacy_profile_pref_value] { return legacy_profile_pref_value; },
        [this] {
          process_global_marker_ = true;
          ++marker_write_count_;
        });
  }

  bool marker() const { return process_global_marker_; }
  int marker_write_count() const { return marker_write_count_; }
  bool core_value() const {
    return maho_core_get_open_external_links_in_maho_mini(core_);
  }

 private:
  MahoCore* const core_;
  // One cell for the whole process == local state, matching production.
  bool process_global_marker_ = false;
  int marker_write_count_ = 0;
};

TEST_F(MahoSettingsVaultHandlerTest, AtcExternalMiniSeedsFromPrefExactlyOnce) {
  ASSERT_NE(core(), nullptr);
  AtcExternalMiniMigrationHarness harness(core());

  // Fresh core defaults to disabled and nothing has migrated yet.
  ASSERT_FALSE(harness.core_value());
  ASSERT_FALSE(harness.marker());

  // Seed-once: marker unset, so the legacy pref value reaches core and the
  // marker is persisted by the migration itself.
  EXPECT_TRUE(harness.RunForProfile(/*legacy_profile_pref_value=*/true));
  EXPECT_TRUE(harness.core_value());
  EXPECT_TRUE(harness.marker());
  EXPECT_EQ(1, harness.marker_write_count());

  // Marker-respected: a second pass with the same stale pref does not re-seed
  // and does not rewrite the marker.
  EXPECT_FALSE(harness.RunForProfile(/*legacy_profile_pref_value=*/true));
  EXPECT_EQ(1, harness.marker_write_count());
}

TEST_F(MahoSettingsVaultHandlerTest, AtcExternalMiniCoreWinsAfterMigration) {
  ASSERT_NE(core(), nullptr);
  AtcExternalMiniMigrationHarness harness(core());

  ASSERT_TRUE(harness.RunForProfile(/*legacy_profile_pref_value=*/true));
  ASSERT_TRUE(harness.core_value());

  // The user turns the setting OFF through the settings surface, which writes
  // core directly (the canonical store).
  maho_core_set_open_external_links_in_maho_mini(core(), false);
  ASSERT_FALSE(harness.core_value());

  // Core-wins: a later migration pass must not resurrect the stale legacy pref.
  EXPECT_FALSE(harness.RunForProfile(/*legacy_profile_pref_value=*/true));
  EXPECT_FALSE(harness.core_value());
}

TEST_F(MahoSettingsVaultHandlerTest,
       AtcExternalMiniMigrationIsProcessGlobalAcrossProfiles) {
  ASSERT_NE(core(), nullptr);
  AtcExternalMiniMigrationHarness harness(core());

  // Profile A migrates first and turns the feature on.
  ASSERT_TRUE(harness.RunForProfile(/*legacy_profile_pref_value=*/true));
  ASSERT_TRUE(harness.core_value());

  // Profile B opens Settings later. Its own legacy pref is still the default
  // false. maho-core is process-global, so if the migration marker were stored
  // per-profile, B's marker would read false, B would seed false, and profile
  // A's (and the user's) setting would be silently destroyed. The marker is
  // process-global precisely to prevent this.
  EXPECT_FALSE(harness.RunForProfile(/*legacy_profile_pref_value=*/false));
  EXPECT_TRUE(harness.core_value());

  // A third profile changes nothing either — exactly one seed per process.
  EXPECT_FALSE(harness.RunForProfile(/*legacy_profile_pref_value=*/false));
  EXPECT_TRUE(harness.core_value());
  EXPECT_EQ(1, harness.marker_write_count());
}

TEST_F(MahoSettingsVaultHandlerTest, AtcExternalMiniMigrationSkipsAbsentCore) {
  // No core yet: nothing is seeded and, critically, the marker is NOT set, so
  // the seed is retried once core exists instead of being lost.
  bool marker = false;
  int marker_writes = 0;
  EXPECT_FALSE(MahoSettingsPageHandler::RunAtcExternalMiniMigration(
      nullptr, [&marker] { return marker; }, [] { return true; },
      [&marker, &marker_writes] {
        marker = true;
        ++marker_writes;
      }));
  EXPECT_FALSE(marker);
  EXPECT_EQ(0, marker_writes);
}

TEST_F(MahoSettingsVaultHandlerTest,
       ConversationAutoArchivePolicyRoundTripsAllowedValues) {
  ASSERT_NE(core(), nullptr);
  EXPECT_EQ(-1, maho_core_get_conversation_auto_archive_policy(core()));

  for (int days : {-1, 3, 7, 30}) {
    SCOPED_TRACE(days);
    EXPECT_TRUE(
        maho_core_set_conversation_auto_archive_policy(core(), days));
    EXPECT_EQ(days, maho_core_get_conversation_auto_archive_policy(core()));
  }

  EXPECT_FALSE(maho_core_set_conversation_auto_archive_policy(core(), 0));
  EXPECT_FALSE(maho_core_set_conversation_auto_archive_policy(core(), -2));
  EXPECT_EQ(30, maho_core_get_conversation_auto_archive_policy(core()));
}

class MahoRoutinesSchedulerTest : public MahoSettingsVaultHandlerTest {};

TEST_F(MahoRoutinesSchedulerTest, StatusTickIntervalIsThirtySeconds) {
  EXPECT_EQ(maho::MahoRoutinesScheduler::kStatusTickInterval,
            base::Seconds(30));
}

TEST(MahoVaultLockStatusJsonParseTest, FailsClosedUnlessExplicitUnlocked) {
  using maho::MahoRoutinesScheduler;
  EXPECT_FALSE(MahoRoutinesScheduler::IsLockedVaultStatusJson(
      R"({"ok":true,"data":{"lockState":"unlocked"}})"));
  EXPECT_TRUE(MahoRoutinesScheduler::IsLockedVaultStatusJson(
      R"({"ok":true,"data":{"lockState":"locked"}})"));
  EXPECT_TRUE(MahoRoutinesScheduler::IsLockedVaultStatusJson(
      R"({"ok":true,"data":{"lockState":"auto_locked"}})"));
  // Malformed or incomplete evidence must never report unlocked (fail closed).
  EXPECT_TRUE(MahoRoutinesScheduler::IsLockedVaultStatusJson(""));
  EXPECT_TRUE(MahoRoutinesScheduler::IsLockedVaultStatusJson("not-json"));
  EXPECT_TRUE(MahoRoutinesScheduler::IsLockedVaultStatusJson("[]"));
  EXPECT_TRUE(MahoRoutinesScheduler::IsLockedVaultStatusJson(
      R"({"ok":false,"data":{"lockState":"unlocked"}})"));
  EXPECT_TRUE(MahoRoutinesScheduler::IsLockedVaultStatusJson(R"({"ok":true})"));
  EXPECT_TRUE(
      MahoRoutinesScheduler::IsLockedVaultStatusJson(R"({"ok":true,"data":{}})"));
}

TEST_F(MahoRoutinesSchedulerTest,
       DisabledConversationArchiveStillTicksAndPublishesAutoLock) {
  ASSERT_NE(core(), nullptr);
  EXPECT_EQ(-1, maho_core_get_conversation_auto_archive_policy(core()));

  auto initialized =
      InitializeVaultInCore("correct horse battery staple",
                            "test-only independent recovery material");
  ASSERT_TRUE(initialized->success);
  ASSERT_TRUE(initialized->status);
  ASSERT_EQ(maho_settings::mojom::VaultLockState::kUnlocked,
            initialized->status->lock_state);

  const int64_t activity_time = base::Time::Now().InSecondsFSinceUnixEpoch();
  const int64_t auto_lock_seconds =
      static_cast<int64_t>(initialized->status->auto_lock_minutes) * 60;
  ASSERT_EQ(-2, maho_core_auto_archive_conversations(
                    core(), activity_time + auto_lock_seconds + 60));

  maho::NotifyVaultLockStateChanged(/*locked=*/false);
  ASSERT_FALSE(maho::IsVaultLockedForUi());
  int lock_broadcasts = 0;
  base::CallbackListSubscription subscription =
      maho::AddVaultLockStateChangeCallback(base::BindRepeating(
          [](int* count, bool locked) {
            if (locked) {
              ++*count;
            }
          },
          &lock_broadcasts));

  maho::MahoRoutinesScheduler scheduler(core());
  scheduler.RunTickForTesting(activity_time + auto_lock_seconds + 60);

  auto after_tick = GetVaultStatusFromCore();
  ASSERT_TRUE(after_tick->success);
  ASSERT_TRUE(after_tick->status);
  EXPECT_EQ(maho_settings::mojom::VaultLockState::kAutoLocked,
            after_tick->status->lock_state);
  EXPECT_EQ(1, lock_broadcasts);
  EXPECT_TRUE(maho::IsVaultLockedForUi());
}

TEST_F(MahoRoutinesSchedulerTest,
       OrdinaryTicksDoNotPostponeVaultAutoLock) {
  ASSERT_NE(core(), nullptr);
  auto initialized = InitializeVaultInCore(
      "correct horse battery staple",
      "test-only independent recovery material");
  ASSERT_TRUE(initialized->success);
  ASSERT_TRUE(initialized->status);
  ASSERT_EQ(maho_settings::mojom::VaultLockState::kUnlocked,
            initialized->status->lock_state);
  ASSERT_GT(initialized->status->auto_lock_minutes, 2u);

  const int64_t activity_time =
      base::Time::Now().InSecondsFSinceUnixEpoch();
  const int64_t auto_lock_seconds =
      static_cast<int64_t>(initialized->status->auto_lock_minutes) * 60;
  maho::MahoRoutinesScheduler scheduler(core());

  scheduler.RunTickForTesting(activity_time + auto_lock_seconds - 60);
  auto after_ordinary_tick = GetVaultStatusFromCore();
  ASSERT_TRUE(after_ordinary_tick->success);
  ASSERT_TRUE(after_ordinary_tick->status);
  EXPECT_EQ(maho_settings::mojom::VaultLockState::kUnlocked,
            after_ordinary_tick->status->lock_state);

  // Only two injected minutes separate these ticks. If the ordinary tick above
  // counted as user activity, this tick would still leave the Vault unlocked.
  scheduler.RunTickForTesting(activity_time + auto_lock_seconds + 60);
  auto after_auto_lock_tick = GetVaultStatusFromCore();
  ASSERT_TRUE(after_auto_lock_tick->success);
  ASSERT_TRUE(after_auto_lock_tick->status);
  EXPECT_EQ(maho_settings::mojom::VaultLockState::kAutoLocked,
            after_auto_lock_tick->status->lock_state);
}

TEST_F(MahoRoutinesSchedulerTest,
       AutoLockBroadcastRevokesSensitiveAccess) {
  ASSERT_NE(core(), nullptr);
  auto initialized = InitializeVaultInCore(
      "correct horse battery staple",
      "test-only independent recovery material");
  ASSERT_TRUE(initialized->success);
  ASSERT_TRUE(initialized->status);
  ASSERT_GT(initialized->status->auto_lock_minutes, 1u);

  auto added = AddVaultLoginInCore(
      "Auto-lock account", {"https://auto-lock.example"},
      "person@example.test", kSentinel);
  ASSERT_TRUE(added->success);
  ASSERT_TRUE(added->item);

  auto* authorization =
      maho::passwords::MahoPasswordAuthorizationService::Get();
  authorization->ResetForTesting();

  const int64_t activity_time =
      base::Time::Now().InSecondsFSinceUnixEpoch();
  const int64_t auto_lock_seconds =
      static_cast<int64_t>(initialized->status->auto_lock_minutes) * 60;
  maho::MahoRoutinesScheduler scheduler(core());

  // Establish the process-global broadcast cache as unlocked before subscribing
  // so the auto-lock below is exactly one observable false-to-true transition.
  scheduler.RunTickForTesting(activity_time + 60);
  auto before_auto_lock = GetVaultStatusFromCore();
  ASSERT_TRUE(before_auto_lock->success);
  ASSERT_TRUE(before_auto_lock->status);
  ASSERT_EQ(maho_settings::mojom::VaultLockState::kUnlocked,
            before_auto_lock->status->lock_state);
  ASSERT_FALSE(maho::IsVaultLockedForUi());

  authorization->GrantForTesting(
      kSettingsAuthorizationProfile,
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kCopy,
      base::TimeTicks::Now() +
          maho::passwords::MahoPasswordAuthorizationService::
              kAuthorizationLifetime);
  ASSERT_TRUE(authorization->ConsumeAuthorization(
      kSettingsAuthorizationProfile,
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kCopy));

  int lock_broadcasts = 0;
  bool callback_locked = false;
  base::CallbackListSubscription subscription =
      maho::AddVaultLockStateChangeCallback(base::BindRepeating(
          [](int* count, bool* last_locked, bool locked) {
            if (locked) {
              ++*count;
            }
            *last_locked = locked;
          },
          &lock_broadcasts, &callback_locked));

  scheduler.RunTickForTesting(activity_time + auto_lock_seconds + 60);

  auto after_auto_lock = GetVaultStatusFromCore();
  ASSERT_TRUE(after_auto_lock->success);
  ASSERT_TRUE(after_auto_lock->status);
  EXPECT_EQ(maho_settings::mojom::VaultLockState::kAutoLocked,
            after_auto_lock->status->lock_state);
  EXPECT_EQ(1, lock_broadcasts);
  EXPECT_TRUE(callback_locked);
  EXPECT_EQ(0u, authorization->grant_count_for_testing());
  EXPECT_FALSE(authorization->ConsumeAuthorization(
      kSettingsAuthorizationProfile,
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kCopy));

  auto denied = maho_settings_password_helpers::UseVaultSecretInCore(
      kSettingsAuthorizationProfile, added->item->id, added->item->revision,
      maho_settings::mojom::SecretAction::kCopy);
  EXPECT_FALSE(denied->success);
  EXPECT_EQ("locked", denied->error_code);

  authorization->ResetForTesting();
}

TEST_F(MahoRoutinesSchedulerTest, ArchivesInactiveConversationsWhenEnabled) {
  ASSERT_NE(core(), nullptr);
  ASSERT_TRUE(maho_core_create_conversation(core(), "old-conversation",
                                            "Old conversation", nullptr,
                                            nullptr));
  ASSERT_TRUE(maho_core_set_conversation_auto_archive_policy(core(), 3));

  maho::MahoRoutinesScheduler scheduler(core());
  scheduler.RunTickForTesting(/*now_sec=*/2'000'000'000);

  char* archived_json = maho_core_list_conversations_v2(
      core(), R"({"state":"archived","limit":100})");
  ASSERT_NE(archived_json, nullptr);
  std::string archived(archived_json);
  maho_string_free(archived_json);
  EXPECT_NE(std::string::npos, archived.find("old-conversation"));
}

TEST_F(MahoRoutinesSchedulerTest,
       LeavesInactiveConversationsActiveWhenDisabled) {
  ASSERT_NE(core(), nullptr);
  ASSERT_TRUE(maho_core_create_conversation(core(), "kept-conversation",
                                            "Kept conversation", nullptr,
                                            nullptr));
  ASSERT_TRUE(maho_core_set_conversation_auto_archive_policy(core(), -1));

  maho::MahoRoutinesScheduler scheduler(core());
  scheduler.RunTickForTesting(/*now_sec=*/2'000'000'000);

  char* active_json = maho_core_list_conversations_v2(
      core(), R"({"state":"active","limit":100})");
  ASSERT_NE(active_json, nullptr);
  std::string active(active_json);
  maho_string_free(active_json);
  EXPECT_NE(std::string::npos, active.find("kept-conversation"));
}

TEST(MahoSettingsCurrentBrowserSpaceSnapshotTest,
     Task12ParserBuildsReadOnlyWindowScopedDto) {
  auto snapshot =
      MahoSettingsPageHandler::ParseCurrentBrowserSpaceSnapshotForTesting(
          42, "space-work",
          R"([{"id":"space-home","name":"Home"},{"id":"space-work","name":"Work"}])",
          R"([{"id":"tab-a","title":"A"},{"id":"tab-b","title":"B"}])");

  ASSERT_TRUE(snapshot);
  EXPECT_EQ(42, snapshot->focused_browser_session_id);
  ASSERT_TRUE(snapshot->selected_space);
  EXPECT_EQ("space-work", snapshot->selected_space->id);
  EXPECT_EQ("Work", snapshot->selected_space->name);
  EXPECT_EQ((std::vector<std::string>{"tab-a", "tab-b"}),
            snapshot->assigned_tab_ids);
  EXPECT_EQ((std::vector<std::string>{"42"}),
            snapshot->assigned_window_ids);
}

TEST(MahoSettingsCurrentBrowserSpaceSnapshotTest,
     Task12ParserFailsClosedForUnavailableOrInvalidCoreData) {
  constexpr std::string_view kInvalidSpaceCatalogs[] = {
      "", "not-json", R"({"id":"space-work"})",
      R"([{"id":"space-other","name":"Other"}])",
      R"([{"id":"space-work"}])"};
  for (std::string_view catalog : kInvalidSpaceCatalogs) {
    SCOPED_TRACE(catalog);
    EXPECT_FALSE(
        MahoSettingsPageHandler::ParseCurrentBrowserSpaceSnapshotForTesting(
            42, "space-work", std::string(catalog), "[]"));
  }

  constexpr std::string_view kInvalidTabPayloads[] = {
      "", "not-json", R"({"id":"tab-a"})", R"([null])",
      R"([{"title":"missing id"}])", R"([{"id":""}])"};
  for (std::string_view tabs : kInvalidTabPayloads) {
    SCOPED_TRACE(tabs);
    EXPECT_FALSE(
        MahoSettingsPageHandler::ParseCurrentBrowserSpaceSnapshotForTesting(
            42, "space-work",
            R"([{"id":"space-work","name":"Work"}])",
            std::string(tabs)));
  }

  EXPECT_FALSE(
      MahoSettingsPageHandler::ParseCurrentBrowserSpaceSnapshotForTesting(
          0, "space-work", R"([{"id":"space-work","name":"Work"}])",
          "[]"));
  EXPECT_FALSE(
      MahoSettingsPageHandler::ParseCurrentBrowserSpaceSnapshotForTesting(
          42, "", R"([{"id":"space-work","name":"Work"}])", "[]"));
}

TEST(MahoSettingsCurrentBrowserSpaceSnapshotTest,
     Task12PrivacyGateAllowsOnlyRegularProfiles) {
  EXPECT_TRUE(MahoSettingsPageHandler::IsCurrentBrowserSpaceSnapshotProfileAllowedForTesting(
      true, false, false, false));
  EXPECT_FALSE(MahoSettingsPageHandler::IsCurrentBrowserSpaceSnapshotProfileAllowedForTesting(
      false, false, false, false));
  EXPECT_FALSE(MahoSettingsPageHandler::IsCurrentBrowserSpaceSnapshotProfileAllowedForTesting(
      true, true, false, false));
  EXPECT_FALSE(MahoSettingsPageHandler::IsCurrentBrowserSpaceSnapshotProfileAllowedForTesting(
      true, false, true, false));
  EXPECT_FALSE(MahoSettingsPageHandler::IsCurrentBrowserSpaceSnapshotProfileAllowedForTesting(
      true, false, false, true));
}

TEST(MahoSettingsSelectedProfileStateMachineTest,
     Task5StateMachineHostASelectsB) {
  MahoSettingsPageHandler::SelectedProfileRequestState state;
  state.selected_id = "host-a";
  state.target_token = "token-a";
  state.context_revision = 4;
  state.profile_revision = 9;
  state.load_generation = 12;
  state.attached = true;

  const uint64_t generation =
      MahoSettingsPageHandler::BeginSelectedProfileRequestForTesting(
          &state, "profile-b", "token-b");

  EXPECT_EQ(13u, generation);
  EXPECT_EQ("profile-b", state.selected_id);
  EXPECT_EQ("token-b", state.target_token);
  EXPECT_EQ(4u, state.context_revision);
  EXPECT_TRUE(MahoSettingsPageHandler::CompleteSelectedProfileRequestForTesting(
      &state, generation, "profile-b", "token-b", 27));
  EXPECT_TRUE(state.attached);
  EXPECT_EQ(5u, state.context_revision);
  EXPECT_EQ(27u, state.profile_revision);

  auto context = MahoSettingsPageHandler::BuildSelectedProfileContextForTesting(
      state, true, false,
      maho_settings::mojom::ProfileLifecycleState::kReady);
  EXPECT_EQ("profile-b", context->profile_id);
  EXPECT_EQ("token-b", context->target_token);
  EXPECT_EQ(5u, context->context_revision);
  EXPECT_EQ(27u, context->profile_revision);
  EXPECT_TRUE(context->is_host_profile);
  EXPECT_FALSE(context->is_active_maho_profile);
}

TEST(MahoSettingsSelectedProfileStateMachineTest,
     Task5StateMachineDelayedBAfterCIsStale) {
  MahoSettingsPageHandler::SelectedProfileRequestState state;

  const uint64_t generation_b =
      MahoSettingsPageHandler::BeginSelectedProfileRequestForTesting(
          &state, "profile-b", "token-b");
  const uint64_t generation_c =
      MahoSettingsPageHandler::BeginSelectedProfileRequestForTesting(
          &state, "profile-c", "token-c");

  EXPECT_FALSE(MahoSettingsPageHandler::CompleteSelectedProfileRequestForTesting(
      &state, generation_b, "profile-b", "token-b", 11));
  EXPECT_EQ(0u, state.context_revision);
  EXPECT_EQ(0u, state.profile_revision);
  EXPECT_FALSE(state.attached);

  EXPECT_TRUE(MahoSettingsPageHandler::CompleteSelectedProfileRequestForTesting(
      &state, generation_c, "profile-c", "token-c", 12));
  EXPECT_EQ(1u, state.context_revision);
  EXPECT_EQ(12u, state.profile_revision);
  EXPECT_TRUE(state.attached);
}

TEST(MahoSettingsSelectedProfileStateMachineTest,
     Task5StateMachineSameTargetStable) {
  MahoSettingsPageHandler::SelectedProfileRequestState state;
  const uint64_t first_generation =
      MahoSettingsPageHandler::BeginSelectedProfileRequestForTesting(
          &state, "profile-a", "token-a");
  ASSERT_TRUE(MahoSettingsPageHandler::CompleteSelectedProfileRequestForTesting(
      &state, first_generation, "profile-a", "token-a", 7));
  ASSERT_EQ(1u, state.context_revision);

  const uint64_t second_generation =
      MahoSettingsPageHandler::BeginSelectedProfileRequestForTesting(
          &state, "profile-a", "token-a");
  EXPECT_TRUE(MahoSettingsPageHandler::CompleteSelectedProfileRequestForTesting(
      &state, second_generation, "profile-a", "token-a", 8));
  EXPECT_EQ(1u, state.context_revision);
  EXPECT_EQ(8u, state.profile_revision);
  EXPECT_TRUE(state.attached);
}

TEST(MahoSettingsSelectedProfileStateMachineTest,
     Task5StateMachineMatchingInvalidation) {
  MahoSettingsPageHandler::SelectedProfileRequestState state;
  const uint64_t generation =
      MahoSettingsPageHandler::BeginSelectedProfileRequestForTesting(
          &state, "profile-a", "token-a");
  ASSERT_TRUE(MahoSettingsPageHandler::CompleteSelectedProfileRequestForTesting(
      &state, generation, "profile-a", "token-a", 5));
  ASSERT_EQ(1u, state.context_revision);
  ASSERT_EQ(1u, state.load_generation);

  EXPECT_FALSE(MahoSettingsPageHandler::InvalidateSelectedProfileRequestForTesting(
      &state, "profile-b", "token-b"));
  EXPECT_EQ(1u, state.context_revision);
  EXPECT_EQ(1u, state.load_generation);
  EXPECT_EQ("profile-a", state.selected_id);

  EXPECT_TRUE(MahoSettingsPageHandler::InvalidateSelectedProfileRequestForTesting(
      &state, "profile-a", "token-a"));
  EXPECT_EQ(2u, state.context_revision);
  EXPECT_EQ(2u, state.load_generation);
  EXPECT_EQ(0u, state.profile_revision);
  EXPECT_TRUE(state.selected_id.empty());
  EXPECT_TRUE(state.target_token.empty());
  EXPECT_FALSE(state.attached);
}

TEST(MahoSettingsSelectedProfileStateMachineTest,
     Task6IdentityValidationRejectsMismatchesAndStaleRevisions) {
  using Error = maho_settings::mojom::ProfileTargetErrorCode;
  MahoSettingsPageHandler::SelectedProfileRequestState state;
  const uint64_t generation =
      MahoSettingsPageHandler::BeginSelectedProfileRequestForTesting(
          &state, "profile-b", "token-b");
  ASSERT_TRUE(MahoSettingsPageHandler::CompleteSelectedProfileRequestForTesting(
      &state, generation, "profile-b", "token-b", 27));

  EXPECT_EQ(Error::kNone,
            MahoSettingsPageHandler::ValidateSelectedProfileIdentityForTesting(
                state, "profile-b", "token-b", 1, 27));
  EXPECT_EQ(Error::kStaleContext,
            MahoSettingsPageHandler::ValidateSelectedProfileIdentityForTesting(
                state, "profile-c", "token-b", 1, 27));
  EXPECT_EQ(Error::kStaleContext,
            MahoSettingsPageHandler::ValidateSelectedProfileIdentityForTesting(
                state, "profile-b", "wrong-token", 1, 27));
  EXPECT_EQ(Error::kStaleContext,
            MahoSettingsPageHandler::ValidateSelectedProfileIdentityForTesting(
                state, "profile-b", "token-b", 2, 27));
  EXPECT_EQ(Error::kStaleProfileRevision,
            MahoSettingsPageHandler::ValidateSelectedProfileIdentityForTesting(
                state, "profile-b", "token-b", 1, 28));
}

TEST(MahoSettingsSelectedProfileStateMachineTest,
     ProfileMetadataAndArchiveMutationRoundTripBumpsRevision) {
  maho::ProfileRegistryRecord record;
  record.maho_id = "profile-b";
  record.name = "Profile B";
  record.avatar_color = "#AF52DE";
  record.archive_timeout_hours = 12;
  record.revision = 9;
  uint64_t catalog_revision = 9;
  const std::string name = "Work";
  const std::string avatar_color = "#34C759";

  ASSERT_TRUE(MahoSettingsPageHandler::ApplyProfileSettingsMutationForTesting(
      &record, &catalog_revision, &name, &avatar_color, nullptr));
  EXPECT_EQ(record.name, "Work");
  EXPECT_EQ(record.avatar_color, "#34C759");
  EXPECT_EQ(record.revision, 10u);
  EXPECT_EQ(catalog_revision, 10u);

  const std::optional<int32_t> disabled_timeout;
  ASSERT_TRUE(MahoSettingsPageHandler::ApplyProfileSettingsMutationForTesting(
      &record, &catalog_revision, nullptr, nullptr, &disabled_timeout));
  EXPECT_FALSE(record.archive_timeout_hours.has_value());
  EXPECT_EQ(record.revision, 11u);
  EXPECT_EQ(catalog_revision, 11u);

  MahoSettingsPageHandler::SelectedProfileRequestState state;
  const uint64_t generation =
      MahoSettingsPageHandler::BeginSelectedProfileRequestForTesting(
          &state, "profile-b", "token-b");
  ASSERT_TRUE(MahoSettingsPageHandler::CompleteSelectedProfileRequestForTesting(
      &state, generation, "profile-b", "token-b", record.revision));
  EXPECT_EQ(
      maho_settings::mojom::ProfileTargetErrorCode::kStaleProfileRevision,
      MahoSettingsPageHandler::ValidateSelectedProfileIdentityForTesting(
          state, "profile-b", "token-b", state.context_revision, 10));
}

TEST(MahoSettingsSelectedProfileStateMachineTest,
     ProfileUpdateErrorsPreserveTaxonomy) {
  using Error = maho_settings::mojom::ProfileTargetErrorCode;
  EXPECT_EQ(Error::kInvalidArgument,
            MahoSettingsPageHandler::MapProfileUpdateErrorForTesting(
                "PROFILE_NAME_DUPLICATE"));
  EXPECT_EQ(Error::kInvalidArgument,
            MahoSettingsPageHandler::MapProfileUpdateErrorForTesting(
                "PROFILE_AVATAR_COLOR_INVALID"));
  EXPECT_EQ(Error::kInvalidArgument,
            MahoSettingsPageHandler::MapProfileUpdateErrorForTesting(
                "PROFILE_ARCHIVE_TIMEOUT_INVALID"));
  EXPECT_EQ(Error::kProfileNotFound,
            MahoSettingsPageHandler::MapProfileUpdateErrorForTesting(
                "PROFILE_NOT_FOUND"));
  EXPECT_EQ(Error::kInternal,
            MahoSettingsPageHandler::MapProfileUpdateErrorForTesting(
                "PROFILE_PERSISTENCE_FAILED"));
}

TEST(MahoSettingsSelectedProfileStateMachineTest,
     Task7DeletionPreflightProtectsUnsafeTargets) {
  using Lifecycle = maho::ProfileLifecycleState;
  EXPECT_TRUE(MahoSettingsPageHandler::CanDeleteProfileForTesting(
      false, false, false, false, Lifecycle::kReady));
  EXPECT_FALSE(MahoSettingsPageHandler::CanDeleteProfileForTesting(
      true, false, false, false, Lifecycle::kReady));
  EXPECT_FALSE(MahoSettingsPageHandler::CanDeleteProfileForTesting(
      false, true, false, false, Lifecycle::kReady));
  EXPECT_FALSE(MahoSettingsPageHandler::CanDeleteProfileForTesting(
      false, false, true, false, Lifecycle::kReady));
  EXPECT_FALSE(MahoSettingsPageHandler::CanDeleteProfileForTesting(
      false, false, false, true, Lifecycle::kReady));
  EXPECT_FALSE(MahoSettingsPageHandler::CanDeleteProfileForTesting(
      false, false, false, false, Lifecycle::kProvisioning));
  EXPECT_FALSE(MahoSettingsPageHandler::CanDeleteProfileForTesting(
      false, false, false, false, Lifecycle::kDeleting));
}

TEST(MahoSettingsSelectedProfileStateMachineTest,
     Task5RegistryErrorsAreDistinct) {
  using LookupError = maho::ProfileRegistryLookupError;
  using MojoError = maho_settings::mojom::ProfileTargetErrorCode;

  struct ErrorCase {
    LookupError input;
    MojoError expected_code;
  };
  constexpr ErrorCase kCases[] = {
      {LookupError::kInvalidProfileId, MojoError::kInvalidProfileId},
      {LookupError::kUnknownProfile, MojoError::kProfileNotFound},
      {LookupError::kProfileDeleting, MojoError::kProfileDeleting},
      {LookupError::kStaleRevision, MojoError::kStaleContext},
      {LookupError::kCatalogUnavailable, MojoError::kProfileUnavailable},
      {LookupError::kProfileProvisioning, MojoError::kProfileUnavailable},
      {LookupError::kProfileRepairRequired, MojoError::kProfileUnavailable},
  };

  for (const auto& test_case : kCases) {
    SCOPED_TRACE(static_cast<int>(test_case.input));
    auto error = MahoSettingsPageHandler::MakeLookupErrorForTesting(
        test_case.input, 17, 23);
    EXPECT_EQ(test_case.expected_code, error->code);
    EXPECT_FALSE(error->message.empty());
    EXPECT_EQ(17u, error->current_context_revision);
    ASSERT_TRUE(error->current_profile_revision.has_value());
    EXPECT_EQ(23u, *error->current_profile_revision);
  }
}

TEST(MahoSettingsDisconnectAIProviderTest, AtomicallyRejectsMalformedPayloads) {
  TestingPrefServiceSimple prefs;
  maho::ai::RegisterProfilePrefs(prefs.registry());

  prefs.SetString("maho.ai.provider", "anthropic");
  prefs.SetString("maho.ai.model", "claude-sonnet-4-20250514");

  {
    ScopedDictPrefUpdate pcfg_update(&prefs, "maho.ai.provider_configs");
    base::DictValue* pcfg_ant = pcfg_update->EnsureDict("anthropic");
    pcfg_ant->Set("connected", true);
    pcfg_ant->Set("last_model", "claude-sonnet-4-20250514");

    base::DictValue* pcfg_oai = pcfg_update->EnsureDict("openai");
    pcfg_oai->Set("connected", true);
    pcfg_oai->Set("last_model", "gpt-4o");
  }

  {
    ScopedDictPrefUpdate task_update(&prefs, "maho.ai.task_models");
    base::DictValue* tidy = task_update->EnsureDict("tab_tidy");
    tidy->Set("provider", "anthropic");
    tidy->Set("model", "claude-sonnet-4-20250514");

    base::DictValue* inline_edit = task_update->EnsureDict("inline_edit");
    inline_edit->Set("provider", "openai");
    inline_edit->Set("model", "gpt-4o");
  }

  // 1. Extra chat replacement -> MUST REJECT without mutation
  {
    std::vector<maho_settings::mojom::AITaskReplacementPtr> repls;
    auto r1 = maho_settings::mojom::AITaskReplacement::New();
    r1->task_id = "tab_tidy";
    r1->inherit_default = true;
    repls.push_back(std::move(r1));

    auto r2 = maho_settings::mojom::AITaskReplacement::New();
    r2->task_id = "chat";
    r2->inherit_default = false;
    r2->provider_id = "openai";
    r2->model_id = "gpt-4o";
    repls.push_back(std::move(r2));

    std::string err;
    bool ok = MahoSettingsPageHandler::DisconnectAIProviderForTesting(
        &prefs, "anthropic", "openai", "gpt-4o", std::move(repls), &err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty());
    EXPECT_EQ(prefs.GetString("maho.ai.provider"), "anthropic");
    const base::DictValue& tm = prefs.GetDict("maho.ai.task_models");
    EXPECT_EQ(*tm.FindDict("tab_tidy")->FindString("provider"), "anthropic");
    EXPECT_EQ(tm.FindDict("chat"), nullptr);
  }

  // 2. Unrelated task replacement -> MUST REJECT without mutation
  {
    std::vector<maho_settings::mojom::AITaskReplacementPtr> repls;
    auto r1 = maho_settings::mojom::AITaskReplacement::New();
    r1->task_id = "tab_tidy";
    r1->inherit_default = true;
    repls.push_back(std::move(r1));

    auto r2 = maho_settings::mojom::AITaskReplacement::New();
    r2->task_id = "inline_edit";
    r2->inherit_default = false;
    r2->provider_id = "openai";
    r2->model_id = "gpt-4o";
    repls.push_back(std::move(r2));

    std::string err;
    bool ok = MahoSettingsPageHandler::DisconnectAIProviderForTesting(
        &prefs, "anthropic", "openai", "gpt-4o", std::move(repls), &err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty());
    EXPECT_EQ(prefs.GetString("maho.ai.provider"), "anthropic");
  }

  // 3. Disconnected replacement destination -> MUST REJECT without mutation
  {
    std::vector<maho_settings::mojom::AITaskReplacementPtr> repls;
    auto r1 = maho_settings::mojom::AITaskReplacement::New();
    r1->task_id = "tab_tidy";
    r1->inherit_default = false;
    r1->provider_id = "not-connected-provider";
    r1->model_id = "some-model";
    repls.push_back(std::move(r1));

    std::string err;
    bool ok = MahoSettingsPageHandler::DisconnectAIProviderForTesting(
        &prefs, "anthropic", "openai", "gpt-4o", std::move(repls), &err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty());
    EXPECT_EQ(prefs.GetString("maho.ai.provider"), "anthropic");
  }

  // 4. Invalid replacement Default model -> MUST REJECT without mutation
  {
    std::vector<maho_settings::mojom::AITaskReplacementPtr> repls;
    auto r1 = maho_settings::mojom::AITaskReplacement::New();
    r1->task_id = "tab_tidy";
    r1->inherit_default = true;
    repls.push_back(std::move(r1));

    std::string err;
    bool ok = MahoSettingsPageHandler::DisconnectAIProviderForTesting(
        &prefs, "anthropic", "openai", "invalid model with spaces", std::move(repls), &err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty());
    EXPECT_EQ(prefs.GetString("maho.ai.provider"), "anthropic");
  }

  // 5. Valid exact replacement -> COMMITS successfully
  {
    std::vector<maho_settings::mojom::AITaskReplacementPtr> repls;
    auto r1 = maho_settings::mojom::AITaskReplacement::New();
    r1->task_id = "tab_tidy";
    r1->inherit_default = true;
    repls.push_back(std::move(r1));

    std::string err;
    bool ok = MahoSettingsPageHandler::DisconnectAIProviderForTesting(
        &prefs, "anthropic", "openai", "gpt-4o", std::move(repls), &err);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(prefs.GetString("maho.ai.provider"), "openai");
    EXPECT_EQ(prefs.GetString("maho.ai.model"), "gpt-4o");
    const base::DictValue& tm = prefs.GetDict("maho.ai.task_models");
    EXPECT_EQ(tm.FindDict("tab_tidy"), nullptr);
    const base::DictValue& pcfg = prefs.GetDict("maho.ai.provider_configs");
    EXPECT_EQ(pcfg.FindDict("anthropic"), nullptr);
  }
}

}  // namespace
}  // namespace maho_settings_password_helpers
