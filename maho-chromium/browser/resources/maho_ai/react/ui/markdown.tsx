import {useMemo} from 'react';

import {renderTrustedMarkdown} from '../../views/cards.js';

export function Markdown({className, text}: {className?: string; text: string}) {
  const html = useMemo(() => renderTrustedMarkdown(text), [text]);
  return <div className={className} dangerouslySetInnerHTML={{__html: html}} />;
}
