#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings_password_helpers.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho_settings_password_helpers {
namespace {

constexpr char kSentinel[] = "S3NTINEL-maho-vault-9F4C";
class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_(maho::GetCore()) {
    maho::SetCore(core);
  }
  ~ScopedCoreOverride() { maho::SetCore(saved_); }

 private:
  MahoCore* saved_;
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
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(maho_storage_set_sqlcipher_key(
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    const base::FilePath database_path =
        temp_dir_.GetPath().AppendASCII("vault-handler.sqlite");
    core_ = maho_core_new_with_storage(database_path.AsUTF8Unsafe().c_str());
    ASSERT_NE(core_, nullptr);
    core_override_ = std::make_unique<ScopedCoreOverride>(core_);
  }

  void TearDown() override {
    core_override_.reset();
    maho_core_free(core_);
    core_ = nullptr;
  }

  const base::FilePath& temp_path() const { return temp_dir_.GetPath(); }

 private:
  base::ScopedTempDir temp_dir_;
  MahoCore* core_ = nullptr;
  std::unique_ptr<ScopedCoreOverride> core_override_;
};

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

TEST_F(MahoSettingsVaultHandlerTest, UseVaultSecretReturnsOnlyStatus) {
  ASSERT_TRUE(
      InitializeVaultInCore("correct passphrase", "recovery key")->success);
  auto added = AddVaultLoginInCore("Secret use", {"https://example.test"},
                                   "person@example.test", kSentinel);
  ASSERT_TRUE(added->success);
  ASSERT_TRUE(added->item);

  auto used = UseVaultSecretInCore(added->item->id, added->item->revision,
                                   maho_settings::mojom::SecretAction::kFill);

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
      maho_settings::mojom::SecretAction::kFill);

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
      {"bitwarden", false, false, Provider::kBitwarden, Provider::kMahoNative,
       false},
      {"bitwarden", false, true, Provider::kBitwarden, Provider::kMahoNative,
       false},
      {"bitwarden", true, false, Provider::kBitwarden, Provider::kBitwarden,
       true},
      {"bitwarden", true, true, Provider::kBitwarden, Provider::kBitwarden,
       true},
      {"onepassword", false, false, Provider::kOnePassword,
       Provider::kMahoNative, false},
      {"onepassword", true, false, Provider::kOnePassword,
       Provider::kMahoNative, false},
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
       PasswordSetupCompletionRequiresApprovedProviderAndInitializedVault) {
  auto vault_status = maho_settings::mojom::VaultOperationResult::New();
  vault_status->success = true;
  vault_status->status = maho_settings::mojom::VaultStatus::New();

  vault_status->status->lock_state =
      maho_settings::mojom::VaultLockState::kUninitialized;
  EXPECT_FALSE(IsPasswordSetupCompleteForTesting("maho_native",
                                                 vault_status.get()));

  for (const auto lock_state : {
           maho_settings::mojom::VaultLockState::kLocked,
           maho_settings::mojom::VaultLockState::kUnlocked,
           maho_settings::mojom::VaultLockState::kAutoLocked,
       }) {
    vault_status->status->lock_state = lock_state;
    EXPECT_TRUE(
        IsPasswordSetupCompleteForTesting("bitwarden", vault_status.get()));
    EXPECT_TRUE(
        IsPasswordSetupCompleteForTesting("onepassword", vault_status.get()));
  }
  EXPECT_FALSE(IsPasswordSetupCompleteForTesting("unsupported",
                                                 vault_status.get()));
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

}  // namespace
}  // namespace maho_settings_password_helpers
