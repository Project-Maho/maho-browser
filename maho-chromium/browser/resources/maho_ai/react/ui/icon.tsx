import type {CSSProperties} from 'react';

import {getIconUrl} from '../../views/icons.js';
import type {IconSymbol} from '../../views/icons.js';

export type {IconSymbol} from '../../views/icons.js';

export function Icon({symbol}: {symbol: IconSymbol}) {
  const style: CSSProperties & {'--maho-ai-icon-url': string} = {
    '--maho-ai-icon-url': `url("${getIconUrl(symbol)}")`,
  };

  return (
    <span
      aria-hidden="true"
      className="maho-ai-icon"
      data-symbol={symbol}
      style={style}
    />
  );
}
