import {PageHandlerFactory, PageHandlerRemote, ChatMessage, ChatIntent, PageCallbackRouter} from './maho_ai.mojom-webui.js';

interface PagePreviewData {
  title: string;
  url: string;
  textContent: string;
}

const SVG_NS = 'http://www.w3.org/2000/svg';

let pageHandler: PageHandlerRemote;
let callbackRouter: PageCallbackRouter;
let sending = false;
let streamingElement: HTMLElement|null = null;
let pagePreviewData: PagePreviewData|null = null;

function createMessageElement(msg: ChatMessage): HTMLElement {
  const el = document.createElement('div');
  el.className = `message ${msg.role}`;
  el.textContent = msg.content;
  return el;
}

function getMessagesContainer(): HTMLElement {
  return document.getElementById('messages')!;
}

function createSvgElement<K extends keyof SVGElementTagNameMap>(
    tagName: K, attributes: Record<string, string> = {}): SVGElementTagNameMap[K] {
  const element = document.createElementNS(SVG_NS, tagName);
  for (const [name, value] of Object.entries(attributes)) {
    element.setAttribute(name, value);
  }
  return element;
}

function createIconSvg(
    className: string, viewBox: string,
    children: Array<{tag: keyof SVGElementTagNameMap, attributes: Record<string, string>}>,
    attributes: Record<string, string> = {}): SVGSVGElement {
  const svg = createSvgElement('svg', {
    class: className,
    viewBox,
    fill: 'none',
    stroke: 'currentColor',
    'stroke-width': '1.8',
    'stroke-linecap': 'round',
    'stroke-linejoin': 'round',
    'aria-hidden': 'true',
    ...attributes,
  });

  for (const child of children) {
    svg.appendChild(createSvgElement(child.tag, child.attributes));
  }

  return svg;
}

function createEmptyStateElement(): HTMLElement {
  const emptyState = document.createElement('div');
  emptyState.className = 'empty-state';
  emptyState.appendChild(createIconSvg('icon-large', '0 0 24 24', [
    {
      tag: 'path',
      attributes: {d: 'M12 20h9M16.5 3.5a2.121 2.121 0 113 3L7 19l-4 1 1-4L16.5 3.5z'},
    },
  ]));
  return emptyState;
}

function restoreEmptyState() {
  const container = getMessagesContainer();
  if (!container.childElementCount) {
    container.replaceChildren(createEmptyStateElement());
  }
}

function scrollToBottom() {
  const container = getMessagesContainer();
  container.scrollTop = container.scrollHeight;
}

function appendMessage(msg: ChatMessage) {
  const container = getMessagesContainer();
  const emptyState = container.querySelector('.empty-state');
  if (emptyState) {
    emptyState.remove();
  }
  container.appendChild(createMessageElement(msg));
  scrollToBottom();
}

function setSending(value: boolean) {
  sending = value;
  const sendBtn = document.getElementById('send-btn') as HTMLButtonElement | null;
  const textarea = document.getElementById('message-input') as HTMLTextAreaElement | null;
  const pills = document.querySelectorAll<HTMLButtonElement>('.pill');

  if (sendBtn) {
    sendBtn.disabled = value;
  }

  if (textarea) {
    textarea.disabled = value;
  }

  pills.forEach((pill: HTMLButtonElement) => {
    pill.disabled = value;
  });
}

function ensureStreamingElement(): HTMLElement {
  if (streamingElement) {
    return streamingElement;
  }

  const container = getMessagesContainer();
  const emptyState = container.querySelector('.empty-state');
  if (emptyState) {
    emptyState.remove();
  }

  streamingElement = document.createElement('div');
  streamingElement.className = 'message assistant streaming';
  container.appendChild(streamingElement);
  scrollToBottom();
  return streamingElement;
}

function truncateText(text: string, maxLength: number): string {
  if (text.length <= maxLength) {
    return text;
  }

  return `${text.slice(0, maxLength - 1)}…`;
}

