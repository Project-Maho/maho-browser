// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_LEGACY_CHAT_ADAPTER_H_
#define MAHO_BROWSER_AI_MAHO_LEGACY_CHAT_ADAPTER_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"
#include "maho/browser/ai/maho_chat_orchestration_service.h"

class Browser;
class PrefService;

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

// Wraps the existing MahoChatOrchestrationService behind the
// MahoAiRuntimeAdapter interface. This preserves the current Rust-bridge
// chat path as a working fallback while the OpenCode path is built out.
class MahoLegacyChatAdapter : public MahoAiRuntimeAdapter {
 public:
  MahoLegacyChatAdapter(
      Browser* browser,
      PrefService* prefs,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~MahoLegacyChatAdapter() override;

  MahoLegacyChatAdapter(const MahoLegacyChatAdapter&) = delete;
  MahoLegacyChatAdapter& operator=(const MahoLegacyChatAdapter&) = delete;

  std::string GetAdapterName() const override;
  bool IsAvailable() const override;
  void SubmitMessage(const std::string& message,
                     maho_ai::mojom::ChatIntent chat_intent,
                     bool attach_browser_context,
                     maho_ai::mojom::InteractionMode mode,
                     RuntimeEventCallback on_event) override;
  void CancelCurrentTurn() override;
  void RespondToApproval(const std::string& approval_id,
                         bool approved) override;
  std::string GetRuntimeSessionId() const override;
  void Reset() override;

 private:
  void OnToken(RuntimeEventCallback on_event, const std::string& token);
  void OnComplete(RuntimeEventCallback on_event,
                  const std::string& full_text);
  void OnError(RuntimeEventCallback on_event, const std::string& error);

  SEQUENCE_CHECKER(sequence_checker_);
  MahoChatOrchestrationService orchestration_service_;
  base::WeakPtrFactory<MahoLegacyChatAdapter> weak_factory_{this};
};

#endif  // MAHO_BROWSER_AI_MAHO_LEGACY_CHAT_ADAPTER_H_
