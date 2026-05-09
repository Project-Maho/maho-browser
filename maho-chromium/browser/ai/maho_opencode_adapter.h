// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_OPENCODE_ADAPTER_H_
#define MAHO_BROWSER_AI_MAHO_OPENCODE_ADAPTER_H_

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "maho/browser/ai/maho_ai_page_context_extractor.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"
#include "url/gurl.h"

class Browser;
class MahoBrowserToolExecutor;
class MahoBrowserToolRegistry;
class PrefService;

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
class SimpleURLLoaderStreamConsumer;
}  // namespace network

class MahoOpenCodeSseConsumer;

// Real OpenCode server-mode adapter. Talks to a running OpenCode server
// via HTTP REST + SSE:
//   - POST /session            -> create session
//   - POST /session/:id/prompt_async -> submit prompt
//   - GET  /event              -> SSE event subscription
//   - POST /session/:id/abort  -> cancel turn
//   - POST /session/:id/permissions/:pid -> approval reply
//
// Browser-local privileged tools remain in Maho (MahoBrowserToolExecutor).
// The adapter does NOT assume the server can suspend/resume around
// external browser tool callbacks — Maho owns that loop if needed.
class MahoOpenCodeAdapter : public MahoAiRuntimeAdapter {
 public:
  MahoOpenCodeAdapter(
      Browser* browser,
      PrefService* prefs,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~MahoOpenCodeAdapter() override;

  MahoOpenCodeAdapter(const MahoOpenCodeAdapter&) = delete;
  MahoOpenCodeAdapter& operator=(const MahoOpenCodeAdapter&) = delete;

  std::string GetAdapterName() const override;
  bool IsAvailable() const override;
  void SubmitMessage(const std::string& message,
                     maho_ai::mojom::ChatIntent chat_intent,
                     RuntimeEventCallback on_event) override;
  void CancelCurrentTurn() override;
  void RespondToApproval(const std::string& approval_id,
                         bool approved) override;
  std::string GetRuntimeSessionId() const override;
  void Reset() override;

 private:
  friend class MahoOpenCodeSseConsumer;

  GURL GetServerBaseUrl() const;
  void EnsureEventStreamConnected();
  void CreateSession();
  void OnCreateSessionResponse(std::optional<std::string> body);
  void SubmitPromptToSession(const std::string& prompt);
  void OnPromptResponse(std::optional<std::string> body);

  void SubmitDirectMessage(const std::string& prompt);
  void OnDirectMessageResponse(std::optional<std::string> body);

  void OnSseDataReceived(std::string chunk);
  void OnSseComplete(bool success);
  void ProcessSseEvent(const std::string& event_type,
                       const std::string& data);
  void EmitConnectionState(const std::string& state);

  // Phase 3: Maho-owned local multi-round browser-tool loop.
  // When the OpenCode agent invokes a tool that matches a Maho browser-local
  // tool, Maho intercepts and executes it locally, emits tool request/result
  // events to the UI, then feeds the structured result back to OpenCode as
  // a follow-up prompt_async submission.
  std::string BuildBrowserContextPrefix();
  std::string BuildIntentInstruction(maho_ai::mojom::ChatIntent intent);
  bool IsPageIntent(maho_ai::mojom::ChatIntent intent) const;
  void OnPageContextForIntent(
      const std::string& message,
      maho_ai::mojom::ChatIntent chat_intent,
      RuntimeEventCallback on_event,
      MahoAiPageContextExtractor::PageContextResult page_context);
  bool IsBrowserLocalTool(const std::string& tool_name) const;
  void ExecuteLocalBrowserTool(const std::string& tool_name,
                               const std::string& call_id,
                               const base::DictValue& arguments);
  void OnLocalToolResult(const std::string& tool_name,
                         const std::string& call_id,
                         base::DictValue result);
  void SubmitToolResultFollowUp(const std::string& tool_name,
                                const std::string& call_id,
                                const base::DictValue& result);

  // Phase 5: Approval pause/resume. The OpenCode server pauses execution
  // while a permission is pending. The adapter tracks this explicitly so
  // runtime state reflects "paused_for_approval" rather than ambiguous silence.
  void OnApprovalTimeout();

  SEQUENCE_CHECKER(sequence_checker_);
  raw_ptr<Browser> browser_;
  raw_ptr<PrefService> prefs_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  std::string session_id_;
  RuntimeEventCallback current_on_event_;
  std::string accumulated_text_;
  std::unordered_map<std::string, std::string> part_types_;
  bool awaiting_direct_response_ = false;

  std::unique_ptr<network::SimpleURLLoader> prompt_loader_;
  std::unique_ptr<network::SimpleURLLoader> direct_message_loader_;
  std::unique_ptr<network::SimpleURLLoader> session_loader_;
  std::unique_ptr<network::SimpleURLLoader> cancel_loader_;
  std::unique_ptr<network::SimpleURLLoader> permission_loader_;

  std::unique_ptr<network::SimpleURLLoader> sse_loader_;
  std::unique_ptr<MahoOpenCodeSseConsumer> sse_consumer_;
  std::string sse_buffer_;
  bool sse_connected_ = false;

  std::string pending_prompt_;
  bool pending_is_direct_message_ = false;

  // Phase 3: browser-local tool loop state.
  static constexpr int kMaxLocalToolRounds = 5;
  std::unique_ptr<MahoBrowserToolExecutor> tool_executor_;
  MahoAiPageContextExtractor page_context_extractor_;
  int local_tool_round_count_ = 0;

  // Phase 5: approval state.
  static constexpr base::TimeDelta kApprovalTimeout = base::Minutes(2);
  std::string pending_approval_id_;
  base::OneShotTimer approval_timeout_timer_;

  base::WeakPtrFactory<MahoOpenCodeAdapter> weak_factory_{this};
};

#endif  // MAHO_BROWSER_AI_MAHO_OPENCODE_ADAPTER_H_
