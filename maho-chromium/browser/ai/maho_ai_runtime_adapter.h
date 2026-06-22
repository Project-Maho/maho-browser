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
  std::string text;

  // For kToolRequest: structured tool call info.
  // For kToolResult: the tool execution result.
  // Empty for other event types for now.
  base::DictValue payload;
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

  // Returns the runtime-owned session ID, if one exists. Empty string if
  // no session is active or the adapter does not use external session
  // identifiers.
  virtual std::string GetRuntimeSessionId() const = 0;

  virtual void Reset() = 0;
};

#endif  // MAHO_BROWSER_AI_MAHO_AI_RUNTIME_ADAPTER_H_
