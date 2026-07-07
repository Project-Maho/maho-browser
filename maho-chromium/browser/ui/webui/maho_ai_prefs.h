// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_PREFS_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_PREFS_H_

namespace maho::ai_prefs {

inline constexpr char kProvider[] = "maho.ai.provider";
inline constexpr char kBaseUrl[] = "maho.ai.base_url";
inline constexpr char kApiKey[] = "maho.ai.api_key";
inline constexpr char kModel[] = "maho.ai.model";
inline constexpr char kStructuredContextV2Enabled[] =
    "maho.ai.structured_context_v2_enabled";
inline constexpr char kBrowserToolsV1Enabled[] = "maho.ai.browser_tools_v1_enabled";
inline constexpr char kChatOrchestratorV2Enabled[] =
    "maho.ai.chat_orchestrator_v2_enabled";
inline constexpr char kProviderAdapterV2Enabled[] =
    "maho.ai.provider_adapter_v2_enabled";
inline constexpr char kUiThinModeEnabled[] = "maho.ai.ui_thin_mode_enabled";
inline constexpr char kPendingInitialQuery[] = "maho.ai.pending_initial_query";
inline constexpr char kSessionListJson[] = "maho.ai.session_list_json";
inline constexpr char kApprovalPolicy[] = "maho.ai.approval_policy";
inline constexpr char kSessionPersistenceEnabled[] =
    "maho.ai.session_persistence_enabled";
inline constexpr char kSessionEventLogs[] =
    "maho.ai.session_event_logs";
inline constexpr char kAiViewMode[] = "maho.ai.view_mode";

// BYOK API keys encrypted at rest via Chromium OSCrypt + base64-encoded for
// safe JSON storage. Populated by the desktop settings UI (write side TBD)
// and read by MahoUnifiedAgentAdapter::OnAgentSecureStorage at session
// creation. Empty string means "no key configured" → agent fails closed.
inline constexpr char kByokOpenAIEncryptedB64[] = "maho.ai.byok.openai.encrypted_b64";
inline constexpr char kByokAnthropicEncryptedB64[] = "maho.ai.byok.anthropic.encrypted_b64";
inline constexpr char kAgentToolGrants[] = "maho.ai.extensions.agent_tool_grants";

// One-time migration flag: true once macOS defaults have been copied into
// Chromium prefs. Prevents re-migration on subsequent launches.
inline constexpr char kSettingsMigratedFromMacOSDefaults[] =
    "maho.ai.settings_migrated_from_macos_defaults";

}  // namespace maho::ai_prefs

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_PREFS_H_
