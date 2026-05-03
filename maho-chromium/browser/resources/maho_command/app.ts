import {PageHandlerFactory, PageHandlerRemote, Suggestion, SuggestionType} from './maho_command.mojom-webui.js';
import {PageCallbackRouter} from './maho_command.mojom-webui.js';

let pageHandler: PageHandlerRemote;
let suggestions: Suggestion[] = [];
let selectedIndex = -1;
let debounceTimer: number = 0;

const BADGE_CONFIG: Record<number, {label: string; className: string}> = {
  [SuggestionType.kTab]: {label: 'Tab', className: 'badge-tab'},
  [SuggestionType.kBookmark]: {label: 'Bookmark', className: 'badge-bookmark'},
  [SuggestionType.kHistory]: {label: 'History', className: 'badge-history'},
  [SuggestionType.kAction]: {label: 'Action', className: 'badge-action'},
  [SuggestionType.kNavigation]: {label: 'URL', className: 'badge-url'},
  [SuggestionType.kCalculator]: {label: 'Calc', className: 'badge-calc'},
  [SuggestionType.kUnitConversion]: {label: 'Convert', className: 'badge-calc'},
  [SuggestionType.kArchivedTab]: {label: 'Archived', className: 'badge-archived'},
  [SuggestionType.kClosedTab]: {label: 'Closed', className: 'badge-closed'},
};

function initialize() {
  const callbackRouter = new PageCallbackRouter();
  pageHandler = new PageHandlerRemote();

  const factory = PageHandlerFactory.getRemote();
  factory.createPageHandler(
      callbackRouter.$.bindNewPipeAndPassRemote(),
      pageHandler.$.bindNewPipeAndPassReceiver());

  const input = document.getElementById('search-input') as HTMLInputElement;
  input.addEventListener('input', () => {
    clearTimeout(debounceTimer);
    debounceTimer = window.setTimeout(() => runQuery(input.value), 100);
  });

  input.addEventListener('keydown', (e: KeyboardEvent) => {
    if (e.key === 'ArrowDown') {
      e.preventDefault();
      selectedIndex = Math.min(selectedIndex + 1, suggestions.length - 1);
      renderResults();
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      selectedIndex = Math.max(selectedIndex - 1, -1);
      renderResults();
    } else if (e.key === 'Enter' && selectedIndex >= 0) {
      e.preventDefault();
      openSelected();
    } else if (e.key === 'Escape') {
      suggestions = [];
      selectedIndex = -1;
      input.value = '';
      renderResults();
    }
  });
}

async function runQuery(input: string) {
  selectedIndex = -1;
  if (!input.trim()) {
    suggestions = [];
    renderResults();
    return;
  }
  const result = await pageHandler.query(input);
  suggestions = result.suggestions;
  if (suggestions.length > 0) {
    selectedIndex = 0;
  }
  renderResults();
}

function highlightTitle(title: string, ranges: Array<{start: number; end: number}>): string {
  if (!ranges || ranges.length === 0) {
    return escapeHtml(title);
  }

  const sorted = [...ranges].sort((a, b) => a.start - b.start);
  let result = '';
  let pos = 0;

  for (const range of sorted) {
    if (range.start > pos) {
      result += escapeHtml(title.slice(pos, range.start));
    }
    result += '<b>' + escapeHtml(title.slice(range.start, range.end)) + '</b>';
    pos = range.end;
  }

  if (pos < title.length) {
    result += escapeHtml(title.slice(pos));
  }

  return result;
}

function renderResults() {
  const container = document.getElementById('results')!;
  container.innerHTML = '';

  for (let i = 0; i < suggestions.length; i++) {
    const s = suggestions[i]!;
    const el = document.createElement('div');
    el.className = 'result-item' + (i === selectedIndex ? ' selected' : '');

    const config = BADGE_CONFIG[s.type] || {label: '', className: ''};
    const titleHtml = highlightTitle(s.title, s.matchRanges);
    const subtitleText = s.subtitle || s.url;

    el.innerHTML =
        `<span class="result-badge ${config.className}">${escapeHtml(config.label)}</span>` +
        `<div class="result-title">${titleHtml}</div>` +
        (subtitleText ? `<div class="result-url">${escapeHtml(subtitleText)}</div>` : '');

    el.addEventListener('click', () => {
      selectedIndex = i;
      openSelected();
    });
    container.appendChild(el);
  }
}

function openSelected() {
  if (selectedIndex < 0 || selectedIndex >= suggestions.length) {
    return;
  }
  const s = suggestions[selectedIndex]!;

  if (s.type === SuggestionType.kAction && s.actionId) {
    pageHandler.executeAction(s.actionId);
    return;
  }

  if (s.type === SuggestionType.kCalculator ||
      s.type === SuggestionType.kUnitConversion) {
    navigator.clipboard.writeText(s.title.split(' = ').pop() || s.title);
    return;
  }

  pageHandler.openSuggestion(selectedIndex, s.url, s.tabIndex);
}

function escapeHtml(text: string): string {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

document.addEventListener('DOMContentLoaded', initialize);
