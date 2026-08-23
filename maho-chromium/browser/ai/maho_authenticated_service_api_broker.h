// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_AUTHENTICATED_SERVICE_API_BROKER_H_
#define MAHO_BROWSER_AI_MAHO_AUTHENTICATED_SERVICE_API_BROKER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(MAHO_STANDALONE_TEST)
namespace base {
template <typename E>
struct unexpected {
  E error;
  template <typename U>
  explicit unexpected(U&& err) : error(std::forward<U>(err)) {}
};

template <typename E>
unexpected(E) -> unexpected<E>;

template <typename T, typename E>
class expected {
 public:
  expected() : val_{}, err_{}, has_val_(true) {}
  expected(const T& val) : val_(val), err_{}, has_val_(true) {}
  expected(T&& val) : val_(std::move(val)), err_{}, has_val_(true) {}
  template <typename E2>
  expected(const unexpected<E2>& unexp) : val_{}, err_(unexp.error), has_val_(false) {}
  template <typename E2>
  expected(unexpected<E2>&& unexp) : val_{}, err_(std::move(unexp.error)), has_val_(false) {}
  expected(const expected&) = default;
  expected& operator=(const expected&) = default;
  expected(expected&&) noexcept = default;
  expected& operator=(expected&&) noexcept = default;

  bool has_value() const { return has_val_; }
  explicit operator bool() const { return has_val_; }
  const T& value() const { return val_; }
  T& value() { return val_; }
  const T& operator*() const { return val_; }
  T& operator*() { return val_; }
  const T* operator->() const { return &val_; }
  T* operator->() { return &val_; }
  const E& error() const { return err_; }
  E& error() { return err_; }
  T value_or(T default_val) const { return has_val_ ? val_ : default_val; }

 private:
  T val_{};
  E err_{};
  bool has_val_ = false;
};

template <typename Signature>
class OnceCallback;

template <typename R, typename... Args>
class OnceCallback<R(Args...)> {
 public:
  OnceCallback() = default;
  template <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, OnceCallback>>>
  OnceCallback(F&& f)
      : func_(std::make_shared<std::function<R(Args...)>>(std::forward<F>(f))) {}

  R Run(Args... args) {
    if (func_ && *func_) {
      auto f = std::move(*func_);
      func_ = nullptr;
      return f(std::forward<Args>(args)...);
    }
    return R();
  }

  explicit operator bool() const { return func_ && static_cast<bool>(*func_); }

 private:
  std::shared_ptr<std::function<R(Args...)>> func_;
};

template <typename Signature>
class RepeatingCallback;

template <typename R, typename... Args>
class RepeatingCallback<R(Args...)> {
 public:
  RepeatingCallback() = default;
  template <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, RepeatingCallback>>>
  RepeatingCallback(F&& f)
      : func_(std::make_shared<std::function<R(Args...)>>(std::forward<F>(f))) {}

  R Run(Args... args) const {
    if (func_ && *func_) {
      return (*func_)(std::forward<Args>(args)...);
    }
    return R();
  }

  explicit operator bool() const { return func_ && static_cast<bool>(*func_); }

 private:
  std::shared_ptr<std::function<R(Args...)>> func_;
};

template <typename T>
T* Unretained(T* ptr) {
  return ptr;
}

template <typename F, typename... BoundArgs>
auto BindOnce(F&& f, BoundArgs&&... bound_args) {
  return [f = std::forward<F>(f), ... args = std::forward<BoundArgs>(bound_args)](
             auto&&... rest) mutable {
    return std::invoke(f, args..., std::forward<decltype(rest)>(rest)...);
  };
}

template <typename F, typename... BoundArgs>
auto BindRepeating(F&& f, BoundArgs&&... bound_args) {
  return [f = std::forward<F>(f), ... args = std::forward<BoundArgs>(bound_args)](
             auto&&... rest) {
    return std::invoke(f, args..., std::forward<decltype(rest)>(rest)...);
  };
}

class TimeDelta {
 public:
  constexpr TimeDelta() = default;
  static constexpr TimeDelta Seconds(int64_t s) { return TimeDelta(s * 1000); }
  static constexpr TimeDelta Milliseconds(int64_t ms) { return TimeDelta(ms); }
  int64_t InSeconds() const { return ms_ / 1000; }
  int64_t InMilliseconds() const { return ms_; }

