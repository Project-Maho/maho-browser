# PROJECT KNOWLEDGE BASE — browser/ai

**Generated:** 2026-07-14

## OVERVIEW
AI subsystem: LLM client, runtime routing (BYOK/OpenRouter), chat orchestration, tool registry, and page-context extraction. ~40 files. All AI helper logic lives here; heavy WebUI work lives in `browser/ui/webui/maho_ai/`.

## STRUCTURE
```
browser/ai/
├── maho_ai_llm_client.cc/h          # HTTP LLM client (streaming SSE)
├── maho_ai_runtime_router.cc/h      # Routes requests → provider (BYOK vs OpenRouter)
├── maho_ai_runtime_adapter.cc/h     # Provider-agnostic adapter interface
├── maho_ai_page_context_extractor.cc/h  # Extracts page text/title/url for AI context
├── maho_chat_orchestration_service.cc/h # Chat session lifecycle
├── maho_conversation_task.cc/h      # Single-turn conversation unit
├── maho_unified_agent_adapter.cc/h  # Agent mode (multi-step tool use)
├── maho_browser_tool_registry.cc/h  # Tool registration (browser-side tools)
├── maho_browser_tool_executor.cc/h  # Tool execution dispatch
├── maho_tool_execution_graph.cc/h   # Tool dependency graph execution
├── maho_ffi_callback_handle.cc/h    # Async callback handle via FFI → maho-core
├── maho_model_list_fetcher.cc/h     # Fetches available model list from OpenRouter
├── maho_tab_tidy_orchestrator.cc/h  # Tab-Tidy AI feature (close/group suggestions)
├── maho_extract_prompt_messages.cc/h # System/user prompt assembly
├── maho_ai_settings_migration.cc/h  # Migrate legacy AI settings
├── maho_ai_security_utils.h         # API key sanitization helpers
├── maho_managed_connection_test.*   # Test helpers for managed connection
└── *_unittest.cc                    # Per-file unit tests
```

## WHERE TO LOOK
| Task | Location |
|------|----------|
| LLM HTTP calls (streaming) | `maho_ai_llm_client.cc` |
| Provider routing (BYOK vs managed) | `maho_ai_runtime_router.cc` |
| Page context for AI | `maho_ai_page_context_extractor.cc` |
| Chat session management | `maho_chat_orchestration_service.cc` |
| Agent mode / tool use | `maho_unified_agent_adapter.cc` + `maho_tool_execution_graph.cc` |
| Tool registration | `maho_browser_tool_registry.cc` |
| FFI async callbacks | `maho_ffi_callback_handle.cc` |
| Available models list | `maho_model_list_fetcher.cc` |
| Tab-Tidy feature | `maho_tab_tidy_orchestrator.cc` |

## CONVENTIONS
- Page-context results: structured objects with `status`/`title`/`url`/`text`/`warnings` — not raw text blobs
- Tool results: typed response objects, not free-form JSON strings
- Async FFI callbacks: use `MahoFfiCallbackHandle` — do not retain raw FFI handles
- All `DCHECK_CALLED_ON_VALID_SEQUENCE` on UI thread methods
- Prefer helpers here over embedding AI logic in Mojo page handlers

## ANTI-PATTERNS
- No business logic in WebUI Mojo handlers — delegate to services here
- No direct HTTP calls outside `maho_ai_llm_client.cc`
- No raw model name strings — use the model list from `maho_model_list_fetcher`
- No `unwrap()`-style `.GetOrDie()` on FFI callbacks across threads

## NOTES
- Unit tests are per-file (`*_unittest.cc`), not in a separate test directory
- `maho_ai_security_utils.h` is header-only — API key redaction for logs
- Tab-Tidy orchestrator is independent of chat; shares runtime router only
