import {sanitizeInnerHtml} from 'chrome://resources/js/parse_html_subset.js';

function escapeHtml(text: string): string {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

const kMarkdownTags = [
  'a',
  'blockquote',
  'br',
  'code',
  'em',
  'h2',
  'h3',
  'h4',
  'li',
  'ol',
  'p',
  'pre',
  'strong',
  'ul',
];

function renderInline(text: string): string {
  let html = escapeHtml(text);
  const inlineCodeSegments: string[] = [];
  html = html.replace(/`([^`]+)`/g, (_match, code) => {
    const token = `__MAHO_INLINE_CODE_${inlineCodeSegments.length}__`;
    inlineCodeSegments.push(`<code>${code}</code>`);
    return token;
  });
  html = html.replace(/\[([^\]]+)\]\((https:\/\/[^)\s]+)\)/g,
      '<a href="$2" target="_blank" rel="noopener noreferrer">$1</a>');
  html = html.replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>');
  html = html.replace(/\*([^*]+)\*/g, '<em>$1</em>');

  for (const [index, segment] of inlineCodeSegments.entries()) {
    html = html.replace(`__MAHO_INLINE_CODE_${index}__`, () => segment);
  }

  return html;
}

function renderParagraph(lines: string[]): string {
  return `<p>${renderInline(lines.join('\n')).replace(/\n/g, '<br>')}</p>`;
}

function renderList(items: string[], ordered: boolean): string {
  const tagName = ordered ? 'ol' : 'ul';
  return `<${tagName}>${items.map((item) =>
    `<li>${renderInline(item).replace(/\n/g, '<br>')}</li>`).join('')}</${tagName}>`;
}

function renderBlockquote(lines: string[]): string {
  const content = lines.join('\n').trim();
  if (!content) {
    return '<blockquote></blockquote>';
  }

  return `<blockquote>${content.split(/\n{2,}/).map((paragraph) =>
    `<p>${renderInline(paragraph.trim()).replace(/\n/g, '<br>')}</p>`).join('')}</blockquote>`;
}

function renderBlocks(raw: string): string {
  const normalized = raw.replace(/\r\n?/g, '\n').trim();
  if (!normalized) {
    return '<p></p>';
  }

  const blocks: string[] = [];
  const lines = normalized.split('\n');
  let paragraphLines: string[] = [];
  let quoteLines: string[] = [];
  let listItems: string[] = [];
  let listType: 'ordered'|'unordered'|null = null;

  const flushParagraph = () => {
    if (!paragraphLines.length) {
      return;
    }
    blocks.push(renderParagraph(paragraphLines));
    paragraphLines = [];
  };

  const flushQuote = () => {
    if (!quoteLines.length) {
      return;
    }
    blocks.push(renderBlockquote(quoteLines));
    quoteLines = [];
  };

  const flushList = () => {
    if (!listItems.length || !listType) {
      return;
    }
    blocks.push(renderList(listItems, listType === 'ordered'));
    listItems = [];
    listType = null;
  };

  for (let index = 0; index < lines.length; index++) {
    const line = lines[index]!;
    const trimmed = line.trim();

    if (!trimmed) {
      flushParagraph();
      flushQuote();
      flushList();
      continue;
    }

    if (trimmed.startsWith('```')) {
      flushParagraph();
      flushQuote();
      flushList();

      const codeLines: string[] = [];
      while (++index < lines.length && !lines[index]!.trim().startsWith('```')) {
        codeLines.push(lines[index]!);
      }

      blocks.push(`<pre><code>${escapeHtml(codeLines.join('\n'))}</code></pre>`);
      continue;
    }

    const headingMatch = trimmed.match(/^(#{1,3})\s+(.+)$/);
    if (headingMatch) {
      flushParagraph();
      flushQuote();
      flushList();

      const headingLevel = Math.min(headingMatch[1]!.length + 1, 4);
      blocks.push(`<h${headingLevel}>${renderInline(headingMatch[2]!)}</h${headingLevel}>`);
      continue;
    }

    const quoteMatch = line.match(/^\s*>\s?(.*)$/);
    if (quoteMatch) {
      flushParagraph();
      flushList();
      quoteLines.push(quoteMatch[1]!);
      continue;
    }

    const unorderedMatch = line.match(/^\s*[-*]\s+(.+)$/);
    if (unorderedMatch) {
      flushParagraph();
      flushQuote();
      if (listType && listType !== 'unordered') {
        flushList();
      }
      listType = 'unordered';
      listItems.push(unorderedMatch[1]!);
      continue;
    }

    const orderedMatch = line.match(/^\s*\d+\.\s+(.+)$/);
    if (orderedMatch) {
      flushParagraph();
      flushQuote();
      if (listType && listType !== 'ordered') {
        flushList();
      }
      listType = 'ordered';
      listItems.push(orderedMatch[1]!);
      continue;
    }

    paragraphLines.push(line);
  }

  flushParagraph();
  flushQuote();
  flushList();
  return blocks.join('');
}

const kMarkdownAttrs = ['rel', 'href', 'target'];

export function renderTrustedMarkdown(raw: string): TrustedHTML {
  const safeRaw = (raw ?? '').toString();
  try {
    return sanitizeInnerHtml(renderBlocks(safeRaw), {
      tags: kMarkdownTags,
      attrs: kMarkdownAttrs,
    });
  } catch (primaryError) {
    console.warn('[Maho AI] renderTrustedMarkdown: primary sanitize failed:', primaryError, {rawLength: safeRaw.length, sample: safeRaw.slice(0, 200)});
    const fallback = `<p>⚠️ 렌더 실패: 안전하지 않은 마크업이 포함되었습니다.</p>` +
        `<pre>${escapeHtml(safeRaw)}</pre>`;
    try {
      return sanitizeInnerHtml(fallback, {
        tags: ['p', 'pre'],
        attrs: [],
      });
    } catch (secondaryError) {
      console.error('[Maho AI] renderTrustedMarkdown: fallback sanitize also failed:', secondaryError);
      return '[Maho AI] 렌더링 오류가 발생했습니다.' as unknown as TrustedHTML;
    }
  }
}

export function renderMarkdownInto(container: HTMLElement, raw: string): void {
  container.innerHTML = renderTrustedMarkdown(raw) as unknown as string;
}
