import {LitElement, html, css, nothing} from 'lit';
import {property, customElement, state} from 'lit/decorators.js';

export {html, css, nothing, property, customElement, state};

export class CrLitElement extends LitElement {
  fire<T>(eventName: string, detail?: T): void {
    this.dispatchEvent(
      new CustomEvent(eventName, {
        detail,
        bubbles: true,
        composed: true,
      })
    );
  }

  $$(selector: string): HTMLElement | null {
    return this.shadowRoot?.querySelector(selector) ?? null;
  }
}
