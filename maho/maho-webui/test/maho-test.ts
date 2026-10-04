import {CrLitElement, html, css, customElement} from '../shared/lib/cr-lit-element.js';

declare global {
  interface Window {
    litElementVersions?: string[];
  }
}

@customElement('maho-test')
export class MahoTest extends CrLitElement {
  static override styles = css`
    :host {
      display: flex;
      align-items: center;
      justify-content: center;
      height: 100vh;
      font-family: var(--maho-font-family, system-ui);
      background: var(--maho-bg);
      color: var(--maho-text);
    }
    .container { text-align: center; }
    h1 { font-size: 2rem; margin-bottom: 0.5rem; }
    p { color: var(--maho-text-secondary); }
  `;

  override render() {
    return html`
      <div class="container">
        <h1>Hello, Maho!</h1>
        <p>chrome://maho-test/ — Gate G1 verification page</p>
        <p>WebUI framework: Lit ${window.litElementVersions?.[0] ?? '3.x'}</p>
      </div>
    `;
  }
}
