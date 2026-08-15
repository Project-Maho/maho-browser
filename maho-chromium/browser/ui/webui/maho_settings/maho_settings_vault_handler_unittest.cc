#include "base/test/task_environment.h"
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
    const std::string& password) {
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kAdd);
  return maho_settings_password_helpers::AddVaultLoginInCore(
      kSettingsAuthorizationProfile, title, origins, username, password);
}

maho_settings::mojom::VaultOperationResultPtr UpdateVaultLoginInCore(
    const std::string& item_id,
    uint64_t expected_revision,
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::optional<std::string>& password) {
  GrantSettingsAuthorization(
      maho::passwords::PasswordAuthorizationAction::kUpdate);
  return maho_settings_password_helpers::UpdateVaultLoginInCore(
      kSettingsAuthorizationProfile, item_id, expected_revision, title,
      origins, username, password);
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
  }

  void TearDown() override {
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
  EXPECT_EQ("Updated", updated->item->title);
  EXPECT_GT(updated->item->revision, added->item->revision);
  EXPECT_NE(kSentinel, updated->item->username_hint);

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
  EXPECT_EQ("Updated", updated->item->title);
  EXPECT_GT(updated->item->revision, added->item->revision);

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

  auto unsupported_update =
      UpdateVaultLoginInCore("item-1", std::numeric_limits<uint64_t>::max(),
                             "Title", {}, "user", std::nullopt);
  EXPECT_FALSE(unsupported_update->success);
  EXPECT_EQ("unsupported_revision", unsupported_update->error_code);

  auto unsupported_delete =
      DeleteVaultItemInCore("item-1", std::numeric_limits<uint64_t>::max());
  EXPECT_FALSE(unsupported_delete->success);
  EXPECT_EQ("unsupported_revision", unsupported_delete->error_code);
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

}  // namespace
}  // namespace maho_settings_password_helpers
