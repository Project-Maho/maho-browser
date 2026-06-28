import React, {useState} from 'react';
import {cn} from '../../lib/cn.js';

export interface TidyFolderSuggestion {
  name: string;
  tabIds: string[];
}

interface Props {
  folders: TidyFolderSuggestion[];
  onApply: (folders: TidyFolderSuggestion[]) => void;
  onDismiss: () => void;
}

export function TabTidyModal({folders, onApply, onDismiss}: Props) {
  const [checked, setChecked] = useState<Set<number>>(() => new Set(folders.map((_, i) => i)));

  function toggle(index: number) {
    setChecked(prev => {
      const next = new Set(prev);
      if (next.has(index)) {
        next.delete(index);
      } else {
        next.add(index);
      }
      return next;
    });
  }

  function handleApply() {
    onApply(folders.filter((_, i) => checked.has(i)));
  }

  return (
    <div className="tab-tidy-modal-overlay">
      <div className="tab-tidy-modal" role="dialog" aria-modal="true" aria-label="Tab Tidy Suggestions">
        <div className="tab-tidy-modal__header">
          <span className="tab-tidy-modal__title">Tab Tidy Suggestions</span>
          <button type="button" className="compact-icon-button" onClick={onDismiss} aria-label="Dismiss">
            ×
          </button>
        </div>
        <p className="tab-tidy-modal__desc">Select folders to create:</p>
        <ul className="tab-tidy-modal__list">
          {folders.map((folder, i) => (
            <li key={i} className={cn('tab-tidy-modal__folder', checked.has(i) && 'tab-tidy-modal__folder--checked')}>
              <label className="tab-tidy-modal__folder-label">
                <input
                  type="checkbox"
                  checked={checked.has(i)}
                  onChange={() => toggle(i)}
                  className="tab-tidy-modal__checkbox"
                />
                <span className="tab-tidy-modal__folder-name">{folder.name}</span>
                <span className="tab-tidy-modal__folder-count">{folder.tabIds.length} tab{folder.tabIds.length !== 1 ? 's' : ''}</span>
              </label>
            </li>
          ))}
        </ul>
        <div className="tab-tidy-modal__actions">
          <button type="button" className="console-button" onClick={onDismiss}>Cancel</button>
          <button
            type="button"
            className="console-button console-button--primary"
            onClick={handleApply}
            disabled={checked.size === 0}
          >
            Apply
          </button>
        </div>
      </div>
    </div>
  );
}
