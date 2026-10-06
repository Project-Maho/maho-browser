import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {Markdown} from '../components/markdown.js';
import {ConversationMessage} from '../features/compact/conversation-message.js';
import type {ConversationItem} from '../../views/conversation_thread.js';
import {MATH_LIMITS, renderTexToMathML} from '../lib/tex-math.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

/** Every element the renderer is allowed to emit inside `<math>`. */
const ALLOWED_MATH_ELEMENTS = new Set([
  'math', 'mrow', 'mi', 'mn', 'mo', 'mtext', 'mspace', 'mfrac', 'msqrt',
  'mroot', 'msup', 'msub', 'msubsup', 'munder', 'mover', 'munderover',
]);

function requireElement<T extends Element>(element: T|null, description: string): T {
  if (element) {
    return element;
  }

  throw new Error(`Missing ${description}`);
}

describe('Markdown equation rendering', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  function renderMarkdown(text: string): HTMLDivElement {
    act(() => root.render(<Markdown text={text} />));
    return container;
  }

  function mathElement(): Element {
    return requireElement(container.querySelector('math'), 'rendered equation');
  }

  it('renders an inline equation as MathML inside its sentence', () => {
    renderMarkdown('The energy is $E = mc^2$ exactly.');

    const math = mathElement();
    expect(math.getAttribute('display')).toBe('inline');
    expect(math.getAttribute('class')).toContain('maho-math-inline');
    expect(math.querySelector('msup')).not.toBeNull();
    expect(math.textContent).toBe('E=mc2');
    expect(container.textContent).toContain('The energy is');
    expect(container.textContent).toContain('exactly.');
  });

  it('renders a display equation as a block outside a paragraph', () => {
    renderMarkdown('$$\n\\int_0^1 x^2\\,dx\n$$');

    const math = mathElement();
    expect(math.getAttribute('display')).toBe('block');
    expect(math.getAttribute('class')).toContain('maho-math-block');
    expect(math.querySelector('msubsup')).not.toBeNull();
    expect(math.querySelector('mfrac')).toBeNull();
    expect(container.querySelector('p')).toBeNull();
  });

  it('renders fractions, roots and stretchy delimiters', () => {
    renderMarkdown('$\\left( \\frac{\\sqrt[3]{x}}{2} \\right)$');

    const math = mathElement();
    expect(math.querySelector('mfrac')).not.toBeNull();
    expect(math.querySelector('mroot')).not.toBeNull();
    const delimiters = math.querySelectorAll('mo[stretchy="true"]');
    expect(delimiters).toHaveLength(2);
    expect(delimiters[0]?.textContent).toBe('(');
    expect(delimiters[1]?.textContent).toBe(')');
  });

  it('supports backslash-paren and backslash-bracket delimiters', () => {
    renderMarkdown('Inline \\(a+b\\) and display:\n\n\\[\n\\sum_{i=1}^{n} i\n\\]');

    const math = container.querySelectorAll('math');
    expect(math).toHaveLength(2);
    expect(math[0]?.getAttribute('display')).toBe('inline');
    expect(math[1]?.getAttribute('display')).toBe('block');
    expect(math[1]?.querySelector('munderover')).not.toBeNull();
  });

  it('renders nothing as math until a streamed expression completes', () => {
    renderMarkdown('Summing $x^2');
    expect(container.querySelector('math')).toBeNull();
    expect(container.textContent?.trim()).toBe('Summing $x^2');

    renderMarkdown('Summing $x^2 + y$');
    expect(mathElement().querySelector('msup')).not.toBeNull();
    expect(container.textContent?.trim()).toBe('Summing x2+y');
  });

  it('keeps a streamed display equation literal until it closes', () => {
    renderMarkdown('$$\n\\frac{1}{2}');
    expect(container.querySelector('math')).toBeNull();
    expect(container.textContent).toContain('\\frac{1}{2}');
  });

  it('falls back to the literal source for invalid syntax', () => {
    renderMarkdown('Broken $\\frac{1}{$ here.');

    expect(container.querySelector('math')).toBeNull();
    const fallback = requireElement(
        container.querySelector('.maho-math-fallback'), 'literal fallback');
    expect(fallback.textContent).toBe('$\\frac{1}{$');
    expect(container.textContent?.trim()).toBe('Broken $\\frac{1}{$ here.');
  });

  it('falls back for environments and unknown commands', () => {
    renderMarkdown('$\\begin{cases} a \\end{cases}$');

    expect(container.querySelector('math')).toBeNull();
    expect(container.textContent?.trim()).toBe('$\\begin{cases} a \\end{cases}$');
  });

  it('leaves code spans and fenced blocks untouched', () => {
    renderMarkdown(
        'Inline `$a$` stays.\n\n```\n$b^2$\n```\n\n$$\nc^2\n$$');

    expect(container.querySelectorAll('math')).toHaveLength(1);
    expect(requireElement(container.querySelector('code'), 'code span').textContent)
        .toBe('$a$');
    expect(requireElement(container.querySelector('pre'), 'code fence').textContent)
        .toContain('$b^2$');
  });

  it('keeps prose dollar amounts literal', () => {
    renderMarkdown('It costs $5 and $10 today.');

    expect(container.querySelector('math')).toBeNull();
    expect(container.textContent?.trim()).toBe('It costs $5 and $10 today.');
  });

  it('escapes hostile math input instead of emitting markup', () => {
    renderMarkdown('$<img src=x onerror=alert(1)>$');

    const math = mathElement();
    expect(container.querySelector('img')).toBeNull();
    expect(container.innerHTML).not.toContain('<img');
    expect(math.textContent).toContain('<img');
    expect(math.textContent).toContain('onerror=alert(1)');
    for (const node of math.querySelectorAll('*')) {
      expect(ALLOWED_MATH_ELEMENTS.has(node.tagName.toLowerCase())).toBe(true);
    }
    expect(container.innerHTML).toContain('&lt;');
  });

  it('refuses commands that could reach outside the renderer', () => {
    renderMarkdown('$\\href{javascript:alert(1)}{x}$');

    expect(container.querySelector('math')).toBeNull();
    expect(container.querySelector('[href]')).toBeNull();
    expect(container.textContent?.trim()).toBe('$\\href{javascript:alert(1)}{x}$');
  });

  it('escapes markup inside a text group', () => {
    renderMarkdown('$\\text{<script>alert(1)</script>}$');

    const math = mathElement();
    expect(container.querySelector('script')).toBeNull();
    expect(requireElement(math.querySelector('mtext'), 'text token').textContent)
        .toBe('<script>alert(1)</script>');
    expect(container.innerHTML).toContain('&lt;script&gt;');
  });

  it('emits only allowlisted elements and no resource attributes', () => {
    renderMarkdown(
        '$\\frac{\\sqrt[3]{x}}{2} \\sum_{i=1}^{n} \\left( a_i \\right)$');

    const math = mathElement();
    const names = [...math.querySelectorAll('*')].map(node => node.tagName.toLowerCase());
    expect(names.length).toBeGreaterThan(0);
    for (const name of names) {
      expect(ALLOWED_MATH_ELEMENTS.has(name)).toBe(true);
    }
    expect(math.querySelector('[href], [src], [style], [onerror]')).toBeNull();
  });

  it('falls back when an expression exceeds the rendering limits', () => {
    renderMarkdown(`$${'a'.repeat(MATH_LIMITS.maxSourceLength)}$`);
    expect(container.querySelector('math')).toBeNull();
    expect(container.querySelector('.maho-math-fallback')).not.toBeNull();

    renderMarkdown(`$${'a'.repeat(MATH_LIMITS.maxTokens + 1)}$`);
    expect(container.querySelector('math')).toBeNull();
    expect(container.querySelector('.maho-math-fallback')).not.toBeNull();
  });

  it('falls back when an expression nests deeper than the limit', () => {
    const nested = `${'{'.repeat(MATH_LIMITS.maxDepth + 4)}a` +
        `${'}'.repeat(MATH_LIMITS.maxDepth + 4)}`;
    renderMarkdown(`$${nested}$`);

    expect(container.querySelector('math')).toBeNull();
    expect(container.querySelector('.maho-math-fallback')).not.toBeNull();
  });

  it('applies the same ceilings through the converter itself', () => {
    expect(renderTexToMathML('x^2', false, {...MATH_LIMITS, maxNodes: 3}).ok)
        .toBe(false);
    expect(renderTexToMathML('x^2', false).ok).toBe(true);
    const tooManyTokens = renderTexToMathML('a'.repeat(MATH_LIMITS.maxTokens + 1), false);
    expect(tooManyTokens.ok).toBe(false);
    expect(tooManyTokens.ok ? '' : tooManyTokens.reason).toContain('tokens');
  });

  it('preserves ordinary Markdown, links and HTML escaping', () => {
    renderMarkdown(
        '## Heading\n\n- one\n- two\n\n[link](https://example.com)\n\n<b>raw</b>');

    expect(requireElement(container.querySelector('h2'), 'heading').textContent)
        .toBe('Heading');
    expect(container.querySelectorAll('li')).toHaveLength(2);
    expect(requireElement(container.querySelector('a'), 'link').getAttribute('href'))
        .toBe('https://example.com');
    expect(container.querySelector('b')).toBeNull();
    expect(container.textContent).toContain('<b>raw</b>');
  });

  it('keeps the caller classes alongside the math layout classes', () => {
    act(() => root.render(<Markdown className="text-sm [&_p]:m-0" text="$x$" />));

    const classes = requireElement(
        container.querySelector('div'), 'markdown wrapper').className;
    expect(classes).toContain('text-sm');
    expect(classes).toContain('[&_p]:m-0');
    expect(classes).toContain('[&_.maho-math-inline]:inline-block');
    expect(classes).toContain('[&_.maho-math-block]:overflow-x-auto');
  });

  it('targets the exact class the renderer emits for each math layout', () => {
    renderMarkdown('$$\n\\frac{a}{b}\n$$');

    const displayToken = Array.from(mathElement().classList)
                             .find(token => token.startsWith('maho-math-'));
    expect(displayToken).toBe('maho-math-block');
    expect(requireElement(container.querySelector('div'), 'markdown wrapper').className)
        .toContain(`[&_.${displayToken}]`);

    renderMarkdown('$x$');

    const inlineToken = Array.from(mathElement().classList)
                            .find(token => token.startsWith('maho-math-'));
    expect(inlineToken).toBe('maho-math-inline');
    expect(requireElement(container.querySelector('div'), 'markdown wrapper').className)
        .toContain(`[&_.${inlineToken}]`);
  });

  it('copies the raw equation source, not the rendered MathML', async () => {
    const raw = 'Energy:\n\n$E = mc^2$\n\nDone.';
    const writeText = vi.fn<(text: string) => Promise<void>>()
                          .mockResolvedValue(undefined);
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: {writeText},
    });

    const item: ConversationItem = {
      key: 'assistant-math',
      markdown: true,
      role: 'assistant',
      text: raw,
      timestamp: 1,
    };
    act(() => root.render(<ConversationMessage item={item} />));
    expect(container.querySelector('math')).not.toBeNull();

    await act(async () => {
      requireElement(
          container.querySelector<HTMLButtonElement>('button[aria-label="Copy response"]'),
          'copy button')
          .dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(writeText).toHaveBeenCalledWith(raw);
  });
});
