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
inline constexpr char kOpenCodeRuntimeEnabled[] =
    "maho.ai.opencode_runtime_enabled";
inline constexpr char kSessionListJson[] = "maho.ai.session_list_json";
inline constexpr char kApprovalPolicy[] = "maho.ai.approval_policy";
inline constexpr char kSessionPersistenceEnabled[] =
    "maho.ai.session_persistence_enabled";
inline constexpr char kSessionEventLogs[] =
    "maho.ai.session_event_logs";
inline constexpr char kRuntimeCommand[] = "maho.ai.runtime_command";
inline constexpr char kAiViewMode[] = "maho.ai.view_mode";

}  // namespace maho::ai_prefs

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_PREFS_H_
