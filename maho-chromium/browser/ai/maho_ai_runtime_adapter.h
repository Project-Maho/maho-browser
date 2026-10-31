// Copyright 2026 Maho Browser. All rights reserved.
//
// MahoAiRuntimeAdapter: Abstract interface for the AI runtime backend.
//
// Defines a session-oriented, event-driven contract implemented by
// MahoUnifiedAgentAdapter. The page handler and Mojo proxy talk to this
// interface — they never know backend details. Runtime selection is
// handled by MahoAiRuntimeRouter.
//
// Ownership boundaries:
//   - Browser tools (page context, tool execution) stay local in Maho.
//   - Session lifecycle and agent orchestration belong to the adapter.
//   - The Chromium proxy layer translates between Mojo and this interface.

#ifndef MAHO_BROWSER_AI_MAHO_AI_RUNTIME_ADAPTER_H_
#define MAHO_BROWSER_AI_MAHO_AI_RUNTIME_ADAPTER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/values.h"

namespace maho_ai::mojom {
enum class ChatIntent;
enum class InteractionMode;
}  // namespace maho_ai::mojom

// Event types that a runtime adapter can emit. The unified agent adapter
// emits these natively; the page handler and Mojo proxy consume them.
enum class MahoAiRuntimeEventType {
  // Incremental text content from the assistant.
  kAssistantToken,
  // Incremental thinking/reasoning content from the model.
  kAssistantThinking,
  // The assistant turn is complete.
  kTurnComplete,
  // An error occurred during the turn.
  kError,
  // The runtime requests a browser tool invocation.
  kToolRequest,
  // A tool invocation completed (result flowing back).
  kToolResult,
  // The runtime is requesting user approval before proceeding.
  kApprovalRequest,
  // An approval decision was recorded.
  kApprovalResult,
  // Session connection state changed.
  kConnectionStateChanged,
  // Browser context was injected (or denied) into the prompt.
  // payload carries url, title, content_snippet, content_length, status,
  // warnings — mirroring the Mojo BrowserContextPayload.
  kBrowserContextInjected,
  kToolAvailabilityChanged,
  // An output artifact was created by the agent (fs_write path). text carries
  // the JSON ArtifactInfo; the page handler maps it to the Mojo ArtifactInfo.
  kArtifactCreated,
  // Intermediate status report for long tasks (~30s status cadence).
  kStatus,
  // The agent is requesting interactive input/choice or action confirmation.
  kInteractionRequest,
  // An interaction resolution was recorded.
  kInteractionResult,
  // Replay gap marker when requested events are outside the journal retention window.
  kReplayGap,
  // Turn lifecycle events (turn queued, steered, interrupted, started).
  kTurnQueued,
  kTurnStarted,
  kTurnSteered,
  kTurnInterrupted,
  // Event wait / notification suspend and wake events.
  kWaitSuspended,
  kWaitWoken,
  // Model category routing resolution event.
  kModelResolved,
};

// Closed, non-secret credential failure codes produced by the Rust agent
// runtime. These stay in the C++ runtime contract until a later Mojo revision
// deliberately persists an additive representation.
enum class MahoAiRuntimeErrorCode {
  kProviderNotConfigured,
  kCredentialUnusable,
  kSecureStoreUnavailable,
  kCredentialDecryptFailed,
  kManagedAuthUnavailable,
  kUnsupportedProvider,
};

// A single runtime event. Adapters produce these; the proxy/page handler
// consumes them. Richer event payloads (structured tool calls, approval
// requests) will be added in subsequent phases.
struct MahoAiRuntimeEvent {
  MahoAiRuntimeEvent();
  MahoAiRuntimeEvent(MahoAiRuntimeEvent&&);
  MahoAiRuntimeEvent& operator=(MahoAiRuntimeEvent&&);
  ~MahoAiRuntimeEvent();

  MahoAiRuntimeEventType type = MahoAiRuntimeEventType::kAssistantToken;

  // For kAssistantToken: the token text.
  // For kTurnComplete: the full accumulated text.
  // For kError: the error message.
  // For kStatus: the status message.
  // For kInteractionRequest: the prompt / question.
  std::string text;

  // Run ID from the unified agent event envelope.
  std::string run_id;

  // Monotonic sequence number from the unified agent event envelope.
  std::optional<uint64_t> event_seq;

  // Set only for a strict, allowlisted credential-error envelope from Rust.
  // This never contains provider input, credentials, ciphertext, endpoints,
  // or platform diagnostic text.
  std::optional<MahoAiRuntimeErrorCode> runtime_error_code;

  // For kToolRequest: structured tool call info.
  // For kToolResult: the tool execution result.
  // Empty for other event types for now.
  base::DictValue payload;

  // Resolved model from model category routing.
  std::optional<std::string> resolved_model;

  // Fallback reason if model routing fell back.
  std::optional<std::string> model_fallback_reason;

  // Current queue depth for turn lifecycle badges.
  std::optional<uint32_t> queue_depth;
};

