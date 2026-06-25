# Maho Chat Reference-Driven Parallel Implementation Plan

## Goal

Rebuild Maho chat so it behaves like a browser-native AI system grounded in the Nook browser reference and the local macOS shell Swift reference.

This plan intentionally prioritizes:

- thin chat UI
- shell or browser-owned orchestration
- structured page context
- explicit browser tools
- active-window or active-tab scoped execution
- parallel implementation with minimal cross-track blocking

## Core Design Decision

### Ownership model

#### Browser or shell side owns

- chat orchestration loop
- current page extraction
- browser tool execution
- provider streaming lifecycle
- panel session-local presentation state

#### Rust owns

- typed contracts
- shared state and persistence primitives
- reusable prompt and state helpers where appropriate
- cross-platform business rules that do not require live browser control

### Why

This is closer to the actual reference implementations:

- Nook keeps orchestration in `AIService.swift`
- Nook keeps browser actions in `BrowserToolExecutor.swift`
- local macOS shell keeps orchestration in `AIChatHandler.swift`
- local macOS shell keeps extraction in `PageContextExtractor.swift`

This plan explicitly rejects making Rust the single live orchestration owner.

## Target Architecture

### Layer A - Chat UI

**Reference:** Nook `SidebarAIChat.swift`

**Responsibilities**

- render transcript
- render loading, tool, and citation state
- render source and page chips
- collect input
- expose model, web-search, and tool toggles if supported
- avoid semantic routing logic

**Maho target**

- `maho-chromium/browser/resources/maho_ai/app.ts`

**Rule**

`app.ts` becomes a thin view-controller, not the semantic brain.

### Layer B - Browser Chat Service

**Reference:** Nook `AIService.swift`, local `AIChatHandler.swift`

**Responsibilities**

- receive user message
- decide request mode
- request structured page context
- assemble prompt and tool request
- run tool-calling loop
- invoke provider transport
- stream partial tokens to UI
- finalize assistant message
- attach provenance and citations

**Maho target**

Introduce a browser-side service near the existing AI layer rather than stuffing more logic into `MahoAIPageHandler`.

**Rule**

`MahoAIPageHandler` becomes transport and UI glue, not the main orchestrator.

### Layer C - Browser Context and Tool Executor

**Reference:** Nook `BrowserToolExecutor.swift`, local `PageContextExtractor.swift`, local `MCPToolRegistry.swift`

**Responsibilities**

- resolve active tab and active page from browser state
- extract rich page context
- expose explicit tools:
  - read current page
  - get selected text
  - get active tab metadata
  - search in page
  - extract structured page context

**Rule**

Chat asks for browser capabilities through explicit interfaces. It should not reach into random browser state ad hoc.

### Layer D - Rust Contracts and Shared State

**Responsibilities**

- define typed events and contracts
- store durable conversation state if needed
- define reusable schemas
- support persistence and indexing later
- optionally provide prompt helpers

**Rule**

Rust supports the shell. It does not replace shell orchestration.

## Replace the Current Semantic Model

### Current model

- `freeform`
- `analyze_current_page`
- `explain_current_page`
- `summarize_current_page`

### New request modes

- `general_chat`
- `page_question`
- `page_transformation`
- `browser_action`
- `tool_assisted_task`

### Why

This fixes the current blind spot where page-dependent freeform queries are treated as generic chat.

Examples:

- `현재 페이지 제목만 말해줘`
- `이 페이지 heading만 보여줘`
- `선택한 텍스트 설명해줘`

These should all become page-aware requests even when they are not analyze, explain, or summarize.

## Replace Flattened Page Context with Structured Page Context

### Reference basis

- local `PageContextExtractor.swift`
- Nook page extraction and page-reading tool paths

### Required page context fields

- `title`
- `url`
- `selected_text`
- `main_text`
- `headings[]`
- `meta_description`
- `links[]`
- `extraction_status`
- `extraction_warnings[]`

### Critical rule

Do not flatten into one string at extraction time. Flattening should happen only at the last prompt-assembly step if still needed.

## Change Success and Failure Semantics

### Current bad behavior

Body text failure effectively destroys otherwise useful title and URL context.

### New statuses

- `full`
- `partial_title_url_only`
- `partial_selection_only`
- `partial_text_only`
- `unavailable`

### Behavior

- title questions can succeed with `title + url`
- selection questions can succeed with `selected_text`
- heading questions can succeed with `headings`
- only fully missing context should produce deterministic failure

## Add Explicit Browser Tools

### Reference basis

- Nook `BrowserTools.swift`
- Nook `BrowserToolExecutor.swift`
- local `MCPToolRegistry.swift`

### Phase 1 tool set

- `read_current_page`
- `get_selected_text`
- `get_active_tab`
- `search_in_page`
- `extract_structured_page_context`

### Phase 2 tool set