 private:
  constexpr explicit TimeDelta(int64_t ms) : ms_(ms) {}
  int64_t ms_ = 0;
};

class FilePath {
 public:
  FilePath() = default;
  explicit FilePath(std::string path) : path_(std::move(path)) {}
  const std::string& value() const { return path_; }
  bool empty() const { return path_.empty(); }

 private:
  std::string path_;
};

struct Location {
  const char* function_name = "";
  const char* file_name = "";
  int line_number = 0;
};
#ifndef FROM_HERE
#define FROM_HERE ::base::Location{__FUNCTION__, __FILE__, __LINE__}
#endif

class RepeatingTimer {
 public:
  RepeatingTimer() = default;
  ~RepeatingTimer() = default;
  void Start(const Location& /*posted_from*/,
             TimeDelta /*delay*/,
             RepeatingCallback<void()> task) {
    task_ = std::move(task);
    running_ = true;
  }
  void Start(RepeatingCallback<void()> task) {
    task_ = std::move(task);
    running_ = true;
  }
  void Stop() { running_ = false; }
  bool IsRunning() const { return running_; }
  void Fire() {
    if (running_ && task_) task_.Run();
  }

 private:
  RepeatingCallback<void()> task_;
  bool running_ = false;
};

}  // namespace base
#else
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/types/expected.h"
#include "base/values.h"
#endif

class Profile;

namespace maho::ai {

// Service provider kinds supported by direct zero-tab API execution.
enum class DirectApiServiceKind {
  kGmail,
  kGoogleDrive,
  kGoogleCalendar,
  kGoogleSheets,
  kSlack,
  kDiscord,
  kTelegram,
  kUnknown,
};

std::string_view ServiceKindToString(DirectApiServiceKind kind);
DirectApiServiceKind ServiceKindFromString(std::string_view name);

// Error codes returned in typed direct API failures.
enum class DirectApiErrorCode {
  kOk,
  kScopeDenied,
  kMissingCredentials,
  kInvalidAuthHandle,
  kConfirmationRequired,
  kInvalidApprovalToken,
  kApprovalTokenAlreadyUsed,
  kRateLimited,
  kNetworkError,
  kServiceUnavailable,
  kResourceNotFound,
  kBadRequest,
  kOperationCancelled,
  kInternalError,
  kTypedUnavailable,
};

std::string_view ErrorCodeToString(DirectApiErrorCode code);
DirectApiErrorCode ErrorCodeFromString(std::string_view str);

// Structured request descriptor for a direct API operation.
// Invariant: Contains provider, operation, scopes, and parameters ONLY.
// Guaranteed to contain no secret or credential fields.
struct DirectApiOpDescriptor {
  DirectApiOpDescriptor();
  DirectApiOpDescriptor(DirectApiServiceKind service,
                        std::string operation_name,
                        std::string parameters_json = "{}",
                        bool read_only = true,
                        std::vector<std::string> required_scopes = {});
  ~DirectApiOpDescriptor();
  DirectApiOpDescriptor(const DirectApiOpDescriptor&);
  DirectApiOpDescriptor& operator=(const DirectApiOpDescriptor&);
  DirectApiOpDescriptor(DirectApiOpDescriptor&&) noexcept;
  DirectApiOpDescriptor& operator=(DirectApiOpDescriptor&&) noexcept;

  DirectApiServiceKind service = DirectApiServiceKind::kUnknown;
  std::string operation_name;
  std::string parameters_json = "{}";
  bool read_only = true;
  std::vector<std::string> required_scopes;
  std::string endpoint_url;
  std::string http_method;
};

// Execution context carrying non-credential session identifiers.
// Invariant: NEVER contains raw cookies, tokens, or headers.
struct DirectApiExecutionContext {
  DirectApiExecutionContext();
  explicit DirectApiExecutionContext(std::string session_id);
  ~DirectApiExecutionContext();
  DirectApiExecutionContext(const DirectApiExecutionContext&);
  DirectApiExecutionContext& operator=(const DirectApiExecutionContext&);

  std::string session_id;
  std::string account_id;
  std::string opaque_auth_handle;

