export function createElement<K extends keyof HTMLElementTagNameMap>(
    tagName: K, className?: string,
    textContent?: string): HTMLElementTagNameMap[K] {
  const element = document.createElement(tagName);
  if (className) {
    element.className = className;
  }
  if (textContent !== undefined) {
    element.textContent = textContent;
  }
  return element;
}

export function createButton(
    text: string, className: string, disabled: boolean,
    onClick: () => void): HTMLButtonElement {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = className;
  button.disabled = disabled;
  button.textContent = text;
  button.addEventListener('click', onClick);
  return button;
}

export function createEditorLabel(
    forId: string, text: string): HTMLLabelElement {
  const label = createElement('label', 'boost-editor-label', text);
  label.htmlFor = forId;
  return label;
}

export function createMetaPill(text: string): HTMLElement {
  return createElement('span', 'boost-meta-pill', text);
}
