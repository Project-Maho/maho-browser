import * as React from 'react';
import {Marked} from 'marked';
import {cn} from '@lib/utils';
import {renderTexToMathML} from '../lib/tex-math.js';

function escapeHtml(input: string): string {
  return input
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
}

interface MathToken {
  type: string;
  raw: string;
  text: string;
  display: boolean;
}

interface MathMatch {
  raw: string;
  body: string;
  display: boolean;
}

/**
 * Equations stream in with the rest of the assistant text, so every matcher
 * below refuses to match without a closing delimiter: a half-written `$x^`
 * stays literal prose until the expression completes.
 */
function findCloser(source: string, start: number, closer: string): number {
  for (let index = start; index < source.length; index++) {
    if (source.startsWith(closer, index)) {
      return index;
    }
    if (source[index] === '\\' && closer[0] !== '\\') {
      index++;
    }
  }
  return -1;
}

function matchDollarMath(source: string): MathMatch|null {
  if (!source.startsWith('$')) {
    return null;
  }
  const display = source.startsWith('$$');
  const opener = display ? '$$' : '$';
  if (!display) {
    const next = source[opener.length];
    if (next === undefined || /\s/.test(next)) {
      return null;
    }
  }
  let cursor = opener.length;
  while (cursor < source.length) {
    const found = findCloser(source, cursor, opener);
    if (found < 0) {
      return null;
    }
    const before = source[found - 1];
    const after = source[found + opener.length];
    // Currency guard: an inline closer may neither follow whitespace nor
    // precede a digit, so "$5 and $10" stays prose.
    const closed = display ||
        (before !== undefined && !/\s/.test(before) &&
         !(after !== undefined && /[0-9]/.test(after)));
    if (closed) {
      const body = source.slice(opener.length, found);
      return body.trim().length === 0 ?
          null :
          {raw: source.slice(0, found + opener.length), body, display};
    }
    cursor = found + opener.length;
  }
  return null;
}

const BRACKET_DELIMITERS:
    Readonly<Record<string, {closer: string; display: boolean}>> = {
      '\\(': {closer: '\\)', display: false},
      '\\[': {closer: '\\]', display: true},
    };

function matchBracketedMath(source: string): MathMatch|null {
  for (const opener of Object.keys(BRACKET_DELIMITERS)) {
    if (!source.startsWith(opener)) {
      continue;
    }
    const {closer, display} = BRACKET_DELIMITERS[opener] as
        {closer: string; display: boolean};
    const found = findCloser(source, opener.length, closer);
    if (found < 0) {
      return null;
    }
    const body = source.slice(opener.length, found);
    return body.trim().length === 0 ?
        null :
        {
          raw: source.slice(0, found + closer.length),
          body,
          display,
        };
  }
  return null;
}

function renderMathToken(token: MathToken, block: boolean): string {
  const outcome = renderTexToMathML(token.text, token.display);
  if (outcome.ok) {
    return outcome.mathml;
  }
  // Unparseable or over-limit math keeps the source the assistant sent, as
  // escaped text, so the reader sees the expression instead of nothing.
  const tag = block ? 'div' : 'span';
  return `<${tag} class="maho-math maho-math-fallback">` +
      `${escapeHtml(token.raw)}</${tag}>`;
}

function firstMathDelimiter(source: string): number {
  const candidates =
      [source.indexOf('$'), source.indexOf('\\('), source.indexOf('\\[')];
  const found = candidates.filter(index => index >= 0);
  return found.length === 0 ? -1 : Math.min(...found);
}

const mathExtensions = [
  {
    name: 'mahoInlineMath',
    level: 'inline' as const,
    start: firstMathDelimiter,
    tokenizer(source: string): MathToken|undefined {
      const match = matchDollarMath(source) ?? matchBracketedMath(source);
      return match === null ? undefined : {
        type: 'mahoInlineMath',
        raw: match.raw,
        text: match.body,
        display: match.display,
      };
    },
    renderer(token: MathToken): string {
      return renderMathToken(token, false);
    },
  },
  {
    name: 'mahoBlockMath',
    level: 'block' as const,
    tokenizer(source: string): MathToken|undefined {
      const match = matchDollarMath(source) ?? matchBracketedMath(source);
      if (match === null || !match.display) {
        return undefined;
      }
      const trailing = source[match.raw.length];
      // Only a whole line counts as a display block; otherwise the paragraph
      // tokenizer keeps the line together and the inline extension renders it.
      if (trailing !== undefined && trailing !== '\n') {
        return undefined;
      }
      return {
        type: 'mahoBlockMath',
        raw: match.raw,
        text: match.body,
        display: match.display,
      };
    },
    renderer(token: MathToken): string {
      return renderMathToken(token, true);
    },
  },
];

const markdownParser = new Marked({
  gfm: true,
  breaks: true,
  extensions: mathExtensions,
  renderer: {
    html({raw}: {raw: string}) {
      return escapeHtml(raw);
    },
  } as never,
});

/**
 * Math layout travels with the renderer rather than with each caller: an
 * inline equation must not overlap the line it sits on, and a display
 * equation needs its own block and a scroll container when it is wide.
 */
const MATH_LAYOUT_CLASS_NAME = cn(
    '[&_.maho-math-inline]:inline-block [&_.maho-math-inline]:align-middle',
    '[&_.maho-math-display]:my-2 [&_.maho-math-display]:overflow-x-auto',
    '[&_.maho-math-fallback]:[overflow-wrap:anywhere]');

export const Markdown = React.memo(function Markdown({className, text}: {className?: string; text: string}) {
  const safeText = text ?? '';
  const html = React.useMemo(() => {
    try {
      const result = markdownParser.parse(safeText, {async: false});
      return typeof result === 'string' ? result : '';
    } catch {
      return escapeHtml(safeText);
    }
  }, [safeText]);

  return (
      <div
          className={cn(MATH_LAYOUT_CLASS_NAME, className)}
          dangerouslySetInnerHTML={{__html: html}} />
  );
});
