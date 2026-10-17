// Copyright 2026 Maho Browser. All rights reserved.
#include "maho/browser/ai/maho_agent_channel_service.h"

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/ai/maho_unified_agent_adapter.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"

namespace maho::ai {
namespace {
std::string Result(bool success, std::string error = {}) {
  return base::WriteJson(base::DictValue().Set("success", success)
                            .Set("error", std::move(error))).value();
}

std::optional<ChannelToken> ReadCredential(std::string id) {
  if (!maho::GetCore()) return std::nullopt;
  const auto request = base::WriteJson(base::DictValue().Set("id", id)).value();
  std::string response = maho::core::VaultGetNotesJson(
      maho::GetCore(), request.c_str());
  auto parsed = base::JSONReader::ReadDict(response, base::JSON_PARSE_RFC);
  maho::core::SecureClearString(&response);
  auto* data = parsed && parsed->FindBool("ok").value_or(false)
      ? parsed->FindDict("data") : nullptr;
  auto* notes = data ? data->FindString("notes") : nullptr;
  if (!notes || notes->empty()) return std::nullopt;
  ChannelToken token(*notes);
  maho::core::SecureClearString(notes);
  return token;
}

class ChannelServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static ChannelServiceFactory* GetInstance() {
    static base::NoDestructor<ChannelServiceFactory> instance;
    return instance.get();
  }
  MahoAgentChannelService* Get(Profile* profile) {
    return static_cast<MahoAgentChannelService*>(
        GetServiceForBrowserContext(profile, true));
  }
 private:
  friend base::NoDestructor<ChannelServiceFactory>;
  ChannelServiceFactory()
      : ProfileKeyedServiceFactory("MahoAgentChannels",
                                    ProfileSelections::BuildForRegularProfile()) {}
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override {
    return std::make_unique<MahoAgentChannelService>(
        Profile::FromBrowserContext(context));
  }
};
}  // namespace

MahoAgentChannelService* MahoAgentChannelService::GetForProfile(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) return nullptr;
  return ChannelServiceFactory::GetInstance()->Get(profile);
}

