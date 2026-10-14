import type { ComponentChildren } from 'preact';
import { useMemo } from 'preact/hooks';

interface MarkdownRendererProps {
  text: string;
  /**
   * Tool output is untrusted page/tool content: links render as plain text so a
   * misleading label can never hide its destination behind a click target.
   */
  plainLinks?: boolean;
}

/**
 * Re-parses only when the text (or link mode) changes: a streamed token for a
 * different block reuses this block's cached vnode. Deliberately not
 * `preact/compat` memo — importing compat rewires onBlur/onFocus globally.
 */
export function MarkdownRenderer({ text, plainLinks = false }: MarkdownRendererProps) {
  return useMemo(() => renderMarkdown(text, plainLinks), [text, plainLinks]);
}

function renderMarkdown(text: string, plainLinks: boolean) {
  const lines = text.replace(/\r\n/g, '\n').split('\n');
  const blocks: ComponentChildren[] = [];

  for (let index = 0; index < lines.length; ) {
    const line = lines[index] ?? '';
    if (!line.trim()) {
      index += 1;
      continue;
    }

    const fence = openingFence(line);
    if (fence !== null) {
      const result = collectCodeBlock(lines, index + 1, fence);
      blocks.push(
        <pre key={`code-${blocks.length}`} class="agent-md-pre">
          <code>{result.code}</code>
        </pre>,
      );
      index = result.nextIndex;
      continue;
    }

    if (line.startsWith('## ')) {
      blocks.push(<h2 key={`h2-${blocks.length}`}>{renderInline(line.slice(3), `h2-${blocks.length}`, plainLinks)}</h2>);
      index += 1;
      continue;
    }

    if (line.startsWith('### ')) {
      blocks.push(<h3 key={`h3-${blocks.length}`}>{renderInline(line.slice(4), `h3-${blocks.length}`, plainLinks)}</h3>);
      index += 1;
      continue;
    }

    if (line.startsWith('#### ')) {
      blocks.push(<h4 key={`h4-${blocks.length}`}>{renderInline(line.slice(5), `h4-${blocks.length}`, plainLinks)}</h4>);
      index += 1;
      continue;
    }

    if (line.startsWith('- ')) {
      const result = collectList(lines, index, '- ');
      blocks.push(
        <ul key={`ul-${blocks.length}`}>
          {result.items.map((item, itemIndex) => (
            <li key={`item-${itemIndex}`}>{renderInline(item, `ul-${blocks.length}-${itemIndex}`, plainLinks)}</li>
          ))}
        </ul>,
      );
      index = result.nextIndex;
      continue;
    }

    if (/^\d+\.\s/.test(line)) {
      const result = collectOrderedList(lines, index);
      blocks.push(
        <ol key={`ol-${blocks.length}`}>
          {result.items.map((item, itemIndex) => (
            <li key={`item-${itemIndex}`}>{renderInline(item, `ol-${blocks.length}-${itemIndex}`, plainLinks)}</li>
          ))}
        </ol>,
      );
      index = result.nextIndex;
      continue;
    }

    if (line.startsWith('> ')) {
      blocks.push(
        <blockquote key={`quote-${blocks.length}`}>
          {renderInline(line.slice(2), `quote-${blocks.length}`, plainLinks)}
        </blockquote>,
      );
      index += 1;
      continue;
    }

    const result = collectParagraph(lines, index);
    blocks.push(<p key={`p-${blocks.length}`}>{renderInline(result.text, `p-${blocks.length}`, plainLinks)}</p>);
    index = result.nextIndex;
  }

  return <div class="agent-md">{blocks}</div>;
}