  std::string ToJson() const;
  std::string ToDebugString() const;
};

// Model-facing direct API request structure.
// Contains provider, operation, scopes, and parameters ONLY.
struct ModelFacingDirectApiRequest {
  ModelFacingDirectApiRequest();
  ~ModelFacingDirectApiRequest();
  ModelFacingDirectApiRequest(const ModelFacingDirectApiRequest&);
  ModelFacingDirectApiRequest& operator=(const ModelFacingDirectApiRequest&);
  ModelFacingDirectApiRequest(ModelFacingDirectApiRequest&&);
  ModelFacingDirectApiRequest& operator=(ModelFacingDirectApiRequest&&);

  static ModelFacingDirectApiRequest FromOp(const DirectApiOpDescriptor& op);

  DirectApiServiceKind service = DirectApiServiceKind::kUnknown;
  std::string operation_name;
  std::vector<std::string> scopes;
  std::string parameters_json;
  bool read_only = true;

  std::string ToJson() const;
};

// High-level outcome kind.
enum class DirectApiOutcomeStatus {
  kOk,
  kNeedsConfirmation,
  kForbidden,
  kTypedError,
  kTypedUnavailable,
  kCancelled,
};

std::string_view OutcomeStatusToString(DirectApiOutcomeStatus status);

// High-level fallback representation matching Rust DirectApiOutcome.
struct HighLevelDirectApiOutcome {
  enum class Type {
    kSupported,
    kTypedUnavailable,
    kHardFailure,
  };

  Type type = Type::kSupported;
  std::string payload_or_reason;
  std::string error_code;
  bool can_fallback_to_tabs = false;
  bool is_policy_denial = false;
};

// Typed execution outcome from the Direct API broker.
// Invariant: Raw HTML/HTTP dumps are strictly sanitized into structured fields.
// Invariant: Secret credentials and cookie values are NEVER inlined into outcomes or logs.
struct DirectApiExecutionOutcome {
  DirectApiExecutionOutcome();
  ~DirectApiExecutionOutcome();
  DirectApiExecutionOutcome(const DirectApiExecutionOutcome&);
  DirectApiExecutionOutcome& operator=(const DirectApiExecutionOutcome&);
  DirectApiExecutionOutcome(DirectApiExecutionOutcome&&) noexcept;
  DirectApiExecutionOutcome& operator=(DirectApiExecutionOutcome&&) noexcept;

  static DirectApiExecutionOutcome MakeOk(DirectApiServiceKind service,
                                          std::string op_name,
                                          std::string payload_json);
  static DirectApiExecutionOutcome MakeNeedsConfirmation(
      std::string action_id,
      std::string description,
      std::vector<std::string> required_scopes);
  static DirectApiExecutionOutcome MakeForbidden(
      std::string reason,
      std::vector<std::string> missing_scopes);
  static DirectApiExecutionOutcome MakeTypedError(DirectApiErrorCode code,
                                                  std::string message,
                                                  bool retryable);
  static DirectApiExecutionOutcome MakeTypedUnavailable(
      std::string reason,
      bool can_fallback_to_tabs);
  static DirectApiExecutionOutcome MakeCancelled(std::string reason);

  DirectApiOutcomeStatus status = DirectApiOutcomeStatus::kOk;
  DirectApiServiceKind service = DirectApiServiceKind::kUnknown;
  std::string operation_name;
  std::string payload_json;
  std::string action_id;
  std::string description;
  std::vector<std::string> required_scopes;
  std::string reason;
  std::vector<std::string> missing_scopes;
  DirectApiErrorCode error_code = DirectApiErrorCode::kOk;
  std::string error_message;
  bool retryable = false;
  bool can_fallback_to_tabs = false;
  bool is_policy_denial = false;

  HighLevelDirectApiOutcome ToHighLevelOutcome() const;
  std::string ToJson() const;
  std::string ToDebugString() const;
};

// Vault credential containing secret string, strictly held internally and never
// serialized into logs or model-facing output.
struct VaultCredential {
  VaultCredential();
  VaultCredential(std::string token_type, std::string secret);
  ~VaultCredential();
  VaultCredential(const VaultCredential&);
  VaultCredential& operator=(const VaultCredential&);

  static VaultCredential Bearer(std::string token);
  static VaultCredential Cookie(std::string cookie_header);

  std::string token_type;
  std::string secret;

  // Formats the Authorization / Cookie header value for transport injection only.
  std::string FormatAuthHeader() const;

