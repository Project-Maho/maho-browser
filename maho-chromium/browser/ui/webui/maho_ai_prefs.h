// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_PREFS_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_PREFS_H_

namespace maho::ai_prefs {

inline constexpr char kProvider[] = "maho.ai.provider";
inline constexpr char kBaseUrl[] = "maho.ai.base_url";
inline constexpr char kApiKey[] = "maho.ai.api_key";
inline constexpr char kModel[] = "maho.ai.model";
inline constexpr char kReasoningEffort[] = "maho.ai.reasoning_effort";
inline constexpr char kStructuredContextV2Enabled[] =
    "maho.ai.structured_context_v2_enabled";
inline constexpr char kBrowserToolsV1Enabled[] = "maho.ai.browser_tools_v1_enabled";
inline constexpr char kChatOrchestratorV2Enabled[] =
    "maho.ai.chat_orchestrator_v2_enabled";
inline constexpr char kProviderAdapterV2Enabled[] =
    "maho.ai.provider_adapter_v2_enabled";
inline constexpr char kUiThinModeEnabled[] = "maho.ai.ui_thin_mode_enabled";
inline constexpr char kSessionListJson[] = "maho.ai.session_list_json";
inline constexpr char kApprovalPolicy[] = "maho.ai.approval_policy";
inline constexpr char kPermissionTier[] = "maho.ai.permission_tier";
inline constexpr char kFinalConfirm[] = "maho.ai.final_confirm";
inline constexpr char kProactiveMode[] = "maho.ai.proactive_mode";
inline constexpr char kSessionPersistenceEnabled[] =
    "maho.ai.session_persistence_enabled";
inline constexpr char kSessionEventLogs[] =
    "maho.ai.session_event_logs";
inline constexpr char kAiViewMode[] = "maho.ai.view_mode";
inline constexpr char kPendingSurface[] = "maho.ai.pending_surface";
inline constexpr char kPendingSurfaceGeneration[] =
    "maho.ai.pending_surface_generation";
inline constexpr char kAttachBrowserContext[] = "maho.ai.attach_browser_context";
inline constexpr char kMailReadAllowed[] = "maho.ai.mail_read_allowed";

// Translation onboarding choice. Cloud-backed (BYOK) — NO local model.
// Values: "byok" | "skip". Empty = not chosen yet.
inline constexpr char kTranslationProvider[] = "maho.translation.provider";

// BYOK API keys encrypted at rest via Chromium OSCrypt + base64-encoded for
// safe JSON storage. Populated by the desktop settings UI (write side TBD)
// and read by MahoUnifiedAgentAdapter::OnAgentSecureStorage at session
// creation. Empty string means "no key configured" → agent fails closed.
inline constexpr char kByokOpenAIEncryptedB64[] = "maho.ai.byok.openai.encrypted_b64";
inline constexpr char kByokAnthropicEncryptedB64[] = "maho.ai.byok.anthropic.encrypted_b64";
// Provider OAuth sign-in state. The access token itself is stored in the
// matching kByok*EncryptedB64 slot so the agent credential path is unchanged;
// only the renewal material lives here.
inline constexpr char kOAuthOpenAIClientId[] = "maho.ai.oauth.openai.client_id";
inline constexpr char kOAuthOpenAIRefreshEncryptedB64[] =
    "maho.ai.oauth.openai.refresh_encrypted_b64";
inline constexpr char kOAuthOpenAIExpiresAt[] =
    "maho.ai.oauth.openai.expires_at";
inline constexpr char kOAuthAnthropicClientId[] =
    "maho.ai.oauth.anthropic.client_id";
inline constexpr char kOAuthAnthropicRefreshEncryptedB64[] =
    "maho.ai.oauth.anthropic.refresh_encrypted_b64";
inline constexpr char kOAuthAnthropicExpiresAt[] =
    "maho.ai.oauth.anthropic.expires_at";

inline constexpr char kAgentToolGrants[] = "maho.ai.extensions.agent_tool_grants";
inline constexpr char kMcpCredentials[] = "maho.ai.byok.mcp_credentials";

// Models settings v1 prefs.
inline constexpr char kProviderConfigs[] = "maho.ai.provider_configs";
inline constexpr char kTaskModels[] = "maho.ai.task_models";
inline constexpr char kByokOpenAICompatibleEncryptedB64[] =
    "maho.ai.byok.openai_compatible.encrypted_b64";
inline constexpr char kModelsSettingsMigratedV1[] =
    "maho.ai.models_settings_migrated_v1";

// One-time migration flag: true once macOS defaults have been copied into
// Chromium prefs. Prevents re-migration on subsequent launches.
inline constexpr char kSettingsMigratedFromMacOSDefaults[] =
    "maho.ai.settings_migrated_from_macos_defaults";

// One-time migration flag: true once legacy Google provider settings
// have been cleared/migrated to blank.
inline constexpr char kSettingsMigratedFromGoogleProvider[] =
    "maho.ai.settings_migrated_from_google_provider";

inline constexpr char kModelCache[] = "maho.ai.model_cache";

}  // namespace maho::ai_prefs

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_PREFS_H_
