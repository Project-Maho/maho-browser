// Copyright 2026 Maho Browser. All rights reserved.
#ifndef MAHO_BROWSER_AI_MAHO_AGENT_CHANNEL_SERVICE_H_
#define MAHO_BROWSER_AI_MAHO_AGENT_CHANNEL_SERVICE_H_

#include <map>
#include <memory>
#include <string>
#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/keyed_service/core/keyed_service.h"
#include "maho/browser/ai/maho_agent_channel_gateway.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"

class Profile;
class MahoUnifiedAgentAdapter;
namespace maho::ai {
// Profile-owned, never borrowed from the personal AI panel.
class MahoAgentChannelService : public KeyedService {
 public:
  static MahoAgentChannelService* GetForProfile(Profile* profile);
  explicit MahoAgentChannelService(Profile* profile);
  ~MahoAgentChannelService() override;
  void Shutdown() override;
  std::string ListChannels() const;
  void ConfigureChannel(std::string json,
                        base::OnceCallback<void(const std::string&)> callback);
  std::string RemoveChannel(const std::string& id);
  std::string ApprovePairing(const std::string& id,
                            const std::string& sender,
                            const std::string& code);
  std::string RevokePairing(const std::string& id, const std::string& sender);
  void SendReply(const std::string& id, const std::string& reply_id,
                 base::OnceCallback<void(const std::string&)> callback);

 private:
  void OnLoaded(std::string json);
  void OnLockChanged(bool locked);
  void Rehydrate();
  void OnCredential(uint64_t generation, ChannelConfig config,
                    base::OnceCallback<void(const std::string&)> callback,
                    std::optional<ChannelToken> token);
  bool Dispatch(const AgentSessionTrigger& trigger);
  void OnRuntimeEvent(const std::string& id, MahoAiRuntimeEvent event);
  raw_ptr<Profile> profile_;
  MahoAuthenticatedServiceApiBroker broker_;
  MahoAgentChannelGateway gateway_;
  std::map<std::string, std::unique_ptr<MahoUnifiedAgentAdapter>> sessions_;
  std::map<std::string, std::string> results_;
  std::map<std::string, OutboundChannelMessage> replies_;
  std::map<std::string, std::string> reply_ids_;
  bool loaded_ = false;
  bool shutdown_ = false;
  uint64_t generation_ = 0;
  base::CallbackListSubscription lock_subscription_;
  base::CallbackListSubscription core_subscription_;
  base::WeakPtrFactory<MahoAgentChannelService> weak_factory_{this};
};
}  // namespace maho::ai
#endif
