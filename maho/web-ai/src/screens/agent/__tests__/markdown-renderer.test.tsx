import { cleanup, render } from '@testing-library/preact';
import { afterEach, describe, expect, it } from 'vitest';
import { MarkdownRenderer } from '../markdown-renderer';

afterEach(() => cleanup());

describe('MarkdownRenderer', () => {
  it('renders links in tool output as inert text with the destination visible (W5)', () => {
    const { container } = render(<MarkdownRenderer text="[Your bank](https://evil.example/login)" plainLinks />);
    expect(container.querySelector('a')).toBeNull();
    expect(container.textContent).toContain('https://evil.example/login');
  });

  it('shows the destination host beside a label that does not name it (W5)', () => {
    const { container } = render(<MarkdownRenderer text="[paypal.com](https://evil.example/x) and [evil.example](https://evil.example/y)" />);
    const hosts = container.querySelectorAll('[data-testid="agent-md-link-host"]');
    expect(hosts).toHaveLength(1);
    expect(hosts[0]).toHaveTextContent('evil.example');
    expect(container.querySelectorAll('a')).toHaveLength(2);
  });

  it('does not swallow headings and blockquotes into the preceding paragraph (W12)', () => {
    const { container } = render(<MarkdownRenderer text={'intro line\n## Title\n> quoted\nafter'} />);
    expect(container.querySelector('h2')).toHaveTextContent('Title');
    expect(container.querySelector('blockquote')).toHaveTextContent('quoted');
    expect(container.querySelectorAll('p')).toHaveLength(2);
  });

  it('only closes a fence on a backtick-only line, not on a line merely starting with backticks (W12)', () => {
    const { container } = render(<MarkdownRenderer text={'```\nline one\n```inline not a close\nline two\n```\nafter'} />);
    const pre = container.querySelector('pre');
    expect(pre).toHaveTextContent('line two');
    expect(pre).toHaveTextContent('inline not a close');
  });

  it('keeps duplicate list items as separate rows (W13)', () => {
    const { container } = render(<MarkdownRenderer text={'- same\n- same\n- same'} />);
    expect(container.querySelectorAll('li')).toHaveLength(3);
  });
});
