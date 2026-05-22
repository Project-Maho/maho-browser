import {forwardRef} from 'react';

import type {ButtonProps} from './button.js';
import {Button} from './button.js';
import {Icon} from './icon.js';
import type {IconSymbol} from './icon.js';

interface IconButtonProps extends Omit<ButtonProps, 'children'> {
  ariaLabel: string;
  symbol: IconSymbol;
}

export const IconButton = forwardRef<HTMLButtonElement, IconButtonProps>(function IconButton(
    {ariaLabel, symbol, ...props}, ref) {
      return (
        <Button ref={ref} aria-label={ariaLabel} {...props}>
          <Icon symbol={symbol} />
        </Button>
      );
    });

IconButton.displayName = 'IconButton';
