import {CrLitElement, html, css, customElement, state} from '../shared/lib/cr-lit-element.js';
import {getMojoHandler} from '../shared/lib/mojo-mock.js';
import type {MahoAIPageHandler} from '../shared/lib/mojo-mock.js';
import type {Conversation, ChatMessage} from '../shared/lib/types.js';
import './message-bubble.js';

@customElement('maho-ai-panel')
export class MahoAIPanel extends CrLitElement {
  static override styles = css`
    :host {
      display: flex;
      flex-direction: column;
      height: 100vh;
      background: var(--maho-bg, #1a1a1a);
      color: var(--maho-text, #e0e0e0);
    }

    .toolbar {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 10px 16px;
      border-bottom: 1px solid var(--maho-border, #333);
    }

    .toolbar-left {
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .toolbar h2 {
      font-size: 15px;
      font-weight: 600;
    }

    .new-btn {
      background: none;
      border: 1px solid var(--maho-border, #333);
      color: var(--maho-text-secondary, #999);
      font-size: 12px;
      padding: 4px 10px;
      border-radius: 6px;
      cursor: pointer;
    }

    .new-btn:hover {
      border-color: var(--maho-accent, #5B9CF6);
      color: var(--maho-accent, #5B9CF6);
    }

    .model-select {
      background: var(--maho-surface, #2a2a2a);
      border: 1px solid var(--maho-border, #333);
      color: var(--maho-text, #e0e0e0);
      font-size: 12px;
      padding: 4px 8px;
      border-radius: 6px;
    }

    .page-context {
      font-size: 11px;
      color: var(--maho-text-secondary, #666);
      padding: 6px 16px;
      border-bottom: 1px solid var(--maho-border, #333);
      display: flex;
      align-items: center;
      gap: 6px;
    }

    .messages {
      flex: 1;
      overflow-y: auto;
      padding: 16px;
      display: flex;
      flex-direction: column;
      gap: 12px;
    }

    .input-area {
      border-top: 1px solid var(--maho-border, #333);
      padding: 12px 16px;
      display: flex;
      gap: 8px;
      align-items: flex-end;
    }

    .input-area textarea {
      flex: 1;
      background: var(--maho-surface, #2a2a2a);
      border: 1px solid var(--maho-border, #333);
      border-radius: 8px;
      padding: 8px 12px;
      color: var(--maho-text, #e0e0e0);
      font-size: 14px;
      font-family: var(--maho-font-family, system-ui);
      resize: none;
      outline: none;
      min-height: 40px;
      max-height: 120px;
    }

    .input-area textarea:focus {
      border-color: var(--maho-accent, #5B9CF6);
    }

    .send-btn {
      width: 36px;
      height: 36px;
      border: none;
      background: var(--maho-accent, #5B9CF6);
      color: white;
      border-radius: 8px;
      cursor: pointer;
      font-size: 16px;
      flex-shrink: 0;
      transition: background 0.15s;
    }

    .send-btn:hover {
      background: var(--maho-accent-hover, #7bb3f7);
    }

    .send-btn:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }

    .typing-indicator {
      display: flex;
      gap: 4px;
      padding: 8px 12px;
    }

    .typing-dot {
      width: 6px;
      height: 6px;
      background: var(--maho-text-secondary, #999);
      border-radius: 50%;
      animation: typing 1.4s infinite;
    }

    .typing-dot:nth-child(2) { animation-delay: 0.2s; }
    .typing-dot:nth-child(3) { animation-delay: 0.4s; }

    @keyframes typing {
      0%, 60%, 100% { opacity: 0.3; }
      30% { opacity: 1; }
    }
  `;

  @state() conversations: Conversation[] = [];
  @state() activeConversation: Conversation | null = null;
  @state() inputText: string = '';
  @state() isStreaming: boolean = false;
  @state() selectedModel: string = 'maho-ai';

  private handler = getMojoHandler<MahoAIPageHandler>('MahoAIPageHandler');

  override async connectedCallback() {
    super.connectedCallback();
    this.conversations = await this.handler.getConversations();
    if (this.conversations.length > 0) {
      this.activeConversation = this.conversations[0];
    }
  }

  private async onSend() {
    if (!this.inputText.trim() || this.isStreaming) return;

    const userMsg: ChatMessage = {
      id: `m_${Date.now()}`,
      role: 'user',
      content: this.inputText.trim(),
      timestamp: Date.now(),
      isStreaming: false,
    };

    const assistantMsg: ChatMessage = {
      id: `m_${Date.now() + 1}`,
      role: 'assistant',
      content: '',
      timestamp: Date.now(),
      isStreaming: true,
    };

    if (this.activeConversation) {
      this.activeConversation = {
        ...this.activeConversation,
        messages: [...this.activeConversation.messages, userMsg, assistantMsg],
      };
    }

    this.inputText = '';
    this.isStreaming = true;

    await this.handler.streamResponse((chunk: string) => {
      if (this.activeConversation) {
        const messages = [...this.activeConversation.messages];
        const lastMsg = messages[messages.length - 1];
        messages[messages.length - 1] = {...lastMsg, content: lastMsg.content + chunk};
        this.activeConversation = {...this.activeConversation, messages};
        this.requestUpdate();
      }
    });

    if (this.activeConversation) {
      const messages = [...this.activeConversation.messages];
      const lastMsg = messages[messages.length - 1];
      messages[messages.length - 1] = {...lastMsg, isStreaming: false};
      this.activeConversation = {...this.activeConversation, messages};
    }
    this.isStreaming = false;
  }

  private onInputChange(e: Event) {
    this.inputText = (e.target as HTMLTextAreaElement).value;
  }

  private onKeyDown(e: KeyboardEvent) {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      this.onSend();
    }
  }

  private onModelChange(e: Event) {
    this.selectedModel = (e.target as HTMLSelectElement).value;
  }

  private onNewConversation() {
    this.activeConversation = {
      id: `c_${Date.now()}`,
      title: 'New Conversation',
      messages: [],
      createdAt: Date.now(),
      updatedAt: Date.now(),
    };
  }

  override render() {
    const messages = this.activeConversation?.messages ?? [];

    return html`
      <div class="toolbar">
        <div class="toolbar-left">
          <h2>AI Assistant</h2>
          <select class="model-select" @change=${this.onModelChange}>
            <option value="maho-ai">Claude 3.5 Sonnet</option>
            <option value="gpt-4o">GPT-4o</option>
            <option value="gemini-pro">Gemini Pro</option>
          </select>
        </div>
        <button class="new-btn" @click=${this.onNewConversation}>+ New</button>
      </div>

      ${/* TODO(B4.2): Replace with dynamic page context from Mojo IPC (MahoAIPanelPageHandler.getPageContext()) */ ''}
      <div class="page-context">
        <span>📄</span>
        <span>Reading: GitHub - maho-browser/maho</span>
      </div>

      <div class="messages">
        ${messages.map(msg => html`
          <message-bubble .message=${msg}></message-bubble>
        `)}
        ${this.isStreaming ? html`
          <div class="typing-indicator">
            <div class="typing-dot"></div>
            <div class="typing-dot"></div>
            <div class="typing-dot"></div>
          </div>
        ` : ''}
      </div>

      <div class="input-area">
        <textarea
          placeholder="Ask anything about this page…"
          .value=${this.inputText}
          @input=${this.onInputChange}
          @keydown=${this.onKeyDown}
          rows="1"
        ></textarea>
        <button
          class="send-btn"
          @click=${this.onSend}
          ?disabled=${!this.inputText.trim() || this.isStreaming}
        >↑</button>
      </div>
    `;
  }
}
