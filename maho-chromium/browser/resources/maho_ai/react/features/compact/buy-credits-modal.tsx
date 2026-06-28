import React, {useState} from 'react';
import type {MahoAiStore} from '../../../store.js';
import {cn} from '../../lib/cn.js';

interface PackConfig {
  packSizeUsd: 10|50|100;
  label: string;
  badge?: string;
}

const PACKS: PackConfig[] = [
  {packSizeUsd: 10, label: '$10'},
  {packSizeUsd: 50, label: '$50', badge: 'BEST VALUE'},
  {packSizeUsd: 100, label: '$100'},
];

interface Props {
  isOpen: boolean;
  store: MahoAiStore;
  onClose: () => void;
}

export function BuyCreditsModal({isOpen, store, onClose}: Props) {
  const [loadingPack, setLoadingPack] = useState<number|null>(null);

  if (!isOpen) {
    return null;
  }

  const handleBuy = (packSizeUsd: 10|50|100) => {
    if (loadingPack !== null) return;
    setLoadingPack(packSizeUsd);
    store.getBuyCreditsUrl(packSizeUsd)
      .then(url => {
        if (url) {
          window.open(url, '_blank', 'noopener,noreferrer');
        }
      })
      .catch(() => {})
      .finally(() => {
        setLoadingPack(null);
      });
  };

  return (
    <div className="tab-tidy-modal-overlay">
      <div
        className="tab-tidy-modal buy-credits-modal"
        role="dialog"
        aria-modal="true"
        aria-label="Buy PAYG credits"
        onKeyDown={e => { if (e.key === 'Escape') onClose(); }}
      >
        <div className="tab-tidy-modal__header">
          <span className="tab-tidy-modal__title">Buy PAYG credits</span>
          <button
            type="button"
            className="compact-icon-button"
            onClick={onClose}
            aria-label="Close"
          >
            x
          </button>
        </div>

        <div className="buy-credits-modal__packs">
          {PACKS.map(({packSizeUsd, label, badge}) => (
            <div
              key={packSizeUsd}
              className={cn(
                  'buy-credits-modal__pack',
                  badge && 'buy-credits-modal__pack--featured',
              )}
            >
              {badge && (
                <span className="buy-credits-modal__pack-badge">{badge}</span>
              )}
              <span className="buy-credits-modal__pack-amount">{label}</span>
              <button
                type="button"
                className="console-button console-button--primary"
                disabled={loadingPack !== null}
                onClick={() => handleBuy(packSizeUsd)}
              >
                {loadingPack === packSizeUsd ? '...' : 'Buy'}
              </button>
            </div>
          ))}
        </div>

        <p className="buy-credits-modal__notice">
          Credits never expire and stack with your subscription. Refundable within 14 days from unconsumed balance only.
        </p>
      </div>
    </div>
  );
}