function getDomain(url: string): string {
  try {
    return new URL(url).hostname.replace(/^www\./, '');
  } catch {
    return '';
  }
}

function isPreviewableUrl(url: string): boolean {
  if (!url) {
    return false;
  }

  try {
    const parsedUrl = new URL(url);
    return ![
      'about:',
      'chrome:',
      'chrome-untrusted:',
      'devtools:',
    ].some((prefix) => parsedUrl.protocol === prefix || parsedUrl.href.startsWith(prefix));
  } catch {
    return false;
  }
}

function showPagePreview() {
  const preview = document.getElementById('page-preview') as HTMLDivElement | null;
  const favicon = document.getElementById('preview-favicon') as HTMLDivElement | null;
  const title = document.getElementById('preview-title') as HTMLDivElement | null;
  const domain = document.getElementById('preview-domain') as HTMLDivElement | null;

  if (!preview || !favicon || !title || !domain || !pagePreviewData || !isPreviewableUrl(pagePreviewData.url)) {
    if (preview) {
      preview.style.display = 'none';
    }
    if (favicon) {
      favicon.replaceChildren();
    }
    return;
  }

  preview.style.display = 'flex';
  const img = document.createElement('img');
  img.src = `chrome://favicon2/?size=16&scaleFactor=2x&pageUrl=${encodeURIComponent(pagePreviewData.url)}`;
  img.alt = '';
  favicon.replaceChildren(img);
  title.textContent = truncateText(pagePreviewData.title || 'Current page', 25);
  domain.textContent = getDomain(pagePreviewData.url);
}

async function loadPagePreview() {
  const preview = document.getElementById('page-preview') as HTMLDivElement | null;
  if (!preview) {
    return;
  }

  try {
    const result = await pageHandler.getPageContent();
    if (!isPreviewableUrl(result.url)) {
      pagePreviewData = null;
      preview.style.display = 'none';
      const favicon = document.getElementById('preview-favicon') as HTMLDivElement | null;
      if (favicon) {
        favicon.replaceChildren();
      }
      return;
    }

    pagePreviewData = {
      title: result.title,
      url: result.url,
      textContent: result.textContent,
    };

    showPagePreview();
    return;
  } catch {
    pagePreviewData = null;
  }

  preview.style.display = 'none';
}

function sendMessage(intent: ChatIntent, text: string) {
  if (!text.trim() || sending) {
    return;
  }

  setSending(true);

  const userMsg: ChatMessage = {
    role: 'user',
    content: text,
    timestamp: Date.now() / 1000,
  };
  appendMessage(userMsg);

  streamingElement = null;
  pageHandler.sendMessage(intent, text);
}

function createToolbarButton(
    options: {
      className: string;
      id?: string;
      ariaLabel: string;
      icon: SVGSVGElement;
    }): HTMLButtonElement {
  const button = document.createElement('button');
  button.className = options.className;
  if (options.id) {
    button.id = options.id;
  }
  button.setAttribute('aria-label', options.ariaLabel);
  button.appendChild(options.icon);
  return button;
}

function createTextButton(
    className: string, text: string, attributes: Record<string, string> = {}): HTMLButtonElement {
  const button = document.createElement('button');
  button.className = className;
  for (const [name, value] of Object.entries(attributes)) {
    button.setAttribute(name, value);
  }
  button.textContent = text;
  return button;
}

