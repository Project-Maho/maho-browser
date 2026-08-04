import mahoLogoUrl from '../assets/maho-logo.png';
import mahoStarUrl from '../assets/maho-star.png';
import { clsx } from '../utils/clsx';

interface MahoLogoProps {
  readonly size?: number;
  readonly class?: string;
  readonly variant?: 'app' | 'star';
}

export function MahoLogo({ size = 56, class: className, variant = 'app' }: MahoLogoProps) {
  return (
    <img
      src={variant === 'star' ? mahoStarUrl : mahoLogoUrl}
      width={size}
      height={size}
      class={clsx('inline-block shrink-0', className)}
      alt=""
      aria-hidden="true"
      data-testid="maho-logo"
    />
  );
}
