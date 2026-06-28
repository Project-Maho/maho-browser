import React, {useEffect, useRef} from 'react';
import type {MahoAiStore} from '../../../store.js';
import type {CreditBalanceInfo} from '../../../types.js';

const POLL_INTERVAL_MS = 60_000;

function formatBalance(balanceUsd: number): string {
  return `$${balanceUsd.toFixed(2)}`;
}

export function CreditBalanceBadge(
    {
      store,
      creditBalance,
      onOpenBuyCredits,
    }: {
      store: MahoAiStore;
      creditBalance: CreditBalanceInfo|null;
      onOpenBuyCredits: () => void;
    }) {
  const intervalRef = useRef<ReturnType<typeof setInterval>|null>(null);

  useEffect(() => {
    void store.fetchCreditBalance();

    intervalRef.current = setInterval(() => {
      void store.fetchCreditBalance();
    }, POLL_INTERVAL_MS);

    const handleFocus = () => { void store.fetchCreditBalance(); };
    window.addEventListener('focus', handleFocus);

    return () => {
      if (intervalRef.current !== null) {
        clearInterval(intervalRef.current);
      }
      window.removeEventListener('focus', handleFocus);
    };
  }, [store]);

  if (!creditBalance) {
    return null;
  }

  return (
    <button
      type="button"
      className="credit-balance-badge compact-icon-button"
      onClick={onOpenBuyCredits}
      title="Buy PAYG credits"
    >
      <span className="credit-balance-badge__label">
        Credits: {formatBalance(creditBalance.balanceUsd)}
      </span>
    </button>
  );
}