// Session-scoped runtime configuration (plan row-1 plumbing, row-3 file
// gate). The triple mirrors the broker's CapabilityRequestContext fields and
// the maho-ffi AgentRuntimeConfig defaults: tier "guard", final_confirm true,
// proactive_mode false. |permission_tier| is one of "read_only", "guard",
// "full_access"; unknown values fail closed to "guard" broker-side.
// |fs_whitelist_roots| are the session-supplied absolute directory roots for
// the runtime permission-tier file gate (plan row 3); empty means nothing is
// whitelisted (fail closed). Both are stamped live into every
// CapabilityRequestContext (Wave 2A); roots must keep a single provisioning
// source shared with the CLI surface, never per-surface lists.
struct MahoAiRuntimeConfig {
  MahoAiRuntimeConfig();
  MahoAiRuntimeConfig(const MahoAiRuntimeConfig&);
  MahoAiRuntimeConfig& operator=(const MahoAiRuntimeConfig&);
  ~MahoAiRuntimeConfig();

  std::string permission_tier;
  bool final_confirm;
  bool proactive_mode;
  std::vector<std::string> fs_whitelist_roots;
};

// Abstract runtime adapter interface. Each concrete adapter owns its own
// connection to the underlying runtime (e.g., the maho-agent FFI bridge)
// and translates runtime-specific events into the common
// MahoAiRuntimeEvent shape.
class MahoAiRuntimeAdapter {
 public:
  // Callback for individual runtime events during a turn.
  using RuntimeEventCallback =
      base::RepeatingCallback<void(MahoAiRuntimeEvent event)>;

  virtual ~MahoAiRuntimeAdapter() = default;

  // Returns a human-readable name for logging/debugging.
  virtual std::string GetAdapterName() const = 0;

  // Whether this adapter has enough configuration to be usable
  // (e.g., the unified agent adapter requires MahoCore to be initialized
  // and at least one BYOK key in secure storage).
  virtual bool IsAvailable() const = 0;

  // Submit a user message to the runtime. The adapter emits events via
  // |on_event| as the turn progresses and completes.
  //
  // When |attach_browser_context| is true and the intent is kFreeform, the
  // adapter injects current page context (URL, title, content) into the
  // prompt. The caller (page handler) does NOT assemble context.
  //
  // The caller must keep the adapter alive for the duration of the turn.
  // Calling SubmitMessage while a turn is in progress is undefined
  // behavior; the unified agent adapter currently serializes turns.
  virtual void SubmitMessage(const std::string& message,
                             maho_ai::mojom::ChatIntent chat_intent,
                             bool attach_browser_context,
                             maho_ai::mojom::InteractionMode mode,
                             RuntimeEventCallback on_event) = 0;

  // Cancel the current turn, if any. The adapter should emit a
  // kTurnComplete or kError event after cancellation.
  virtual void CancelCurrentTurn() = 0;

  virtual void RespondToApproval(const std::string& approval_id,
                                 bool approved) = 0;

  virtual void RespondToInteraction(const std::string& interaction_id,
                                    const std::string& answer_json);

  // Session runtime configuration (plan row-1 plumbing; enforcement stays at
  // the CapabilityBroker). Defaults report "unsupported": adapters without an
  // FFI session keep the WebUI's runtime-config feature detection off.
  // GetRuntimeConfig returns false when unsupported; the caller then renders
  // the broker defaults (guard / true / false).
  virtual bool GetRuntimeConfig(MahoAiRuntimeConfig* out_config) const;
  virtual bool SetRuntimeConfig(const MahoAiRuntimeConfig& config);

  virtual bool ReplayAfter(const std::string& run_id,
                           uint64_t after_seq,
                           RuntimeEventCallback on_event);

  virtual uint64_t GetLastConsumedEventSeq(const std::string& run_id) const;

  // Returns true if a turn is currently in progress.
  virtual bool IsTurnActive() const;

  // Submits a follow-up message when a turn is already active.
  // |intent| can be "queue" (default), "steer", "interrupt", or "continue".
  // Returns true if successfully submitted to the turn controller / queue.
  virtual bool SubmitFollowUp(const std::string& message,
                              const std::string& intent);

  // Returns the current queued follow-up turns count.
  virtual uint32_t GetTurnQueueDepth() const;

  // Wakes a suspended run waiting on external notifications.
  // Wave 3 consumers and notification services invoke this hook.
  virtual bool WakeForNotification(const std::string& run_id,
                                   const std::string& event_json);

  // Registers a wait filter on external notifications with a timeout.
  virtual std::string RegisterWait(const std::string& run_id,
                                   const std::string& filter_json,
                                   uint64_t timeout_ms);

  // Returns the runtime-owned session ID, if one exists. Empty string if
  // no session is active or the adapter does not use external session
  // identifiers.
  virtual std::string GetRuntimeSessionId() const = 0;

  // Notifies the runtime adapter that a new session has started or the active
  // session has changed. Adapters should reset turn state and switch underlying
  // FFI/backend sessions.
  virtual void StartSession(const std::string& session_id);
  virtual void ResetSession(const std::string& session_id);

  virtual void Reset() = 0;
};

#endif  // MAHO_BROWSER_AI_MAHO_AI_RUNTIME_ADAPTER_H_