/** Opening fence: 3+ backticks; returns the fence marker so only a matching close ends the block. */
function openingFence(line: string): string | null {
  const match = /^(`{3,})/.exec(line);
  return match === null ? null : (match[1] ?? null);
}

/** A closing fence is a line of only backticks (>= opening length), optionally padded. */
function isClosingFence(line: string, fence: string): boolean {
  const trimmed = line.trim();
  return trimmed.length >= fence.length && /^`+$/.test(trimmed);
}

function collectCodeBlock(lines: readonly string[], startIndex: number, fence: string) {
  const codeLines: string[] = [];
  let index = startIndex;
  while (index < lines.length && !isClosingFence(lines[index] ?? '', fence)) {
    codeLines.push(lines[index] ?? '');
    index += 1;
  }
  return { code: codeLines.join('\n'), nextIndex: index < lines.length ? index + 1 : index };
}

function collectList(lines: readonly string[], startIndex: number, marker: string) {
  const items: string[] = [];
  let index = startIndex;
  while (index < lines.length && (lines[index] ?? '').startsWith(marker)) {
    items.push((lines[index] ?? '').slice(marker.length));
    index += 1;
  }
  return { items, nextIndex: index };
}

function collectOrderedList(lines: readonly string[], startIndex: number) {
  const items: string[] = [];
  let index = startIndex;
  while (index < lines.length && /^\d+\.\s/.test(lines[index] ?? '')) {
    items.push((lines[index] ?? '').replace(/^\d+\.\s/, ''));
    index += 1;
  }
  return { items, nextIndex: index };
}

function collectParagraph(lines: readonly string[], startIndex: number) {
  const parts: string[] = [];
  let index = startIndex;
  while (index < lines.length && isParagraphLine(lines[index] ?? '')) {
    parts.push(lines[index] ?? '');
    index += 1;
  }
  return { nextIndex: index, text: parts.join(' ') };
}

function isParagraphLine(line: string): boolean {
  return (
    Boolean(line.trim())
    && openingFence(line) === null
    && !line.startsWith('- ')
    && !/^\d+\.\s/.test(line)
    && !/^#{2,4} /.test(line)
    && !line.startsWith('> ')
  );
}

function renderInline(text: string, keyPrefix: string, plainLinks: boolean): ComponentChildren[] {
  const parts: ComponentChildren[] = [];
  let remaining = text;
  let index = 0;

  while (remaining.length > 0) {
    const codeStart = remaining.indexOf('`');
    const linkStart = remaining.indexOf('[');
    const nextStart = firstNonNegative(codeStart, linkStart);

    if (nextStart === -1) {
      parts.push(remaining);
      break;
    }

    if (nextStart > 0) {
      parts.push(remaining.slice(0, nextStart));
      remaining = remaining.slice(nextStart);
    }

    const parsed = remaining.startsWith('`') ? parseCode(remaining) : parseLink(remaining, plainLinks);
    if (parsed === null) {
      parts.push(remaining.slice(0, 1));
      remaining = remaining.slice(1);
      continue;
    }

    parts.push(parsed.node(keyPrefix, index));
    remaining = remaining.slice(parsed.length);
    index += 1;
  }

  return parts;
}

function firstNonNegative(left: number, right: number): number {
  if (left === -1) return right;
  if (right === -1) return left;
  return Math.min(left, right);
}

function parseCode(text: string) {
  const end = text.indexOf('`', 1);
  if (end === -1) return null;
  const code = text.slice(1, end);
  return { length: end + 1, node: (prefix: string, index: number) => <code key={`${prefix}-code-${index}`}>{code}</code> };
}

function parseLink(text: string, plainLinks: boolean) {
  const match = /^\[([^\]]+)]\((https?:\/\/[^\s)]+)\)/.exec(text);
  if (match === null) return null;
  const label = match[1] ?? '';
  const href = match[2] ?? '';
  if (plainLinks) {
    // Untrusted content: show label and destination as inert text.
    return { length: match[0].length, node: () => `${label} (${href})` };
  }
  const host = hostOf(href);
  // When the label does not already name the destination, show the real host
  // beside it so a deceptive label cannot masquerade as another site.
  const showHost = host !== null && label.trim().toLowerCase() !== host;
  return {
    length: match[0].length,
    node: (prefix: string, index: number) => (
      <span key={`${prefix}-link-${index}`}>
        <a href={href} rel="noreferrer noopener" target="_blank">
          {label}
        </a>
        {showHost && <span class="agent-md-link-host" data-testid="agent-md-link-host"> ({host})</span>}
      </span>
    ),
  };
}

function hostOf(href: string): string | null {
  try {
    return new URL(href).host.toLowerCase();
  } catch {
    return null;
  }
}
