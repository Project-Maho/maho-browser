import { useState, useRef, useEffect, useCallback, useMemo } from 'react';
import { useFocusTrap } from '../../hooks/useFocusTrap';
import { calculateScore, groupByCategory } from '../../hooks/useCommandRegistry';
import type { PaletteCommand, CommandCategory } from '../../hooks/useCommandRegistry';

interface CommandPaletteProps {
  open: boolean;
  onClose: () => void;
  commands: PaletteCommand[];
}

const CATEGORY_LABELS: Record<CommandCategory, string> = {
  mail: 'Mail',
  compose: 'Compose',
  navigate: 'Navigate',
  search: 'Search',
  view: 'View',
  settings: 'Settings',
  ai: 'AI',
};

export function CommandPalette({ open, onClose, commands }: CommandPaletteProps) {
  const [query, setQuery] = useState('');
  const [activeIndex, setActiveIndex] = useState(0);
  const inputRef = useRef<HTMLInputElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const listRef = useRef<HTMLDivElement>(null);

  useFocusTrap(containerRef, open);

  const filteredCommands = useMemo(() => {
    const trimmedQuery = query.trim().toLowerCase();

    if (trimmedQuery.length === 0) {
      const enabledCommands = commands.filter(
        (cmd) => cmd.enabled === undefined || cmd.enabled()
      );
      return groupByCategory(enabledCommands);
    }

    const terms = trimmedQuery.split(/\s+/).filter((t) => t.length > 0);
    const scored: Array<{ command: PaletteCommand; score: number }> = [];

    for (const command of commands) {
      if (command.enabled !== undefined && !command.enabled()) {
        continue;
      }

      const score = calculateScore(command, terms);
      if (score > 0) {
        scored.push({ command, score });
      }
    }

    scored.sort((a, b) => b.score - a.score);
    return scored.slice(0, 15).map((s) => s.command);
  }, [query, commands]);

  useEffect(() => {
    setActiveIndex(0);
  }, [query]);

  useEffect(() => {
    if (open) {
      setQuery('');
      setActiveIndex(0);
      requestAnimationFrame(() => {
        inputRef.current?.focus();
      });
    }
  }, [open]);

  useEffect(() => {
    const activeElement = listRef.current?.children[activeIndex] as HTMLElement | undefined;
    if (activeElement) {
      activeElement.scrollIntoView({ block: 'nearest' });
    }
  }, [activeIndex]);

  const handleKeyDown = useCallback(
    (e: React.KeyboardEvent) => {
      if (e.key === 'Escape') {
        e.preventDefault();
        onClose();
        return;
      }

      if (e.key === 'ArrowDown') {
        e.preventDefault();
        setActiveIndex((prev) => {
          return prev < filteredCommands.length - 1 ? prev + 1 : prev;
        });
        return;
      }

      if (e.key === 'ArrowUp') {
        e.preventDefault();
        setActiveIndex((prev) => {
          return prev > 0 ? prev - 1 : 0;
        });
        return;
      }

      if (e.key === 'Enter') {
        e.preventDefault();
        const command = filteredCommands[activeIndex];
        if (command) {
          command.handler();
          onClose();
        }
        return;
      }
    },
    [filteredCommands, activeIndex, onClose]
  );

  const handleBackdropClick = useCallback(
    (e: React.MouseEvent) => {
      if (e.target === e.currentTarget) {
        onClose();
      }
    },
    [onClose]
  );

  const handleCommandClick = useCallback(
    (command: PaletteCommand) => {
      command.handler();
      onClose();
    },
    [onClose]
  );

  if (!open) return null;

  let lastCategory: CommandCategory | null = null;

  return (
    <div
      className="fixed inset-0 z-50 flex items-start justify-center bg-black/50 pt-[20vh] backdrop-blur-sm"
      onClick={handleBackdropClick}
      onKeyDown={handleKeyDown}
      role="dialog"
      aria-modal="true"
      aria-label="Command palette"
    >
      <div
        ref={containerRef}
        className="w-full max-w-lg overflow-hidden rounded-xl border border-border bg-card shadow-2xl"
      >
        <div className="border-b border-border">
          <input
            ref={inputRef}
            type="text"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            placeholder="Type a command..."
            className="w-full bg-transparent px-4 py-3 text-sm text-foreground placeholder:text-muted-foreground focus:outline-none"
            aria-label="Search commands"
          />
        </div>

        <div
          ref={listRef}
          className="max-h-80 overflow-y-auto py-2"
          role="listbox"
          aria-label="Command results"
        >
          {filteredCommands.length === 0 ? (
            <div className="py-8 text-center text-sm text-muted-foreground">
              No commands found
            </div>
          ) : (
            filteredCommands.map((command, index) => {
              const showCategoryHeader =
                query.trim().length === 0 && command.category !== lastCategory;
              if (showCategoryHeader) {
                lastCategory = command.category;
              }

              return (
                <div key={command.id} role="group" aria-labelledby={showCategoryHeader ? `category-${command.category}` : undefined}>
                  {showCategoryHeader && (
                    <div
                      id={`category-${command.category}`}
                      className="px-3 py-1 text-[11px] uppercase tracking-wider text-muted-foreground"
                    >
                      {CATEGORY_LABELS[command.category]}
                    </div>
                  )}
                  <button
                    type="button"
                    onClick={() => handleCommandClick(command)}
                    className={`mx-1 flex w-[calc(100%-8px)] items-center justify-between rounded-md px-3 py-2 text-left transition-colors ${
                      index === activeIndex
                        ? 'bg-muted'
                        : 'hover:bg-surface-hover'
                    }`}
                    role="option"
                    aria-selected={index === activeIndex}
                  >
                    <span className="text-sm text-foreground">{command.label}</span>
                    {command.shortcut && (
                      <span className="rounded bg-muted px-1.5 py-0.5 text-xs text-muted-foreground">
                        {command.shortcut}
                      </span>
                    )}
                  </button>
                </div>
              );
            })
          )}
        </div>
      </div>
    </div>
  );
}