  // Debug string always redacts secrets with [REDACTED].
  std::string ToDebugString() const;
};

// Credential vault storage interface.
class CredentialVault {
 public:
  virtual ~CredentialVault() = default;
  virtual std::optional<VaultCredential> GetCredential(
      const std::string& handle) const = 0;
  virtual void StoreCredential(const std::string& handle,
                               VaultCredential cred) = 0;
};

// In-memory implementation of CredentialVault.
class InMemoryCredentialVault : public CredentialVault {
 public:
  InMemoryCredentialVault();
  ~InMemoryCredentialVault() override;

  std::optional<VaultCredential> GetCredential(
      const std::string& handle) const override;
  void StoreCredential(const std::string& handle,
                       VaultCredential cred) override;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, VaultCredential> credentials_;
};

// Store managing single-use confirmation approval tokens.
class ApprovalTokenStore {
 public:
  virtual ~ApprovalTokenStore() = default;
  virtual std::string IssueToken(const std::string& action_id) = 0;
  virtual DirectApiErrorCode ConsumeToken(const std::string& action_id,
                                          const std::string& token) = 0;
};

// In-memory implementation of ApprovalTokenStore.
class InMemoryApprovalTokenStore : public ApprovalTokenStore {
 public:
  InMemoryApprovalTokenStore();
  ~InMemoryApprovalTokenStore() override;

  std::string IssueToken(const std::string& action_id) override;
  DirectApiErrorCode ConsumeToken(const std::string& action_id,
                                  const std::string& token) override;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::unordered_set<std::string>> valid_tokens_;
  std::unordered_set<std::string> consumed_tokens_;
  uint64_t counter_ = 0;
};

// Registry tracking granted scopes per account/handle and service.
class ScopeRegistry {
 public:
  virtual ~ScopeRegistry() = default;
  virtual std::unordered_set<std::string> GetGrantedScopes(
      const std::string& handle_or_account,
      DirectApiServiceKind service) const = 0;
  virtual void GrantScopes(const std::string& handle_or_account,
                           DirectApiServiceKind service,
                           const std::vector<std::string>& scopes) = 0;
  virtual bool CheckScopes(const std::string& handle_or_account,
                           DirectApiServiceKind service,
                           const std::vector<std::string>& required,
                           std::vector<std::string>* out_missing) const;
};

// In-memory implementation of ScopeRegistry.
class InMemoryScopeRegistry : public ScopeRegistry {
 public:
  InMemoryScopeRegistry();
  ~InMemoryScopeRegistry() override;

  std::unordered_set<std::string> GetGrantedScopes(
      const std::string& handle_or_account,
      DirectApiServiceKind service) const override;
  void GrantScopes(const std::string& handle_or_account,
                   DirectApiServiceKind service,
                   const std::vector<std::string>& scopes) override;

 private:
  mutable std::mutex mutex_;
  std::map<std::pair<std::string, DirectApiServiceKind>,
           std::unordered_set<std::string>>
      grants_;
};

// Internal request dispatched to the transport layer.
// Invariant: Contains injected Authorization/Cookie headers. Never leaked to model context.
struct TransportRequest {
  TransportRequest();
  ~TransportRequest();
  TransportRequest(const TransportRequest&);
  TransportRequest& operator=(const TransportRequest&);
  TransportRequest(TransportRequest&&);
  TransportRequest& operator=(TransportRequest&&);

  DirectApiServiceKind service = DirectApiServiceKind::kUnknown;
  std::string operation_name;
  std::string method = "GET";
  std::string url;
  std::unordered_map<std::string, std::string> headers;
  std::string body;
};

// Response returned from the transport layer.
struct TransportResponse {
  TransportResponse();
  ~TransportResponse();
  TransportResponse(const TransportResponse&);
  TransportResponse& operator=(const TransportResponse&);
  TransportResponse(TransportResponse&&);
  TransportResponse& operator=(TransportResponse&&);

  int status_code = 200;
  std::string body;
  std::unordered_map<std::string, std::string> headers;
};

// Errors originating in the transport layer.
enum class TransportErrorKind {
  kNetworkError,
  kTimeout,
  kCancelled,
};

struct TransportError {
  TransportErrorKind kind = TransportErrorKind::kNetworkError;
  std::string message;
};

// Direct API transport trait.
class DirectApiTransport {
 public:
  virtual ~DirectApiTransport() = default;
  using Callback =
      base::OnceCallback<void(base::expected<TransportResponse, TransportError>)>;

