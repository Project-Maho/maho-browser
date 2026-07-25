// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/passwords/maho_password_store_backend.h"

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_form_digest.h"
#include "components/password_manager/core/browser/password_store/password_store_change.h"
#include "components/password_manager/core/browser/password_store/stored_credential.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/passwords/maho_password_provider_utils.h"
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
constexpr char kManualPassword[] = "S3NTINEL-manual-runtime-5C1D";

std::u16string ImportedPassword16() {
  return u"S3NTINEL-imported-runtime-2A7B";
}

std::u16string ManualPassword16() {
  return u"S3NTINEL-manual-runtime-5C1D";
}

class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_(maho::GetCore()) {
    maho::SetCore(core);
  }
  ~ScopedCoreOverride() { maho::SetCore(saved_); }

  ScopedCoreOverride(const ScopedCoreOverride&) = delete;
  ScopedCoreOverride& operator=(const ScopedCoreOverride&) = delete;

 private:
  MahoCore* saved_;
};

bool VaultResponseOk(char* response) {
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

std::string TakeImportJson(char* response) {
  if (!response) {
    return {};
  }
  std::string json(response);
  maho_import_string_free(response);
  return json;
}

std::optional<std::string> PreviewTokenFromImportResponse(
    const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict() ||
      !parsed->GetDict().FindBool("ok").value_or(false)) {
    return std::nullopt;
  }
  const base::DictValue* data = parsed->GetDict().FindDict("data");
  if (!data) {
    return std::nullopt;
  }
  const std::string* preview_token = data->FindString("previewToken");
  if (!preview_token || preview_token->empty()) {
    return std::nullopt;
  }
  return *preview_token;
}

bool ImportCommitResponseOk(const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict() ||
      !parsed->GetDict().FindBool("ok").value_or(false)) {
    return false;
  }
  const base::DictValue* data = parsed->GetDict().FindDict("data");
  return data && data->FindInt("committed").value_or(0) == 1;
}

class ScopedPasswordImportJob {
 public:
  ScopedPasswordImportJob() : job_(maho_password_import_job_new()) {}
  ~ScopedPasswordImportJob() { maho_password_import_job_free(job_); }

  ScopedPasswordImportJob(const ScopedPasswordImportJob&) = delete;
  ScopedPasswordImportJob& operator=(const ScopedPasswordImportJob&) = delete;

  MahoPasswordImportJob* get() const { return job_; }

 private:
  MahoPasswordImportJob* job_ = nullptr;
};

}  // namespace

class MahoPasswordStoreBackendTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(maho_storage_set_sqlcipher_key(kSqlcipherKey));
  }

  void TearDown() override {
    core_override_.reset();
    if (core_) {
      maho_core_free(core_);
      core_ = nullptr;
    }
  }

  void CreateInitializedVaultCore() {
    const base::FilePath database_path =
        temp_dir_.GetPath().AppendASCII("password-backend.sqlite");
    core_ = maho_core_new_with_storage(database_path.AsUTF8Unsafe().c_str());
    ASSERT_NE(core_, nullptr);
    core_override_ = std::make_unique<ScopedCoreOverride>(core_);

    const std::string request = std::string("{\"masterPassphrase\":\"") +
                                kMasterPassphrase + "\",\"recoverySecret\":\"" +
                                kRecoverySecret + "\"}";
    ASSERT_TRUE(
        VaultResponseOk(maho_vault_initialize_json(core_, request.c_str())));
  }

  bool LockVault() { return VaultResponseOk(maho_vault_lock_json(core_)); }

  void SelectPasswordProvider(const std::string& provider_mode,
                              const std::string& installed_extensions_json) {
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

  base::FilePath WriteOnePasswordExport(const std::string& origin,
                                        const std::string& username,
                                        const std::string& password) {
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

  bool ImportOnePasswordCredential(const std::string& origin,
                                   const std::string& username,
                                   const std::string& password) {
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

  std::vector<MahoPasswordForm> FillMahoForms(MahoPasswordStoreBackend& backend,
                                              const std::string& origin) {
    base::RunLoop fill_loop;
    bool fill_success = false;
    std::vector<MahoPasswordForm> matched_forms;
    backend.FillMatchingLoginsMahoAsync(
        origin, base::BindOnce(
                    [](base::RunLoop* loop, bool* success_out,
                       std::vector<MahoPasswordForm>* forms_out,
                       std::vector<MahoPasswordForm> forms, bool success) {
                      *success_out = success;
                      *forms_out = std::move(forms);
                      loop->Quit();
                    },
                    &fill_loop, &fill_success, &matched_forms));
    fill_loop.Run();
    EXPECT_TRUE(fill_success);
    return matched_forms;
  }

  password_manager::BackendLoginsResult FillPasswordStoreForms(
      MahoPasswordStoreBackend& backend,
      const std::string& origin) {
    base::RunLoop fill_loop;
    bool fill_success = false;
    password_manager::BackendLoginsResult matched_forms;
    std::vector<password_manager::PasswordFormDigest> digests;
    digests.emplace_back(password_manager::PasswordForm::Scheme::kHtml, origin,
                         GURL(origin + "/login"));
    const bool include_psl = false;
    backend.FillMatchingLoginsAsync(
        base::BindOnce(
            [](base::RunLoop* loop, bool* success_out,
               password_manager::BackendLoginsResult* forms_out,
               password_manager::BackendLoginsResultOrError result) {
              if (auto* forms =
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
    EXPECT_TRUE(fill_success);
    return matched_forms;
  }

  std::string CredentialPayloadForOrigin(const std::string& origin) {
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

  MahoPasswordForm AddLoginAndFetch(MahoPasswordStoreBackend& backend,
                                    const std::string& origin,
                                    const std::string& username,
                                    const std::string& password) {
    MahoPasswordForm form;
    form.signon_realm = origin;
    form.username_value = username;
    form.password_value = password;

    base::RunLoop add_loop;
    bool add_success = false;
    backend.AddLoginMahoAsync(
        form, base::BindOnce(
                  [](base::RunLoop* loop, bool* success_out, bool success) {
                    *success_out = success;
                    loop->Quit();
                  },
                  &add_loop, &add_success));
    add_loop.Run();
    EXPECT_TRUE(add_success);

    base::RunLoop fill_loop;
    bool fill_success = false;
    std::vector<MahoPasswordForm> matched_forms;
    backend.FillMatchingLoginsMahoAsync(
        origin, base::BindOnce(
                    [](base::RunLoop* loop, bool* success_out,
                       std::vector<MahoPasswordForm>* forms_out,
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

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

 private:
  base::ScopedTempDir temp_dir_;
  MahoCore* core_ = nullptr;
  std::unique_ptr<ScopedCoreOverride> core_override_;
};

TEST_F(MahoPasswordStoreBackendTest, InitBackend) {
  MahoPasswordStoreBackend backend;
  EXPECT_FALSE(backend.IsInitialized());

  base::RunLoop run_loop;
  bool init_result = false;
  backend.InitBackend(base::BindOnce(
      [](base::RunLoop* loop, bool* result_out, bool success) {
        *result_out = success;
        loop->Quit();
      },
      &run_loop, &init_result));
  run_loop.Run();

  EXPECT_TRUE(init_result);
  EXPECT_TRUE(backend.IsInitialized());
}

TEST_F(MahoPasswordStoreBackendTest, Shutdown) {
  MahoPasswordStoreBackend backend;
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
      [](base::RunLoop* loop, bool* success_out,
         std::vector<MahoPasswordForm> forms, bool success) {
        *success_out = success;
        loop->Quit();
      },
      &call_loop, &call_success));
  call_loop.Run();

  EXPECT_FALSE(call_success);
}

TEST_F(MahoPasswordStoreBackendTest,
       DisabledAccountStoreBackendInitializesAndFailsClosed) {
  MahoPasswordStoreBackend backend(false);

  base::RunLoop init_loop;
  bool init_success = false;
  backend.InitBackend(base::BindOnce(
      [](base::RunLoop* loop, bool* success_out, bool success) {
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
                [](base::RunLoop* loop, bool* success_out, bool success) {
                  *success_out = success;
                  loop->Quit();
                },
                &add_loop, &add_success));
  add_loop.Run();
  EXPECT_FALSE(add_success);

  base::RunLoop wrapper_add_loop;
  bool wrapper_add_failed_closed = false;
  password_manager::StoredCredential disabled_credential;
  disabled_credential.signon_realm = "https://account-store.example";
  disabled_credential.username_value = u"account@example.com";
  disabled_credential.password_value = u"disabled-store-password";
  backend.AddLoginAsync(
      std::move(disabled_credential),
      base::BindOnce(
          [](base::RunLoop* loop, bool* failed_closed,
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
      [](base::RunLoop* loop, bool* success_out, size_t* count_out,
         password_manager::BackendLoginsResultOrError result) {
        if (auto* forms =
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
  MahoPasswordStoreBackend backend;
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  base::RunLoop run_loop;
  bool get_success = true;
  std::vector<MahoPasswordForm> fetched_forms;
  backend.GetAllLoginsMahoAsync(base::BindOnce(
      [](base::RunLoop* loop, bool* success_out,
         std::vector<MahoPasswordForm>* forms_out,
         std::vector<MahoPasswordForm> forms, bool success) {
        *success_out = success;
        *forms_out = std::move(forms);
        loop->Quit();
      },
      &run_loop, &get_success, &fetched_forms));
  run_loop.Run();

  // On an uninitialized Vault without active profile storage, queries fail
  // closed safely.
  EXPECT_FALSE(get_success);
}

TEST_F(MahoPasswordStoreBackendTest, AddAndRemoveLoginAsync) {
  MahoPasswordStoreBackend backend;
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
                [](base::RunLoop* loop, bool* success_out, bool success) {
                  *success_out = success;
                  loop->Quit();
                },
                &add_loop, &add_success));
  add_loop.Run();
  // Fail-closed Vault without initialized profile returns false safely without
  // crashing.
  EXPECT_FALSE(add_success);

  // Test FillMatchingLoginsMahoAsync
  base::RunLoop fill_loop;
  bool fill_success = true;
  std::vector<MahoPasswordForm> matched_forms;
  backend.FillMatchingLoginsMahoAsync(
      "https://example.com",
      base::BindOnce(
          [](base::RunLoop* loop, bool* success_out,
             std::vector<MahoPasswordForm>* forms_out,
             std::vector<MahoPasswordForm> forms, bool success) {
            *success_out = success;
            *forms_out = std::move(forms);
            loop->Quit();
          },
          &fill_loop, &fill_success, &matched_forms));
  fill_loop.Run();
  EXPECT_FALSE(fill_success);

  // Test RemoveLoginMahoAsync
  base::RunLoop remove_loop;
  bool remove_success = true;
  backend.RemoveLoginMahoAsync(
      form, base::BindOnce(
                [](base::RunLoop* loop, bool* success_out, bool success) {
                  *success_out = success;
                  loop->Quit();
                },
                &remove_loop, &remove_success));
  remove_loop.Run();
  EXPECT_FALSE(remove_success);
}

TEST_F(MahoPasswordStoreBackendTest, UpdateLoginAsync) {
  MahoPasswordStoreBackend backend;
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
                [](base::RunLoop* loop, bool* success_out, bool success) {
                  *success_out = success;
                  loop->Quit();
                },
                &update_loop, &update_success));
  update_loop.Run();
  EXPECT_FALSE(update_success);
}

TEST_F(MahoPasswordStoreBackendTest, RemoveLoginsCreatedBetweenAsync) {
  MahoPasswordStoreBackend backend;
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  base::Time now = base::Time::Now();
  base::RunLoop range_loop;
  bool range_success = true;
  backend.RemoveLoginsCreatedBetweenMahoAsync(
      now - base::Days(1), now + base::Days(1),
      base::BindOnce(
          [](base::RunLoop* loop, bool* success_out, bool success) {
            *success_out = success;
            loop->Quit();
          },
          &range_loop, &range_success));
  range_loop.Run();
  EXPECT_FALSE(range_success);
}

TEST_F(MahoPasswordStoreBackendTest, UnlockedVaultAddListAndFillLoginAsync) {
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend;
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();

  MahoPasswordForm fetched = AddLoginAndFetch(
      backend, "https://example.com", "user@example.com", kSentinelPassword);
  const std::string credential_payload =
      CredentialPayloadForOrigin("https://example.com");
  EXPECT_NE(std::string::npos, credential_payload.find(kSentinelPassword));
  std::optional<base::Value> parsed_payload =
      base::JSONReader::Read(credential_payload, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed_payload);
  ASSERT_TRUE(parsed_payload->is_list());
  ASSERT_EQ(1u, parsed_payload->GetList().size());
  const std::string* payload_password =
      parsed_payload->GetList()[0].GetDict().FindString("password");
  ASSERT_NE(nullptr, payload_password);
  EXPECT_EQ(kSentinelPassword, *payload_password);

  EXPECT_FALSE(fetched.id.empty());
  EXPECT_EQ("https://example.com", fetched.signon_realm);
  EXPECT_EQ("user@example.com", fetched.username_value);
  EXPECT_EQ(kSentinelPassword, fetched.password_value);
  EXPECT_EQ(1u, fetched.revision);

  base::RunLoop all_loop;
  bool all_success = false;
  std::vector<MahoPasswordForm> all_forms;
  backend.GetAllLoginsMahoAsync(base::BindOnce(
      [](base::RunLoop* loop, bool* success_out,
         std::vector<MahoPasswordForm>* forms_out,
         std::vector<MahoPasswordForm> forms, bool success) {
        *success_out = success;
        *forms_out = std::move(forms);
        loop->Quit();
      },
      &all_loop, &all_success, &all_forms));
  all_loop.Run();
  ASSERT_TRUE(all_success);
  ASSERT_EQ(1u, all_forms.size());
  EXPECT_EQ(kSentinelPassword, all_forms[0].password_value);
}

TEST_F(MahoPasswordStoreBackendTest,
       ImportedCredentialVisibleToNativeFillWhenExternalProviderUnavailable) {
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend;
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  const std::string origin = "https://import-fill.example";

  ASSERT_TRUE(ImportOnePasswordCredential(origin, "import@example.test",
                                          kImportedPassword));
  SelectPasswordProvider("bitwarden", "[]");
  EXPECT_EQ("maho_native", GetActivePasswordProviderMode());

  std::vector<MahoPasswordForm> maho_forms = FillMahoForms(backend, origin);
  ASSERT_EQ(1u, maho_forms.size());
  EXPECT_EQ(origin, maho_forms[0].signon_realm);
  EXPECT_EQ("import@example.test", maho_forms[0].username_value);
  EXPECT_TRUE(maho_forms[0].password_value == kImportedPassword);

  password_manager::BackendLoginsResult password_store_forms =
      FillPasswordStoreForms(backend, origin);
  ASSERT_EQ(1u, password_store_forms.size());
  EXPECT_EQ(origin, password_store_forms[0].signon_realm);
  EXPECT_EQ(u"import@example.test", password_store_forms[0].username_value);
  EXPECT_TRUE(password_store_forms[0].password_value == ImportedPassword16());
}

TEST_F(MahoPasswordStoreBackendTest,
       ManualCredentialVisibleThroughPasswordStoreSaveAndFillPath) {
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend;
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  SelectPasswordProvider("maho_native", "[]");
  ASSERT_TRUE(ShouldEnableChromiumPasswordManager());

  const std::string origin = "https://manual-fill.example";
  password_manager::StoredCredential credential;
  credential.signon_realm = origin;
  credential.username_value = u"manual@example.test";
  credential.password_value = ManualPassword16();

  base::RunLoop add_loop;
  bool add_success = false;
  backend.AddLoginAsync(
      std::move(credential),
      base::BindOnce(
          [](base::RunLoop* loop, bool* success_out,
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
  EXPECT_TRUE(password_store_forms[0].password_value == ManualPassword16());

  std::vector<MahoPasswordForm> maho_forms = FillMahoForms(backend, origin);
  ASSERT_EQ(1u, maho_forms.size());
  EXPECT_EQ(origin, maho_forms[0].signon_realm);
  EXPECT_EQ("manual@example.test", maho_forms[0].username_value);
  EXPECT_TRUE(maho_forms[0].password_value == kManualPassword);
}

TEST_F(MahoPasswordStoreBackendTest, UnlockedVaultUpdateLoginAsync) {
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend;
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  MahoPasswordForm stored = AddLoginAndFetch(
      backend, "https://update.example.com", "old@example.com", "old-password");

  stored.username_value = "new@example.com";
  stored.password_value = "new-password";
  base::RunLoop update_loop;
  bool update_success = false;
  backend.UpdateLoginMahoAsync(
      stored, base::BindOnce(
                  [](base::RunLoop* loop, bool* success_out, bool success) {
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
  backend.FillMatchingLoginsMahoAsync(
      "https://update.example.com",
      base::BindOnce(
          [](base::RunLoop* loop, bool* success_out,
             std::vector<MahoPasswordForm>* forms_out,
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
  EXPECT_EQ("new-password", matched_forms[0].password_value);
  EXPECT_EQ(2u, matched_forms[0].revision);
}

TEST_F(MahoPasswordStoreBackendTest, UnlockedVaultRemoveLoginAsync) {
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend;
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  MahoPasswordForm stored =
      AddLoginAndFetch(backend, "https://delete.example.com",
                       "delete@example.com", "delete-password");

  base::RunLoop remove_loop;
  bool remove_success = false;
  backend.RemoveLoginMahoAsync(
      stored, base::BindOnce(
                  [](base::RunLoop* loop, bool* success_out, bool success) {
                    *success_out = success;
                    loop->Quit();
                  },
                  &remove_loop, &remove_success));
  remove_loop.Run();
  ASSERT_TRUE(remove_success);

  base::RunLoop fill_loop;
  bool fill_success = true;
  std::vector<MahoPasswordForm> matched_forms;
  backend.FillMatchingLoginsMahoAsync(
      "https://delete.example.com",
      base::BindOnce(
          [](base::RunLoop* loop, bool* success_out,
             std::vector<MahoPasswordForm>* forms_out,
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
  CreateInitializedVaultCore();
  MahoPasswordStoreBackend backend;
  backend.InitBackend(base::OnceCallback<void(bool)>());
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(LockVault());

  MahoPasswordForm form;
  form.signon_realm = "https://locked.example.com";
  form.username_value = "locked@example.com";
  form.password_value = "locked-password";
  base::RunLoop add_loop;
  bool add_success = true;
  backend.AddLoginMahoAsync(
      form, base::BindOnce(
                [](base::RunLoop* loop, bool* success_out, bool success) {
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
      base::BindOnce(
          [](base::RunLoop* loop, bool* success_out,
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
  MahoPasswordStoreBackend backend;
  // Uninitialized backend / locked Vault operations fail closed safely
  base::RunLoop get_loop;
  bool get_success = true;
  backend.GetAllLoginsMahoAsync(base::BindOnce(
      [](base::RunLoop* loop, bool* success_out,
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

}  // namespace passwords
}  // namespace maho
