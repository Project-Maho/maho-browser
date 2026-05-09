// Copyright 2026 Maho Browser. All rights reserved.
//
// MahoAiRuntimeAdapter: Abstract interface for the AI runtime backend.
//
// Defines a session-oriented, event-driven contract implemented by
// MahoOpenCodeAdapter. The page handler and Mojo proxy talk to this
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
}  // namespace maho_ai::mojom

// Event types that a runtime adapter can emit. This is the beginning of
// the event-oriented contract that replaces the flat token/complete/error
// callback shape. For now, the legacy adapter maps its callbacks into
// these categories; the OpenCode adapter will emit them natively.
enum class MahoAiRuntimeEventType {
  // Incremental text content from the assistant.
  kAssistantToken,
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
// connection to the underlying runtime (legacy Rust bridge, OpenCode
// server, etc.) and translates runtime-specific events into the common
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
  // (e.g., API key for legacy, server connection for OpenCode).
  virtual bool IsAvailable() const = 0;

  // Submit a user message to the runtime. The adapter emits events via
  // |on_event| as the turn progresses and completes.
  //
  // The caller must keep the adapter alive for the duration of the turn.
  // Calling SubmitMessage while a turn is in progress is undefined
  // behavior for now (the legacy adapter serializes; OpenCode may queue).
  virtual void SubmitMessage(const std::string& message,
                             maho_ai::mojom::ChatIntent chat_intent,
                             RuntimeEventCallback on_event) = 0;

  // Cancel the current turn, if any. The adapter should emit a
  // kTurnComplete or kError event after cancellation.
  virtual void CancelCurrentTurn() = 0;

  virtual void RespondToApproval(const std::string& approval_id,
                                 bool approved) = 0;

  // Returns the runtime-owned session ID (e.g. the OpenCode server session
  // ID), if one exists. Empty string if no session is active or the adapter
  // does not use external session identifiers.
  virtual std::string GetRuntimeSessionId() const = 0;

  virtual void Reset() = 0;
};

#endif  // MAHO_BROWSER_AI_MAHO_AI_RUNTIME_ADAPTER_H_
