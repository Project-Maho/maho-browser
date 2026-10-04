import type { ComponentChildren } from 'preact';

interface MarkdownRendererProps {
  text: string;
}

export function MarkdownRenderer({ text }: MarkdownRendererProps) {
  const lines = text.replace(/\r\n/g, '\n').split('\n');
  const blocks: ComponentChildren[] = [];

  for (let index = 0; index < lines.length; ) {
    const line = lines[index] ?? '';
    if (!line.trim()) {
      index += 1;
      continue;
    }

    if (line.startsWith('```')) {
      const result = collectCodeBlock(lines, index + 1);
      blocks.push(
        <pre key={`code-${blocks.length}`} class="agent-md-pre">
          <code>{result.code}</code>
        </pre>,
      );
      index = result.nextIndex;
      continue;
    }

    if (line.startsWith('## ')) {
      blocks.push(<h2 key={`h2-${blocks.length}`}>{renderInline(line.slice(3), `h2-${blocks.length}`)}</h2>);
      index += 1;
      continue;
    }

    if (line.startsWith('### ')) {
      blocks.push(<h3 key={`h3-${blocks.length}`}>{renderInline(line.slice(4), `h3-${blocks.length}`)}</h3>);
      index += 1;
      continue;
    }

    if (line.startsWith('#### ')) {
      blocks.push(<h4 key={`h4-${blocks.length}`}>{renderInline(line.slice(5), `h4-${blocks.length}`)}</h4>);
      index += 1;
      continue;
    }

    if (line.startsWith('- ')) {
      const result = collectList(lines, index, '- ');
      blocks.push(
        <ul key={`ul-${blocks.length}`}>
          {result.items.map((item) => (
            <li key={item}>{renderInline(item, `ul-${blocks.length}-${item}`)}</li>
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
          {result.items.map((item) => (
            <li key={item}>{renderInline(item, `ol-${blocks.length}-${item}`)}</li>
          ))}
        </ol>,
      );
      index = result.nextIndex;
      continue;
    }

    if (line.startsWith('> ')) {
      blocks.push(
        <blockquote key={`quote-${blocks.length}`}>
          {renderInline(line.slice(2), `quote-${blocks.length}`)}
        </blockquote>,
      );
      index += 1;
      continue;
    }

    const result = collectParagraph(lines, index);
    blocks.push(<p key={`p-${blocks.length}`}>{renderInline(result.text, `p-${blocks.length}`)}</p>);
    index = result.nextIndex;
  }

  return <div class="agent-md">{blocks}</div>;
}

function collectCodeBlock(lines: readonly string[], startIndex: number) {
  const codeLines: string[] = [];
  let index = startIndex;
  while (index < lines.length && !(lines[index] ?? '').startsWith('```')) {
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
  return Boolean(line.trim()) && !line.startsWith('```') && !line.startsWith('- ') && !/^\d+\.\s/.test(line);
}

function renderInline(text: string, keyPrefix: string): ComponentChildren[] {
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

    const parsed = remaining.startsWith('`') ? parseCode(remaining) : parseLink(remaining);
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

function parseLink(text: string) {
  const match = /^\[([^\]]+)]\((https?:\/\/[^\s)]+)\)/.exec(text);
  if (match === null) return null;
  const label = match[1] ?? '';
  const href = match[2] ?? '';
  return {
    length: match[0].length,
    node: (prefix: string, index: number) => (
      <a key={`${prefix}-link-${index}`} href={href} rel="noreferrer" target="_blank">
        {label}
      </a>
    ),
  };
}
