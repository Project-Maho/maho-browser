import {CrLitElement, html, css, customElement, property} from '../shared/lib/cr-lit-element.js';
import type {ChatMessage} from '../shared/lib/types.js';

@customElement('message-bubble')
export class MessageBubble extends CrLitElement {
  static override styles = css`
    :host {
      display: block;
    }

    .bubble {
      max-width: 85%;
      padding: 10px 14px;
      border-radius: 12px;
      font-size: 14px;
      line-height: 1.5;
      word-break: break-word;
      position: relative;
    }

    .user {
      margin-left: auto;
      background: var(--maho-accent, #5B9CF6);
      color: white;
      border-bottom-right-radius: 4px;
    }

    .assistant {
      margin-right: auto;
      background: var(--maho-surface, #2a2a2a);
      color: var(--maho-text, #e0e0e0);
      border-bottom-left-radius: 4px;
    }

    .content {
      white-space: pre-wrap;
    }

    .timestamp {
      font-size: 10px;
      color: var(--maho-text-secondary, #666);
      margin-top: 4px;
    }

    .user .timestamp {
      text-align: right;
      color: rgba(255, 255, 255, 0.6);
    }

    .copy-btn {
      display: none;
      position: absolute;
      top: 6px;
      right: 6px;
      width: 24px;
      height: 24px;
      border: none;
      background: rgba(0, 0, 0, 0.3);
      color: white;
      border-radius: 4px;
      cursor: pointer;
      font-size: 11px;
    }

    .bubble:hover .copy-btn {
      display: block;
    }

    code {
      background: rgba(0, 0, 0, 0.2);
      padding: 1px 4px;
      border-radius: 3px;
      font-size: 13px;
      font-family: 'SF Mono', ui-monospace, monospace;
    }

    pre {
      background: rgba(0, 0, 0, 0.3);
      padding: 10px;
      border-radius: 6px;
      overflow-x: auto;
      margin: 8px 0;
    }

    pre code {
      background: none;
      padding: 0;
    }
  `;

  @property({attribute: false}) message!: ChatMessage;

  private formatTime(ts: number): string {
    return new Date(ts).toLocaleTimeString([], {hour: '2-digit', minute: '2-digit'});
  }

  private onCopy() {
    navigator.clipboard.writeText(this.message.content);
  }

  override render() {
    const m = this.message;
    return html`
      <div class="bubble ${m.role}">
        <button class="copy-btn" @click=${this.onCopy}>📋</button>
        <div class="content">${m.content}</div>
        <div class="timestamp">${this.formatTime(m.timestamp)}</div>
      </div>
    `;
  }
}