MahoAgentChannelService::MahoAgentChannelService(Profile* profile)
    : profile_(profile), broker_(profile), gateway_(nullptr, &broker_) {
  gateway_.ConfigureProductionTransports(profile);
  // A WeakPtr cannot bind a method with a return value; a destroyed service
  // declines the trigger.
  gateway_.SetTriggerCallback(base::BindRepeating(
      [](base::WeakPtr<MahoAgentChannelService> self,
         const AgentSessionTrigger& trigger) {
        return self && self->Dispatch(trigger);
      },
      weak_factory_.GetWeakPtr()));
  lock_subscription_ = maho::AddVaultLockStateChangeCallback(base::BindRepeating(
      &MahoAgentChannelService::OnLockChanged, weak_factory_.GetWeakPtr()));
  core_subscription_ = maho::AddCoreReadyCallback(base::BindRepeating(
      &MahoAgentChannelService::Rehydrate, weak_factory_.GetWeakPtr()));
  const auto path = profile->GetPath().AppendASCII("MahoChannels.json");
  gateway_.SetPersistencePath(path);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()}, base::BindOnce([](base::FilePath path) {
        std::string json;
        base::ReadFileToString(path, &json);
        return json;
      }, path), base::BindOnce(&MahoAgentChannelService::OnLoaded,
                              weak_factory_.GetWeakPtr()));
}
MahoAgentChannelService::~MahoAgentChannelService() = default;
void MahoAgentChannelService::Shutdown() {
  shutdown_ = true;
  ++generation_;
  weak_factory_.InvalidateWeakPtrs();
  lock_subscription_ = {};
  core_subscription_ = {};
  gateway_.ClearCredentials();
  sessions_.clear();
  results_.clear();
  replies_.clear();
  reply_ids_.clear();
  for (const auto& config : gateway_.GetChannels()) {
    if (config.opaque_auth_handle)
      broker_.vault()->StoreCredential(*config.opaque_auth_handle, VaultCredential());
  }
}
void MahoAgentChannelService::OnLoaded(std::string json) {
  if (!json.empty()) gateway_.ImportStateJson(json);
  // Persistence never authorizes a credential, even if a file was edited.
  gateway_.ClearCredentials();
  loaded_ = true;
  Rehydrate();
}
void MahoAgentChannelService::OnLockChanged(bool locked) {
  ++generation_;
  gateway_.ClearCredentials();
  sessions_.clear();
  results_.clear();
  replies_.clear();
  reply_ids_.clear();
  for (const auto& config : gateway_.GetChannels()) {
    if (config.opaque_auth_handle)
      broker_.vault()->StoreCredential(*config.opaque_auth_handle, VaultCredential());
  }
  if (!locked) Rehydrate();
}
void MahoAgentChannelService::Rehydrate() {
  if (!loaded_ || shutdown_ || maho::IsVaultLockedForUi() ||
      !maho::IsPasswordManagerAllowedForProfile(profile_)) return;
  for (auto config : gateway_.GetChannels()) {
    if (!config.enabled || !config.opaque_auth_handle) continue;
    const auto id = *config.opaque_auth_handle;
    broker_.vault()->StoreCredential(id, VaultCredential());
    maho::PostCoreTaskAndReplyWithResult(FROM_HERE,
        base::BindOnce(&ReadCredential, id),
        base::BindOnce(&MahoAgentChannelService::OnCredential,
            weak_factory_.GetWeakPtr(), generation_, std::move(config),
            base::BindOnce([](const std::string&) {})));
  }
}
void MahoAgentChannelService::OnCredential(
    uint64_t generation, ChannelConfig config,
    base::OnceCallback<void(const std::string&)> callback,
    std::optional<ChannelToken> token) {
  if (generation != generation_ || shutdown_ || maho::IsVaultLockedForUi() ||
      !maho::IsPasswordManagerAllowedForProfile(profile_)) {
    token.reset();
    std::move(callback).Run(Result(false, "Vault state changed; try again"));
    return;
  }
  if (!token) {
    std::move(callback).Run(Result(false, "Vault note unavailable or empty"));
    return;
  }
  const bool should_start = config.enabled;
  const auto id = config.id;
  const auto* previous = gateway_.GetChannel(id);
  const std::optional<std::string> previous_handle = previous
      ? previous->opaque_auth_handle : std::nullopt;
  gateway_.StopChannel(id);
  if (config.provider == ChannelProvider::kSlack) {
    auto secrets = base::JSONReader::ReadDict(token->expose_secret(), base::JSON_PARSE_RFC);
    auto* app = secrets ? secrets->FindString("app_token") : nullptr;
    auto* bot = secrets ? secrets->FindString("bot_token") : nullptr;
    if (!app || app->empty() || !bot || bot->empty()) {
      std::move(callback).Run(Result(false, "Slack note requires app_token and bot_token"));
      return;
    }
    config.outbound_token = ChannelToken(*bot);
    token = ChannelToken(*app);
    maho::core::SecureClearString(app);
    maho::core::SecureClearString(bot);
  }
  broker_.vault()->StoreCredential(*config.opaque_auth_handle,
      VaultCredential::Bearer(config.outbound_token
          ? config.outbound_token->expose_secret() : token->expose_secret()));
  config.token = std::move(token);
  gateway_.RegisterChannel(std::move(config));
  const auto* registered = gateway_.GetChannel(id);
  if (previous_handle && registered &&
      previous_handle != registered->opaque_auth_handle)
    broker_.vault()->StoreCredential(*previous_handle, VaultCredential());
  if (should_start) gateway_.StartChannel(id);
  std::move(callback).Run(Result(true));
}
void MahoAgentChannelService::ConfigureChannel(
    std::string json, base::OnceCallback<void(const std::string&)> callback) {
  auto dict = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  auto config = ChannelConfig::FromJson(json);
  if (!loaded_ || shutdown_ || maho::IsVaultLockedForUi() ||
      !maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(Result(false, "Unlock this profile's Vault first"));
    return;
  }
  if (!dict || dict->contains("token") || !config.has_value() ||
      config->provider == ChannelProvider::kCustom || config->id.size() > 128 ||
      config->id.find(':') != std::string::npos ||
      !config->opaque_auth_handle || config->opaque_auth_handle->empty() ||
      config->opaque_auth_handle->size() > 256 || config->allowed_sender_ids.empty() ||
      std::any_of(config->allowed_sender_ids.begin(), config->allowed_sender_ids.end(),
          [](const std::string& value) {
            return value.empty() || value.size() > 128 ||
                value.find_first_not_of("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_.@-:") != std::string::npos;
          })) {
    std::move(callback).Run(Result(false, "Provide a channel ID, provider, Vault note ID and sender allowlist"));
    return;
  }
  ++generation_;
  sessions_.erase(config->id);
  results_.erase(config->id);
  replies_.erase(config->id);
  reply_ids_.erase(config->id);
  const auto id = *config->opaque_auth_handle;
  maho::PostCoreTaskAndReplyWithResult(FROM_HERE,
      base::BindOnce(&ReadCredential, id),
      base::BindOnce(&MahoAgentChannelService::OnCredential,
          weak_factory_.GetWeakPtr(), generation_, std::move(*config),
          std::move(callback)));
}
std::string MahoAgentChannelService::ListChannels() const {
  base::ListValue channels;
  for (const auto& config : gateway_.GetChannels()) {
    auto dict = base::JSONReader::ReadDict(config.ToJson(), base::JSON_PARSE_RFC).value();
    dict.Remove("token");
    dict.Set("running", gateway_.IsChannelRunning(config.id));
    dict.Set("health", ChannelHealthStatusToString(gateway_.GetHealth(config.id).status));
    base::ListValue paired;
    for (const auto& sender : config.allowed_sender_ids) {
      if (gateway_.IsSenderPaired(config.id, sender)) paired.Append(sender);
    }
    dict.Set("paired_sender_ids", std::move(paired));
    auto result = results_.find(config.id);
    if (result != results_.end()) dict.Set("result", result->second);
    auto reply = reply_ids_.find(config.id);
    if (reply != reply_ids_.end()) dict.Set("reply_id", reply->second);
    channels.Append(std::move(dict));
  }
  return base::WriteJson(base::DictValue().Set("channels", std::move(channels))
      .Set("locked", maho::IsVaultLockedForUi())).value();
}
std::string MahoAgentChannelService::RemoveChannel(const std::string& id) {
  if (!loaded_) return Result(false, "Channels are loading");
  ++generation_;
  sessions_.erase(id);
  results_.erase(id);
  replies_.erase(id);
  reply_ids_.erase(id);
  if (const auto* config = gateway_.GetChannel(id); config && config->opaque_auth_handle)
    broker_.vault()->StoreCredential(*config->opaque_auth_handle, VaultCredential());
  return Result(gateway_.UnregisterChannel(id).has_value());
}
std::string MahoAgentChannelService::ApprovePairing(
    const std::string& id, const std::string& sender, const std::string& code) {
  const auto* config = gateway_.GetChannel(id);
  if (maho::IsVaultLockedForUi() || !config || !config->IsSenderAllowed(sender))
    return Result(false, "Unlock Vault and allowlist this sender first");
  const bool approved = gateway_.ApprovePairing(id, sender, code) ==
      MahoAgentChannelPairing::ConfirmResult::kApproved;
  return Result(approved, approved ? "" : "Pairing code invalid or expired");
}
std::string MahoAgentChannelService::RevokePairing(
    const std::string& id, const std::string& sender) {
  gateway_.RevokePairing(id, sender);
  sessions_.erase(id);
  replies_.erase(id);
  reply_ids_.erase(id);
  return Result(true);
}
void MahoAgentChannelService::SendReply(
    const std::string& id, const std::string& reply_id,
    base::OnceCallback<void(const std::string&)> callback) {
  auto reply = replies_.find(id);
  auto revision = reply_ids_.find(id);
  if (maho::IsVaultLockedForUi() || reply == replies_.end() ||
      revision == reply_ids_.end() || revision->second != reply_id ||
      !maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(Result(false, "Reply expired; refresh channels"));
    return;
  }
  const auto* config = gateway_.GetChannel(id);
  if (!config) {
    std::move(callback).Run(Result(false, "Channel unavailable"));
    return;
  }
  auto operation = SendViaDirectApi(*config, reply->second);
  if (!operation.has_value()) {
    std::move(callback).Run(Result(false, "Channel unavailable"));
    return;
  }
  const auto action = std::string(ServiceKindToString(operation->operation.service)) +
      ":" + operation->operation.operation_name;
  const auto token = broker_.approval_store()->IssueToken(action);
  auto message = std::move(reply->second);
  replies_.erase(reply);
  reply_ids_.erase(revision);
  gateway_.SendMessage(message, token, base::BindOnce(
      [](base::OnceCallback<void(const std::string&)> callback,
         base::expected<OutboundSendReceipt, ChannelError> result) {
        std::move(callback).Run(Result(result.has_value(),
            result.has_value() ? "" : "Provider rejected the reply"));
      }, std::move(callback)));
}
bool MahoAgentChannelService::Dispatch(const AgentSessionTrigger& trigger) {
  if (shutdown_ || maho::IsVaultLockedForUi() ||
      !maho::IsPasswordManagerAllowedForProfile(profile_)) return false;
  auto& adapter = sessions_[trigger.channel_id];
  if (!adapter) {
    adapter = std::make_unique<MahoUnifiedAgentAdapter>(profile_->GetPrefs(),
        profile_->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess(),
        base::BindRepeating([](Profile* profile) {
          return !maho::IsVaultLockedForUi() &&
                 maho::IsPasswordManagerAllowedForProfile(profile);
        }, profile_.get()));
  }
  if (adapter->IsTurnActive()) return false;
  OutboundChannelMessage reply;
  reply.channel_id = trigger.channel_id;
  reply.provider = trigger.provider;
  reply.destination_id = trigger.reply_channel_id.empty()
      ? trigger.sender_id : trigger.reply_channel_id;
  replies_[trigger.channel_id] = std::move(reply);
  reply_ids_.erase(trigger.channel_id);
  // New identity for every external turn: never append to another sender or
  // the personal panel's active conversation.
  adapter->StartSession("channel-" + base::Uuid::GenerateRandomV4().AsLowercaseString());
  adapter->SubmitMessage(trigger.ToFollowUpPrompt(), maho_ai::mojom::ChatIntent::kFreeform,
      false, maho_ai::mojom::InteractionMode::kAssistant,
      base::BindRepeating(&MahoAgentChannelService::OnRuntimeEvent,
          weak_factory_.GetWeakPtr(), trigger.channel_id));
  return true;
}
void MahoAgentChannelService::OnRuntimeEvent(
    const std::string& id, MahoAiRuntimeEvent event) {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return;
  if (event.type == MahoAiRuntimeEventType::kApprovalRequest) {
    if (const auto* approval = event.payload.FindString("approval_id"))
      it->second->RespondToApproval(*approval, false);
  } else if (event.type == MahoAiRuntimeEventType::kInteractionRequest) {
    if (const auto* interaction = event.payload.FindString("interaction_id"))
      it->second->RespondToInteraction(*interaction, "{\"approved\":false}");
  } else if (event.type == MahoAiRuntimeEventType::kTurnComplete ||
             event.type == MahoAiRuntimeEventType::kError) {
    results_[id] = event.text;
    if (event.type == MahoAiRuntimeEventType::kTurnComplete && !event.text.empty()) {
      replies_[id].text = event.text;
      reply_ids_[id] = base::Uuid::GenerateRandomV4().AsLowercaseString();
    } else {
      replies_.erase(id);
      reply_ids_.erase(id);
    }
  }
}
}  // namespace maho::ai
