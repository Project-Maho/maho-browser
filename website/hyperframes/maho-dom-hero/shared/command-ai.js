import { createSplitView } from "./demo-pages.js";
import { SPLIT_VIEW_PRESETS } from "./split-data.js";
import {
  AI_PANEL,
  COMMAND_AI_STATES,
  COMMAND_MODES,
  COMMAND_QUERY,
  COMMAND_RESULTS,
  isCommandAiState,
} from "./command-data.js";

function element(tagName, className, text) {
  const node = document.createElement(tagName);
  if (className) node.className = className;
  if (text) node.textContent = text;
  return node;
}

function appendText(parent, tagName, className, text) {
  parent.append(element(tagName, className, text));
}

function createModeSwitch() {
  const modes = element("div", "command-palette__modes");
  modes.setAttribute("aria-label", "Command mode");
  for (const mode of COMMAND_MODES) {
    const item = element("span", "command-palette__mode", mode.label);
    item.dataset.commandMode = mode.id;
    item.dataset.active = String(mode.active);
    modes.append(item);
  }
  return modes;
}

function createCommandRow(result) {
  const row = element("li", "command-palette__result");
  row.dataset.commandRow = result.kind;
  row.setAttribute("data-command-selected", String(result.selected));
  row.setAttribute("aria-selected", String(result.selected));

  const mark = element("span", "command-palette__mark", result.mark);
  mark.setAttribute("aria-hidden", "true");
  const copy = element("span", "command-palette__result-copy");
  appendText(copy, "strong", "command-palette__result-title", result.title);
  appendText(copy, "span", "command-palette__result-detail", result.detail);
  row.append(mark, copy);
  return row;
}

function createCommandPalette() {
  const palette = element("section", "command-palette");
  palette.dataset.commandPaletteSurface = "local-dom";
  palette.setAttribute("aria-label", "Command Bar palette");

  const query = element("div", "command-palette__query");
  const queryMark = element("span", "command-palette__query-mark", "M");
  queryMark.setAttribute("aria-hidden", "true");
  const queryCopy = element("span", "command-palette__query-copy");
  appendText(queryCopy, "strong", "command-palette__query-value", COMMAND_QUERY.value);
  appendText(queryCopy, "span", "command-palette__query-context", COMMAND_QUERY.context);
  query.append(queryMark, queryCopy, createModeSwitch());

  const results = element("ul", "command-palette__results");
  results.setAttribute("role", "listbox");
  results.setAttribute("aria-label", "Command results");
  for (const result of COMMAND_RESULTS) results.append(createCommandRow(result));

  const footer = element("footer", "command-palette__footer");
  appendText(footer, "span", "command-palette__hint", "Return to open");
  appendText(footer, "span", "command-palette__hint", "Tab to switch mode");
  appendText(footer, "span", "command-palette__hint", "Esc to close");
  palette.append(query, results, footer);
  return palette;
}

function createQuickActions() {
  const actions = element("div", "command-ai-panel__actions");
  actions.setAttribute("aria-label", "Page actions");
  for (const action of AI_PANEL.quickActions) {
    const button = element("span", "command-ai-panel__action", action.label);
    button.dataset.aiAction = action.id;
    actions.append(button);
  }
  return actions;
}

function createComposer() {
  const composer = element("section", "command-ai-panel__composer");
  composer.setAttribute("aria-label", "Ask Maho composer");
  appendText(composer, "p", "command-ai-panel__placeholder", AI_PANEL.composer.placeholder);
  const controls = element("div", "command-ai-panel__composer-controls");
  appendText(controls, "span", "command-ai-panel__attach", "Add context");
  appendText(controls, "span", "command-ai-panel__model", AI_PANEL.composer.model);
  appendText(controls, "span", "command-ai-panel__send", AI_PANEL.composer.sendLabel);
  composer.append(controls);
  return composer;
}

function createAiPanel() {
  const panel = element("aside", "command-ai-panel");
  panel.dataset.aiPanelSurface = "local-dom";
  panel.dataset.dock = "right";
  panel.setAttribute("aria-label", "Maho AI panel with visible page context");

  const header = element("header", "command-ai-panel__header");
  const identity = element("div", "command-ai-panel__identity");
  appendText(identity, "span", "command-ai-panel__avatar", "M");
  const title = element("span", "command-ai-panel__title-group");
  appendText(title, "strong", "command-ai-panel__title", AI_PANEL.title);
  appendText(title, "span", "command-ai-panel__context-label", AI_PANEL.contextLabel);
  identity.append(title);
  appendText(header, "span", "command-ai-panel__status", AI_PANEL.status);
  header.append(identity);

  const connection = element("section", "command-ai-panel__connection");
  const connectionCopy = element("span", "command-ai-panel__connection-copy");
  appendText(connectionCopy, "span", "command-ai-panel__eyebrow", AI_PANEL.connection.eyebrow);
  appendText(connectionCopy, "strong", "command-ai-panel__connection-title", AI_PANEL.connection.title);
  appendText(connection, "span", "command-ai-panel__connection-action", AI_PANEL.connection.action);
  connection.prepend(connectionCopy);

  const emptyState = element("section", "command-ai-panel__empty");
  appendText(emptyState, "span", "command-ai-panel__context-glyph", "M");
  appendText(emptyState, "h2", "command-ai-panel__heading", AI_PANEL.heading);
  appendText(emptyState, "p", "command-ai-panel__body", AI_PANEL.body);

  panel.append(header, connection, emptyState, createQuickActions(), createComposer());
  return panel;
}

export function getCommandAiStateDescriptor(state) {
  if (!isCommandAiState(state)) throw new RangeError(`Unknown command AI state: ${String(state)}`);
  return Object.freeze({
    state,
    palette: state === "palette" ? "open" : "closed",
    aiPanel: state === "ai" ? "open" : "closed",
    browserContext: "visible",
    splitContext: `${SPLIT_VIEW_PRESETS.initial.left}:${SPLIT_VIEW_PRESETS.initial.right}`,
    selectedResultId: COMMAND_RESULTS.find((result) => result.selected)?.id ?? null,
  });
}

export function createCommandAiScene(options = {}) {
  const state = options.state ?? COMMAND_AI_STATES[0];
  const descriptor = getCommandAiStateDescriptor(state);
  const root = element("section", "command-ai");
  root.dataset.commandAiState = descriptor.state;
  root.dataset.commandPalette = descriptor.palette;
  root.dataset.aiPanel = descriptor.aiPanel;
  root.dataset.browserContext = descriptor.browserContext;
  root.setAttribute("aria-label", "Command Bar and AI states over the visible Split View");

  const context = element("div", "command-ai__context");
  context.dataset.browserContext = "visible";
  context.append(createSplitView(SPLIT_VIEW_PRESETS.initial));

  const overlay = element("div", "command-ai__overlay");
  overlay.dataset.commandOverlay = state;
  if (state === "palette") overlay.append(createCommandPalette());
  if (state === "ai") overlay.append(createAiPanel());
  root.append(context, overlay);
  return root;
}

export function mountCommandAiScene(target, options = {}) {
  const scene = createCommandAiScene(options);
  target.replaceChildren(scene);
  return scene;
}
