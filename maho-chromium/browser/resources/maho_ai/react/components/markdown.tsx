import * as React from 'react';
import {Marked} from 'marked';

function escapeHtml(input: string): string {
  return input
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
}

const markdownParser = new Marked({
  gfm: true,
  breaks: true,
  renderer: {
    html({raw}: {raw: string}) {
      return escapeHtml(raw);
    },
  } as never,
});

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
          className={className}
          dangerouslySetInnerHTML={{__html: html}} />
  );
});
