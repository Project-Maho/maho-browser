export const COMMAND_AI_STATES = Object.freeze(["closed", "palette", "ai"]);

export const COMMAND_MODES = Object.freeze([
  Object.freeze({ id: "search", label: "Search", active: true }),
  Object.freeze({ id: "ask-maho", label: "Ask Maho", active: false }),
]);

export const COMMAND_QUERY = Object.freeze({
  value: "changing light",
  context: "Search across the two local documents",
});

export const COMMAND_RESULTS = Object.freeze([
  Object.freeze({
    id: "open-field-notes",
    kind: "page",
    mark: "FN",
    title: "Listening for weather beneath the city",
    detail: "Field Notes / local document",
    selected: false,
  }),
  Object.freeze({
    id: "open-meridian-review",
    kind: "page",
    mark: "MR",
    title: "A room designed for changing light",
    detail: "Meridian Review / local article",
    selected: true,
  }),
  Object.freeze({
    id: "search-both-documents",
    kind: "action",
    mark: "S",
    title: "Search both documents for changing light",
    detail: "Local Split View context",
    selected: false,
  }),
  Object.freeze({
    id: "ask-about-connection",
    kind: "ai",
    mark: "M",
    title: "Ask Maho how weather and light shape a room",
    detail: "Uses the visible page context",
    selected: false,
  }),
]);

export const AI_PANEL = Object.freeze({
  title: "Page context",
  contextLabel: "Two local documents",
  status: "Ready",
  connection: Object.freeze({
    eyebrow: "MAHO CONTEXT",
    title: "Use the visible Split View as context",
    action: "Connected",
  }),
  heading: "Ask across both pages",
  body: "Maho can compare the field document and architecture essay without replacing the browser context.",
  quickActions: Object.freeze([
    Object.freeze({ id: "analyze", label: "Analyze" }),
    Object.freeze({ id: "explain", label: "Explain" }),
    Object.freeze({ id: "summarize", label: "Summarize" }),
  ]),
  composer: Object.freeze({
    placeholder: "Ask a question about these pages...",
    model: "Maho Mini",
    sendLabel: "Send",
  }),
});

export function isCommandAiState(value) {
  return COMMAND_AI_STATES.includes(value);
}