function buildUI() {
  const app = document.getElementById('app')!;

  const toolbar = document.createElement('div');
  toolbar.className = 'toolbar';

  const newChatBtn = createToolbarButton({
    className: 'toolbar-btn',
    id: 'new-chat-btn',
    ariaLabel: 'New chat',
    icon: createIconSvg('icon', '0 0 24 24', [
      {tag: 'path', attributes: {d: 'M12 20h9M16.5 3.5a2.121 2.121 0 113 3L7 19l-4 1 1-4L16.5 3.5z'}},
    ]),
  });
  toolbar.appendChild(newChatBtn);

  const toolbarActions = document.createElement('div');
  toolbarActions.className = 'toolbar-actions';

  const closeBtn = createToolbarButton({
    className: 'toolbar-btn',
    id: 'close-btn',
    ariaLabel: 'Close',
    icon: createIconSvg('icon', '0 0 24 24', [
      {tag: 'line', attributes: {x1: '18', y1: '6', x2: '6', y2: '18'}},
      {tag: 'line', attributes: {x1: '6', y1: '6', x2: '18', y2: '18'}},
    ]),
  });
  toolbarActions.appendChild(closeBtn);

  toolbar.appendChild(toolbarActions);

  const messages = document.createElement('div');
  messages.id = 'messages';
  messages.className = 'messages';
  messages.appendChild(createEmptyStateElement());

  const actionPills = document.createElement('div');
  actionPills.className = 'action-pills';
  actionPills.appendChild(createTextButton('pill', 'Analyze', {'data-action': 'analyze'}));
  actionPills.appendChild(createTextButton('pill', 'Explain', {'data-action': 'explain'}));
  actionPills.appendChild(createTextButton('pill', 'Summarize', {'data-action': 'summarize'}));

  const composerCard = document.createElement('div');
  composerCard.className = 'composer-card';

  const pagePreview = document.createElement('div');
  pagePreview.className = 'page-preview';
  pagePreview.id = 'page-preview';
  pagePreview.style.display = 'none';

  const previewFavicon = document.createElement('div');
  previewFavicon.className = 'preview-icon';
  previewFavicon.id = 'preview-favicon';

  const previewText = document.createElement('div');
  previewText.className = 'preview-text';

  const previewTitle = document.createElement('div');
  previewTitle.className = 'preview-title';
  previewTitle.id = 'preview-title';

  const previewDomain = document.createElement('div');
  previewDomain.className = 'preview-domain';
  previewDomain.id = 'preview-domain';

  previewText.append(previewTitle, previewDomain);

  const previewCloseBtn = createTextButton('preview-close', '×', {
    id: 'preview-close',
    'aria-label': 'Remove',
  });

  pagePreview.append(previewFavicon, previewText, previewCloseBtn);

  const textarea = document.createElement('textarea');
  textarea.id = 'message-input';
  textarea.placeholder = 'Ask a question about this page…';
  textarea.rows = 1;

  const composerControls = document.createElement('div');
  composerControls.className = 'composer-controls';

  const controlsLeft = document.createElement('div');
  controlsLeft.className = 'controls-left';
  const attachBtn = createToolbarButton({
    className: 'control-btn',
    id: 'attach-btn',
    ariaLabel: 'Attach',
    icon: createIconSvg('icon', '0 0 24 24', [
      {tag: 'line', attributes: {x1: '12', y1: '5', x2: '12', y2: '19'}},
      {tag: 'line', attributes: {x1: '5', y1: '12', x2: '19', y2: '12'}},
    ]),
  });
  controlsLeft.appendChild(attachBtn);
  const moreBtn = createToolbarButton({
    className: 'control-btn',
    id: 'more-btn',
    ariaLabel: 'More',
    icon: createIconSvg('icon', '0 0 24 24', [
      {tag: 'circle', attributes: {cx: '5', cy: '12', r: '1.5'}},
      {tag: 'circle', attributes: {cx: '12', cy: '12', r: '1.5'}},
      {tag: 'circle', attributes: {cx: '19', cy: '12', r: '1.5'}},
    ], {fill: 'currentColor', stroke: 'none'}),
  });
  controlsLeft.appendChild(moreBtn);

  const controlsRight = document.createElement('div');
  controlsRight.className = 'controls-right';

  const sendBtn = createToolbarButton({
    className: 'send-btn',
    id: 'send-btn',
    ariaLabel: 'Send',
    icon: createIconSvg('icon', '0 0 24 24', [
      {tag: 'line', attributes: {x1: '12', y1: '19', x2: '12', y2: '5'}},
      {tag: 'polyline', attributes: {points: '5 12 12 5 19 12'}},
    ],),
  });
  controlsRight.appendChild(sendBtn);

  composerControls.append(controlsLeft, controlsRight);
  composerCard.append(pagePreview, textarea, composerControls);

  app.replaceChildren(toolbar, messages, actionPills, composerCard);

  const pills = document.querySelectorAll<HTMLButtonElement>('.pill');

  const dispatchMessage = () => {
    const text = textarea.value.trim();
    if (!text) {
      return;
    }

    sendMessage(ChatIntent.kFreeform, text);
    textarea.value = '';
    textarea.style.height = 'auto';
  };

  sendBtn.addEventListener('click', dispatchMessage);

  textarea.addEventListener('keydown', (e: KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      dispatchMessage();
    }
  });

  textarea.addEventListener('input', () => {
    textarea.style.height = 'auto';
    textarea.style.height = `${Math.min(textarea.scrollHeight, 160)}px`;
  });

  pills.forEach((pill: HTMLButtonElement) => {
    pill.addEventListener('click', () => {
      const action = pill.dataset['action'];
      const intent = action === 'analyze' ? ChatIntent.kAnalyzeCurrentPage :
          action === 'explain' ? ChatIntent.kExplainCurrentPage :
                                ChatIntent.kSummarizeCurrentPage;
      const prompt = action === 'analyze' ? 'Analyze this page' :
          action === 'explain' ? 'Explain this page' :
                               'Summarize this page';
      sendMessage(intent, prompt);
    });
  });

  previewCloseBtn.addEventListener('click', () => {
    const preview = document.getElementById('page-preview') as HTMLDivElement;
    preview.style.display = 'none';
    pagePreviewData = null;
    const favicon = document.getElementById('preview-favicon') as HTMLDivElement | null;
    if (favicon) {
      favicon.replaceChildren();
    }
  });

  attachBtn.addEventListener('click', async () => {
    if (pagePreviewData) {
      showPagePreview();
      return;
    }

    await loadPagePreview();
  });

  moreBtn.addEventListener('click', () => {
    window.open('chrome://maho-settings', '_blank');
  });

  newChatBtn.addEventListener('click', () => {
    pageHandler.clearHistory();
    streamingElement = null;
    pagePreviewData = null;
    setSending(false);
    const preview = document.getElementById('page-preview') as HTMLDivElement | null;
    if (preview) {
      preview.style.display = 'none';
    }
    const favicon = document.getElementById('preview-favicon') as HTMLDivElement | null;
    if (favicon) {
      favicon.replaceChildren();
    }
    const container = getMessagesContainer();
    container.replaceChildren();
    restoreEmptyState();
  });

  closeBtn.addEventListener('click', () => {
    window.close();
  });
}

function initialize() {
  callbackRouter = new PageCallbackRouter();
  pageHandler = new PageHandlerRemote();

  const factory = PageHandlerFactory.getRemote();
  factory.createPageHandler(
      callbackRouter.$.bindNewPipeAndPassRemote(),
      pageHandler.$.bindNewPipeAndPassReceiver());

  callbackRouter.onStreamingResponse.addListener((partialContent: string) => {
    const el = ensureStreamingElement();
    el.textContent += partialContent;
    scrollToBottom();
  });

  callbackRouter.onStreamingComplete.addListener((response: ChatMessage) => {
    if (streamingElement) {
      streamingElement.classList.remove('streaming');
      streamingElement.textContent = response.content;
      streamingElement = null;
    } else {
      appendMessage(response);
    }
    setSending(false);
  });

  buildUI();
  loadPagePreview();

  pageHandler.getChatHistory().then(({messages}: {messages: ChatMessage[]}) => {
    for (const msg of messages) {
      appendMessage(msg);
    }
  });
}

document.addEventListener('DOMContentLoaded', initialize);
