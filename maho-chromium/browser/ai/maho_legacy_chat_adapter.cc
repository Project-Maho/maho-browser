// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_legacy_chat_adapter.h"

#include <utility>

#include "base/functional/bind.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

MahoLegacyChatAdapter::MahoLegacyChatAdapter(
    Browser* browser,
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : orchestration_service_(browser, prefs, url_loader_factory) {}

MahoLegacyChatAdapter::~MahoLegacyChatAdapter() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

std::string MahoLegacyChatAdapter::GetAdapterName() const {
  return "legacy-chat";
}

bool MahoLegacyChatAdapter::IsAvailable() const {
  return orchestration_service_.HasConfiguredCredential();
}

void MahoLegacyChatAdapter::SubmitMessage(
    const std::string& message,
    maho_ai::mojom::ChatIntent chat_intent,
    RuntimeEventCallback on_event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  orchestration_service_.HandleMessage(
      message, chat_intent,
      base::BindRepeating(&MahoLegacyChatAdapter::OnToken,
                          weak_factory_.GetWeakPtr(), on_event),
      base::BindOnce(&MahoLegacyChatAdapter::OnComplete,
                     weak_factory_.GetWeakPtr(), on_event),
      base::BindOnce(&MahoLegacyChatAdapter::OnError,
                     weak_factory_.GetWeakPtr(), on_event));
}

void MahoLegacyChatAdapter::CancelCurrentTurn() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void MahoLegacyChatAdapter::RespondToApproval(const std::string& approval_id,
                                               bool approved) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

std::string MahoLegacyChatAdapter::GetRuntimeSessionId() const {
  return std::string();
}

void MahoLegacyChatAdapter::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  orchestration_service_.Reset();
}

void MahoLegacyChatAdapter::OnToken(RuntimeEventCallback on_event,
                                    const std::string& token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kAssistantToken;
  event.text = token;
  on_event.Run(std::move(event));
}

void MahoLegacyChatAdapter::OnComplete(RuntimeEventCallback on_event,
                                       const std::string& full_text) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kTurnComplete;
  event.text = full_text;
  on_event.Run(std::move(event));
}

void MahoLegacyChatAdapter::OnError(RuntimeEventCallback on_event,
                                    const std::string& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoAiRuntimeEvent event;
  event.type = MahoAiRuntimeEventType::kError;
  event.text = error;
  on_event.Run(std::move(event));
}
