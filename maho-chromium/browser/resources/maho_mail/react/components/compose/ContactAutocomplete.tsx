import { useCallback, useEffect, useId, useRef, useState } from "react";
import * as api from "../../api";
import type { Contact, ContactGroup } from "../../types";
import { Users, X } from "lucide-react";
import { parseRecipientsFn } from "../../hooks/useComposeRecipients";

const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
function isValidEmail(email: string): boolean {
  const match = email.match(/<(.+?)>/);
  const addr = match ? match[1] : email;
  return EMAIL_RE.test(addr.trim());
}

interface ContactAutocompleteProps {
  id?: string;
  accountId: string;
  value: string;
  onChange: (value: string) => void;
  onPendingChange?: (value: string) => void;
  placeholder?: string;
  label?: string;
  inline?: boolean;
  inputClassName?: string;
  ariaLabel?: string;
  autoFocus?: boolean;
}

type SuggestionItem =
  | { type: 'group'; data: ContactGroup }
  | { type: 'contact'; data: Contact };

export function ContactAutocomplete({
  id,
  accountId,
  value,
  onChange,
  onPendingChange,
  placeholder = "recipient@example.com",
  label,
  inline = false,
  inputClassName,
  ariaLabel,
  autoFocus,
}: ContactAutocompleteProps) {
  const [inputValue, setInputValue] = useState("");
  const [suggestions, setSuggestions] = useState<SuggestionItem[]>([]);
  const [showSuggestions, setShowSuggestions] = useState(false);
  const [activeIndex, setActiveIndex] = useState(-1);
  const inputRef = useRef<HTMLInputElement>(null);
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const blurTimeoutRef = useRef<any>(null);
  const isComposingRef = useRef(false);
  const listboxId = useId();

  useEffect(() => {
    return () => {
      if (blurTimeoutRef.current) {
        clearTimeout(blurTimeoutRef.current);
      }
    };
  }, []);

  useEffect(() => { onPendingChange?.(inputValue); }, [inputValue, onPendingChange]);

  // Parse comma-separated value into unique array of chips
  const chips = parseRecipientsFn(value);

  const search = useCallback(
    async (query: string) => {
      if (query.length < 2 || !accountId) {
        setSuggestions([]);
        return;
      }
      try {
        const [contactResults, groupResults] = await Promise.all([
          api.searchContacts(accountId, query, 5),
          api.searchContactGroups(accountId, query),
        ]);
        const items: SuggestionItem[] = [
          ...groupResults.map((g): SuggestionItem => ({ type: 'group', data: g })),
          ...contactResults.map((c): SuggestionItem => ({ type: 'contact', data: c })),
        ];
        setSuggestions(items);
        setShowSuggestions(items.length > 0);
      } catch {
        setSuggestions([]);
      }
    },
    [accountId],
  );

  useEffect(() => {
    if (debounceRef.current) clearTimeout(debounceRef.current);
    if (inputValue.length >= 2) {
      debounceRef.current = setTimeout(() => search(inputValue), 200);
    } else {
      setSuggestions([]);
      setShowSuggestions(false);
    }
    return () => {
      if (debounceRef.current) clearTimeout(debounceRef.current);
    };
  }, [inputValue, search]);

  useEffect(() => {
    if (autoFocus) {
      inputRef.current?.focus();
    }
  }, [autoFocus]);

  useEffect(() => {
    return () => {
      if (debounceRef.current) {
        clearTimeout(debounceRef.current);
        debounceRef.current = null;
      }
      if (blurTimeoutRef.current) {
        clearTimeout(blurTimeoutRef.current);
        blurTimeoutRef.current = null;
      }
    };
  }, []);

  const selectSuggestion = (item: SuggestionItem) => {
    const currentChips = [...chips];

    if (item.type === 'group') {
      item.data.member_emails.forEach((email) => {
        if (!currentChips.includes(email)) {
          currentChips.push(email);
        }
      });
    } else {
      const name = item.data.name;
      const encodedName = name && /[,;"\\]/.test(name) ? `"${name.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"` : name;
      const display = encodedName ? `${encodedName} <${item.data.email}>` : item.data.email;
      if (!currentChips.includes(display)) {
        currentChips.push(display);
      }
    }

    onChange(currentChips.join(", "));
    setInputValue("");
    setShowSuggestions(false);
    setActiveIndex(-1);
    inputRef.current?.focus();
  };

  const createChip = () => {
    const trimmed = inputValue.trim();
    if (trimmed) {
      const newTokens = parseRecipientsFn(trimmed);
      const currentChips = [...chips];

      newTokens.forEach((token) => {
        if (!currentChips.includes(token)) {
          currentChips.push(token);
        }
      });

      onChange(currentChips.join(", "));
      setInputValue("");
      setShowSuggestions(false);
      setActiveIndex(-1);
    }
  };

  const removeChip = (indexToRemove: number) => {
    const newChips = chips.filter((_, idx) => idx !== indexToRemove);
    onChange(newChips.join(", "));
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (isComposingRef.current) return;

    if (e.key === "Backspace" && inputValue === "") {
      e.preventDefault();
      if (chips.length > 0) {
        removeChip(chips.length - 1);
      }
      return;
    }

    if (["Enter", "Tab", ",", ";"].includes(e.key)) {
      if (showSuggestions && suggestions.length > 0 && activeIndex >= 0) {
        e.preventDefault();
        selectSuggestion(suggestions[activeIndex]);
      } else if (inputValue.trim()) {
        e.preventDefault();
        createChip();
      }
      return;
    }

    if (!showSuggestions || suggestions.length === 0) return;

    if (e.key === "ArrowDown") {
      e.preventDefault();
      setActiveIndex((prev) => Math.min(prev + 1, suggestions.length - 1));
    } else if (e.key === "ArrowUp") {
      e.preventDefault();
      setActiveIndex((prev) => Math.max(prev - 1, 0));
    } else if (e.key === "Escape") {
      e.preventDefault();
      e.stopPropagation();
      setShowSuggestions(false);
    }
  };

  return (
    <div className="relative w-full">
      {label && (
        <label className="mb-1 block text-xs text-muted-foreground">{label}</label>
      )}
      <div
        onClick={() => inputRef.current?.focus()}
        className={
          inputClassName ??
          (inline
            ? "flex flex-wrap items-center gap-1.5 w-full bg-transparent py-1.5 min-h-[40px] focus-within:ring-0 cursor-text"
            : "flex flex-wrap items-center gap-1.5 w-full rounded-lg border border-border bg-card px-3 py-1.5 min-h-[40px] focus-within:border-ring cursor-text")
        }
      >
        {chips.map((chip, index) => {
          const isValid = isValidEmail(chip);
          const isDuplicate = chips.indexOf(chip) !== index;
          
          let chipClass = "bg-primary/10 text-primary border border-primary/20";
          if (isDuplicate) {
            chipClass = "bg-amber-500/10 text-amber-500 border border-amber-500/20";
          } else if (!isValid) {
            chipClass = "bg-destructive/10 text-destructive border border-destructive/20";
          }

          return (
            <div
              key={index}
              className={`inline-flex h-7 items-center gap-1 rounded-full pl-2.5 pr-1 text-xs font-medium border animate-scale-in ${chipClass}`}
            >
              <span>{chip}</span>
              <button
                type="button"
                onClick={(e) => {
                  e.stopPropagation();
                  removeChip(index);
                }}
                className="flex h-5 w-5 items-center justify-center rounded-full transition-colors hover:bg-foreground/10 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
                aria-label={`Remove ${chip}`}
              >
                <X size={12} />
              </button>
            </div>
          );
        })}
        <input
          id={id}
          ref={inputRef}
          type="text"
          autoFocus={autoFocus}
          value={inputValue}
          onChange={(e) => setInputValue(e.target.value)}
          onKeyDown={handleKeyDown}
          onCompositionStart={() => {
            isComposingRef.current = true;
          }}
          onCompositionEnd={() => {
            isComposingRef.current = false;
          }}
          onFocus={() => suggestions.length > 0 && setShowSuggestions(true)}
          onBlur={() => {
            createChip();
            if (blurTimeoutRef.current) clearTimeout(blurTimeoutRef.current);
            blurTimeoutRef.current = setTimeout(() => {
              setShowSuggestions(false);
            }, 150);
          }}
          placeholder={chips.length === 0 ? placeholder : ""}
          role="combobox"
          aria-expanded={showSuggestions && suggestions.length > 0}
          aria-autocomplete="list"
          aria-controls={listboxId}
          aria-activedescendant={activeIndex >= 0 ? `${listboxId}-option-${activeIndex}` : undefined}
          aria-label={ariaLabel ?? label}
          className="flex-1 min-w-[120px] bg-transparent text-sm text-foreground placeholder:text-muted-foreground focus:outline-none focus:ring-0 border-0 p-0"
        />
      </div>
      {chips.some(c => !isValidEmail(c)) && (
        <p className="mt-1 text-xs text-destructive">
          Invalid: {chips.filter(c => !isValidEmail(c)).join(", ")}
        </p>
      )}
      {showSuggestions && suggestions.length > 0 && (
        <div
          id={listboxId}
          role="listbox"
          className="absolute left-0 right-0 top-full z-50 mt-1 max-h-64 overflow-y-auto rounded-xl border border-border bg-popover p-1 shadow-xl animate-scale-in"
        >
          {suggestions.map((item, i) => (
            <button
              key={`${item.type}-${item.data.id}`}
              id={`${listboxId}-option-${i}`}
              type="button"
              role="option"
              aria-selected={i === activeIndex}
              onMouseDown={(e) => e.preventDefault()}
              onClick={() => selectSuggestion(item)}
              className={`flex min-h-10 w-full items-center gap-2 rounded-lg px-2.5 py-1.5 text-left text-sm transition-colors ${
                i === activeIndex
                  ? "bg-primary/10 text-foreground"
                  : "text-muted-foreground hover:bg-surface-hover hover:text-foreground"
              }`}
            >
              {item.type === 'group' ? (
                <>
                  <Users size={16} className="shrink-0 text-muted-foreground" aria-hidden="true" />
                  <div className="min-w-0 flex-1">
                    <p className="truncate text-sm font-medium">{item.data.name}</p>
                    <p className="truncate text-xs text-muted-foreground">
                      {item.data.member_emails.length} members
                    </p>
                  </div>
                </>
              ) : (
                <>
                  <div className="min-w-0 flex-1">
                    <p className="truncate text-sm">{item.data.name ?? item.data.email}</p>
                    {item.data.name && (
                      <p className="truncate text-xs text-muted-foreground">{item.data.email}</p>
                    )}
                  </div>
                  {item.data.is_vip && (
                    <span className="shrink-0 text-[10px] text-amber-400 font-semibold">VIP</span>
                  )}
                </>
              )}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}