  virtual void Send(TransportRequest request, Callback callback) = 0;
};

// In-memory transport for unit tests.
class InMemoryDirectApiTransport : public DirectApiTransport {
 public:
  InMemoryDirectApiTransport();
  ~InMemoryDirectApiTransport() override;

  void SetResponse(
      DirectApiServiceKind service,
      const std::string& operation_name,
      base::expected<TransportResponse, TransportError> response);

  std::vector<TransportRequest> GetSentRequests() const;
  size_t SentCount() const;

  void Send(TransportRequest request, Callback callback) override;

 private:
  mutable std::mutex mutex_;
  std::map<std::pair<DirectApiServiceKind, std::string>,
           base::expected<TransportResponse, TransportError>>
      canned_responses_;
  std::vector<TransportRequest> sent_requests_;
};

// Browser cookie jar direct API transport (WebContents-free profile).
class ProfileCookieJarDirectApiTransport : public DirectApiTransport {
 public:
  explicit ProfileCookieJarDirectApiTransport(Profile* profile);
  ~ProfileCookieJarDirectApiTransport() override;

  void Send(TransportRequest request, Callback callback) override;

  Profile* profile() const { return profile_; }

 private:
  [[maybe_unused]] Profile* profile_ = nullptr;
};

// Main broker for Gap #11: Zero-tab direct API calls with cookie jar auth.
class MahoAuthenticatedServiceApiBroker {
 public:
  using OutcomeCallback =
      base::OnceCallback<void(DirectApiExecutionOutcome)>;

  explicit MahoAuthenticatedServiceApiBroker(Profile* profile = nullptr);
  MahoAuthenticatedServiceApiBroker(
      Profile* profile,
      std::shared_ptr<CredentialVault> vault,
      std::shared_ptr<ApprovalTokenStore> approval_store,
      std::shared_ptr<ScopeRegistry> scope_registry,
      std::unique_ptr<DirectApiTransport> transport);
  ~MahoAuthenticatedServiceApiBroker();

  MahoAuthenticatedServiceApiBroker(
      const MahoAuthenticatedServiceApiBroker&) = delete;
  MahoAuthenticatedServiceApiBroker& operator=(
      const MahoAuthenticatedServiceApiBroker&) = delete;

  // Executes a direct API operation.
  // Enforces confirmation tokens, validates granted scopes, injects auth headers
  // at the transport boundary, and maps outcomes into strictly typed results.
  void ExecuteOp(const DirectApiOpDescriptor& op,
                 const DirectApiExecutionContext& context,
                 const std::optional<std::string>& approval_token,
                 OutcomeCallback callback);

  // Overload using default execution context.
  void ExecuteOp(const DirectApiOpDescriptor& op,
                 const std::optional<std::string>& approval_token,
                 OutcomeCallback callback);

  // Lazily returns the broker instance associated with the profile.
  static MahoAuthenticatedServiceApiBroker* GetForProfile(Profile* profile);

  // Sanitizes upstream error response bodies to prevent raw HTML dumps from entering model context.
  static std::string SanitizeErrorPayload(const std::string& body);

  // Capability-backed registry exposure for direct API operations.
  static std::vector<DirectApiOpDescriptor> GetSupportedOperations();
  static bool IsOperationSupported(DirectApiServiceKind service,
                                   std::string_view op_name);

  // Accessors for internal stores (useful for test setup).
  std::shared_ptr<CredentialVault> vault() const { return vault_; }
  std::shared_ptr<ApprovalTokenStore> approval_store() const {
    return approval_store_;
  }
  std::shared_ptr<ScopeRegistry> scope_registry() const {
    return scope_registry_;
  }

  Profile* profile() const { return profile_; }

 private:
  [[maybe_unused]] Profile* profile_ = nullptr;
  std::shared_ptr<CredentialVault> vault_;
  std::shared_ptr<ApprovalTokenStore> approval_store_;
  std::shared_ptr<ScopeRegistry> scope_registry_;
  std::unique_ptr<DirectApiTransport> transport_;
};

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_AUTHENTICATED_SERVICE_API_BROKER_H_