- `open_tab`
- `switch_tab`
- `navigate_to`
- `click_element`
- `get_interactive_elements`

## Provider Abstraction

### Reference basis

- Nook provider adapters
- local Swift shell `LLMService` style

### Plan

Keep the current browser-side LLM transport temporarily, but refactor it into:

- provider config resolver
- provider adapter
- streaming parser
- response normalizer

## Conversation Persistence

### Short term

- session-local in browser service

### Medium term

- persist through Rust or shared storage layer if product wants history

### Rule

Do not leave chat history split ambiguously between browser local state and Rust local state. There must be one canonical owner.

### Recommendation

Canonical owner should be the browser chat service for live runtime, with optional Rust-backed persistence later.

## Phase 0 - Contract Freeze

**Duration:** 0.5 to 1 day

This is the only globally blocking stage.

### Deliverables

#### 1. New request model

- `general_chat`
- `page_question`
- `page_transformation`
- `browser_action`
- `tool_assisted_task`

#### 2. Structured page context contract

- `title`
- `url`
- `selected_text`
- `main_text`
- `headings[]`
- `meta_description`
- `links[]`
- `extraction_status`
- `extraction_warnings[]`

#### 3. Tool schema contract

- `read_current_page`
- `get_selected_text`
- `get_active_tab`
- `search_in_page`
- `extract_structured_page_context`

#### 4. Ownership declaration

- browser or shell owns orchestration
- Rust is typed support and persistence layer
- TS is thin UI

### Files to touch

- `maho/crates/maho-types/src/events/shell_event.rs`
- `maho/crates/maho-types/src/events/core_update.rs`
- optional new shared types file for structured page context

## Parallel Workstreams

### Track A - Structured Page Context Extractor

**Owner:** Browser/C++

**Reference:** local `PageContextExtractor.swift`, Nook page extraction

#### Goal

Replace the current minimal extraction with a structured extraction service.

#### New files

- `maho-chromium/browser/ai/maho_page_context_extractor.h`
- `maho-chromium/browser/ai/maho_page_context_extractor.cc`

#### Existing files likely touched

- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.h`

#### Scope

- active tab resolve
- title and URL extraction
- selected text extraction
- headings extraction
- meta description extraction
- visible text extraction
- partial success semantics

#### Done when

- title-only partial success works
- heading and selection fields exist
- extraction stays structured until prompt assembly

#### Dependency

Only Phase 0 contract freeze.

### Track B - Browser Tool Registry and Executor

**Owner:** Browser/C++

**Reference:** Nook `BrowserTools.swift`, `BrowserToolExecutor.swift`, local `MCPToolRegistry.swift`

#### Goal

Expose browser capabilities as explicit tools callable by the orchestrator.

#### New files

- `maho-chromium/browser/ai/maho_browser_tool_registry.h`
- `maho-chromium/browser/ai/maho_browser_tool_registry.cc`
- `maho-chromium/browser/ai/maho_browser_tool_executor.h`
- `maho-chromium/browser/ai/maho_browser_tool_executor.cc`

#### Scope

- tool schema definition
- active browser, window, and tab binding
- normalized tool result objects
- initial Phase 1 tool set only

#### Done when

- at least five tools are callable
- active window and tab scope is guaranteed
- orchestrator depends only on the registry and executor interface

#### Dependency

Only Phase 0 contract freeze.

### Track C - Browser-side Chat Orchestrator Service

**Owner:** Browser/C++

**Reference:** Nook `AIService.swift`, local `AIChatHandler.swift`

#### Goal

Move the actual chat brain out of `MahoAIPageHandler`.

#### New files

- `maho-chromium/browser/ai/maho_ai_chat_service.h`
- `maho-chromium/browser/ai/maho_ai_chat_service.cc`

#### Existing files likely touched

- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.h`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc`

#### Scope

- request mode classification
- structured page context inclusion policy
- tool-calling loop
- provider invocation orchestration
- streaming lifecycle
- final response assembly
- provenance and citation attachment

#### Done when

- `MahoAIPageHandler` is a thin delegate
- page-aware freeform queries route correctly
- orchestration logic is centralized in one browser-side service

#### Dependency

- Phase 0 required
- can start with stubs while Tracks A, B, and F are in progress

### Track D - Rust Contracts, Persistence, and Shared State Cleanup

**Owner:** Rust

#### Goal

Make Rust a typed support layer rather than the live orchestrator.

#### Files likely touched

- `maho/crates/maho-types/src/events/shell_event.rs`
- `maho/crates/maho-types/src/events/core_update.rs`
- `maho/crates/maho-core/src/maho_core.rs`
- `maho/crates/maho-core/src/llm_manager.rs`
- optional storage or conversation-related files

#### Scope

- remove flat `page_context: String` assumptions
- adopt structured payloads
- define persistence boundaries
- remove duplicated chat-state ownership assumptions

#### Done when

- Rust cleanly accepts typed event and state payloads
- browser-owned orchestration no longer conflicts with Rust state

#### Dependency

Only Phase 0 contract freeze.

### Track E - WebUI Thin-UI Refactor

**Owner:** TS/WebUI

**Reference:** Nook `SidebarAIChat.swift`

#### Goal

Turn `app.ts` into a thin UI layer.

#### Files likely touched

- `maho-chromium/browser/resources/maho_ai/app.ts`
- related WebUI resource files if needed

#### Scope

- input and rendering only
- tool state, source badge, and page chip display
- remove routing and semantic logic from UI
- support structured response rendering

#### Done when

- semantic decisioning is gone from the UI layer
- provenance and tool state can be rendered cleanly

#### Dependency

- Phase 0 required
- can begin early with mock payloads

### Track F - Provider Adapter Refactor

**Owner:** Browser/C++

**Reference:** Nook provider adapter pattern, local `LLMService` style

#### Goal

Split current `maho_ai_llm_client` responsibilities into cleaner interfaces.

#### Files likely touched

- `maho-chromium/browser/ai/maho_ai_llm_client.h`
- `maho-chromium/browser/ai/maho_ai_llm_client.cc`

#### New files recommended

- `maho-chromium/browser/ai/maho_ai_provider.h`
- `maho-chromium/browser/ai/maho_openai_compatible_provider.cc`
- future provider adapter files

#### Scope

- separate config resolution
- add provider adapter abstraction
- separate streaming parser
- normalize responses into browser chat service contract

#### Done when

- chat orchestrator depends only on provider interfaces
- current OpenAI-compatible path still works

#### Dependency

Only Phase 0 contract freeze.

## Parallelization Graph

### Wave 1 - can start immediately after Phase 0

- Track A
- Track B
- Track D
- Track E
- Track F

### Wave 2 - integrate while others finish

- Track C

Track C can start with stubs but reaches full integration after outputs from A, B, and F settle.

## Recommended Merge Order

### PR 1

Phase 0 contract freeze and shared type definitions.

### PR 2 to PR 6 in parallel

- Track A extractor
- Track B tools
- Track D Rust cleanup
- Track E thin UI refactor
- Track F provider refactor

### PR 7

Track C browser orchestrator service plus `MahoAIPageHandler` thin-delegate conversion.

### PR 8

Integration pass, feature-flag rollout, and end-to-end verification.

## Feature Flags

Recommended flags for safe parallel merge:

- `maho.ai.structuredContextV2`
- `maho.ai.browserToolsV1`
- `maho.ai.chatOrchestratorV2`
- `maho.ai.providerAdapterV2`
- `maho.ai.uiThinMode`

## Independent Verification Per Track

### Track A verification

- title-only partial context returns correctly
- headings, selection, and meta fields populate
- partial success semantics behave correctly

### Track B verification

- tools operate only on active tab or active window
- `read_current_page`, `get_selected_text`, and `search_in_page` work

### Track C verification

- `현재 페이지 제목만 말해줘` is classified as `page_question`
- tool loop, streaming, and finalization are consistent
- deterministic failure path is preserved for unavailable context

### Track D verification

- flat string context assumptions are removed
- duplicated history and state boundaries are clarified

### Track E verification

- `app.ts` performs no semantic routing
- tool, source, and page chips render correctly

### Track F verification

- current local-server and OpenAI-compatible requests still function
- chat orchestrator uses provider interface only

## Final Integration Success Criteria

1. `현재 페이지 제목만 말해줘`
   - is treated as page-aware
   - succeeds using title-only partial context when necessary

2. `이 페이지 heading만 보여줘`
   - uses structured heading data

3. `선택한 텍스트 설명해줘`
   - uses explicit selection data

4. `현재 페이지 요약해줘`
   - continues to work through structured context

5. body text failure does not break title or URL questions

6. browser or shell side is the orchestration owner

7. UI remains thin and contains no semantic hacks

## Recommended Team Split

### Five engineers or agents

- Engineer 1: Track A
- Engineer 2: Track B
- Engineer 3: Track C
- Engineer 4: Track D
- Engineer 5: Track E then Track F, or vice versa

### Three engineers or agents

- Person 1: Track A + Track B
- Person 2: Track C + Track F
- Person 3: Track D + Track E

## Summary

The core of parallel implementation is:

1. freeze the contracts first
2. split extractor, tools, orchestrator, Rust support, UI, and provider work into separate tracks
3. integrate orchestrator last against stabilized interfaces

This plan is intentionally reference-faithful:

- Nook and local Swift shell patterns drive orchestration ownership
- structured page context is first-class
- browser tools are explicit
- UI is thin
- Rust is support infrastructure, not the live chat brain
