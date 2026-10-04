import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { Clock, Search } from "lucide-react";
import { useTranslation } from "react-i18next";

const OPERATORS = [
  { prefix: "from:", description: "Filter by sender" },
  { prefix: "to:", description: "Filter by recipient" },
  { prefix: "has:", description: "Filter by property" },
  { prefix: "is:", description: "Filter by status" },
  { prefix: "before:", description: "Before a date" },
  { prefix: "after:", description: "After a date" },
  { prefix: "subject:", description: "Filter by subject" },
  { prefix: "in:", description: "Search in folder" },
] as const;

const HAS_VALUES = ["attachment"] as const;
const IS_VALUES = ["unread", "read", "starred"] as const;

interface Suggestion {
  kind: "operator" | "value" | "recent";
  text: string;
  description?: string;
  insertText: string;
}

interface SearchAutocompleteProps {
  query: string;
  onSelect: (newQuery: string) => void;
  visible: boolean;
  onDismiss: () => void;
  recentSearches?: string[];
  contacts?: string[];
  folderNames?: string[];
}

function getWordContext(query: string, cursorPos: number) {
  const before = query.slice(0, cursorPos);
  const lastSpace = before.lastIndexOf(" ");
  const currentWord = before.slice(lastSpace + 1);
  const prefix = before.slice(0, lastSpace + 1);
  return { currentWord, prefix };
}

function buildSuggestions(
  query: string,
  cursorPos: number,
  recentSearches: string[],
  contacts: string[],
  folderNames: string[],
): Suggestion[] {
  if (query.length === 0) {
    const suggestions: Suggestion[] = [];

    for (const recent of recentSearches.slice(0, 5)) {
      suggestions.push({
        kind: "recent",
        text: recent,
        insertText: recent,
      });
    }

    for (const op of OPERATORS) {
      suggestions.push({
        kind: "operator",
        text: op.prefix,
        description: op.description,
        insertText: op.prefix,
      });
    }

    return suggestions;
  }

  const { currentWord, prefix } = getWordContext(query, cursorPos);
  const lowerWord = currentWord.toLowerCase();

  const colonIdx = currentWord.indexOf(":");
  if (colonIdx !== -1) {
    const operator = currentWord.slice(0, colonIdx + 1).toLowerCase();
    const valuePart = currentWord.slice(colonIdx + 1).toLowerCase();
    const rest = query.slice(cursorPos);

    if (operator === "from:" || operator === "to:") {
      return contacts
        .filter((c) => c.toLowerCase().includes(valuePart))
        .slice(0, 6)
        .map((c) => ({
          kind: "value" as const,
          text: `${operator}${c}`,
          insertText: `${prefix}${operator}${c}${rest}`,
        }));
    }

    if (operator === "has:") {
      return HAS_VALUES
        .filter((v) => v.startsWith(valuePart))
        .map((v) => ({
          kind: "value" as const,
          text: `${operator}${v}`,
          insertText: `${prefix}${operator}${v}${rest}`,
        }));
    }

    if (operator === "is:") {
      return IS_VALUES
        .filter((v) => v.startsWith(valuePart))
        .map((v) => ({
          kind: "value" as const,
          text: `${operator}${v}`,
          insertText: `${prefix}${operator}${v}${rest}`,
        }));
    }

    if (operator === "in:") {
      return folderNames
        .filter((f) => f.toLowerCase().includes(valuePart))
        .slice(0, 6)
        .map((f) => ({
          kind: "value" as const,
          text: `${operator}${f}`,
          insertText: `${prefix}${operator}${f}${rest}`,
        }));
    }

    return [];
  }

  const matchingOps = OPERATORS.filter((op) => op.prefix.startsWith(lowerWord));
  if (matchingOps.length > 0 && lowerWord.length > 0) {
    const rest = query.slice(cursorPos);
    return matchingOps.map((op) => ({
      kind: "operator" as const,
      text: op.prefix,
      description: op.description,
      insertText: `${prefix}${op.prefix}${rest}`,
    }));
  }

  if (lowerWord.length === 0) {
    const rest = query.slice(cursorPos);
    return OPERATORS.map((op) => ({
      kind: "operator" as const,
      text: op.prefix,
      description: op.description,
      insertText: `${prefix}${op.prefix}${rest}`,
    }));
  }

  return [];
}

export function SearchAutocomplete({
  query,
  onSelect,
  visible,
  onDismiss,
  recentSearches = [],
  contacts = [],
  folderNames = [],
}: SearchAutocompleteProps) {
  const { t } = useTranslation();
  const [activeIndex, setActiveIndex] = useState(-1);
  const listRef = useRef<HTMLDivElement>(null);

  const suggestions = useMemo(
    () => buildSuggestions(query, query.length, recentSearches, contacts, folderNames),
    [query, recentSearches, contacts, folderNames],
  );

  useEffect(() => {
    setActiveIndex(-1);
  }, [query]);

  const selectSuggestion = useCallback(
    (suggestion: Suggestion) => {
      onSelect(suggestion.insertText);
      onDismiss();
    },
    [onSelect, onDismiss],
  );

  const handleKeyDown = useCallback(
    (e: KeyboardEvent) => {
      if (!visible || suggestions.length === 0) return;

      if (e.key === "ArrowDown") {
        e.preventDefault();
        setActiveIndex((prev) => (prev < suggestions.length - 1 ? prev + 1 : 0));
        return;
      }

      if (e.key === "ArrowUp") {
        e.preventDefault();
        setActiveIndex((prev) => (prev > 0 ? prev - 1 : suggestions.length - 1));
        return;
      }

      if (e.key === "Enter" && activeIndex >= 0) {
        e.preventDefault();
        selectSuggestion(suggestions[activeIndex]);
        return;
      }

      if (e.key === "Escape") {
        e.preventDefault();
        onDismiss();
      }
    },
    [visible, suggestions, activeIndex, selectSuggestion, onDismiss],
  );

  useEffect(() => {
    if (!visible) return;
    document.addEventListener("keydown", handleKeyDown);
    return () => document.removeEventListener("keydown", handleKeyDown);
  }, [visible, handleKeyDown]);

  useEffect(() => {
    if (activeIndex >= 0 && listRef.current) {
      const activeEl = listRef.current.children[activeIndex] as HTMLElement | undefined;
      activeEl?.scrollIntoView({ block: "nearest" });
    }
  }, [activeIndex]);

  if (!visible || suggestions.length === 0) return null;

  return (
    <div
      ref={listRef}
      role="listbox"
      aria-label={t("search.autocompleteSuggestions")}
      className="absolute left-0 right-0 top-full z-20 mt-1 max-h-64 overflow-y-auto rounded-xl border border-border bg-background shadow-lg"
    >
      {suggestions.map((suggestion, index) => (
        <button
          key={`${suggestion.kind}-${suggestion.text}-${index}`}
          type="button"
          role="option"
          aria-selected={index === activeIndex}
          onClick={() => selectSuggestion(suggestion)}
          onMouseEnter={() => setActiveIndex(index)}
          className={`flex w-full items-center gap-2 px-3 py-2 text-left text-sm transition-colors ${
            index === activeIndex
              ? "bg-primary/10 text-foreground"
              : "text-muted-foreground hover:bg-surface-hover hover:text-foreground"
          }`}
        >
          {suggestion.kind === "recent" ? (
            <Clock size={14} className="shrink-0 text-muted-foreground" />
          ) : suggestion.kind === "operator" ? (
            <code className="shrink-0 rounded bg-primary/10 px-1 py-0.5 text-[11px] font-semibold text-primary">op</code>
          ) : (
            <Search size={14} className="shrink-0 text-muted-foreground" />
          )}
          <span className="min-w-0 flex-1 truncate font-medium">{suggestion.text}</span>
          {suggestion.description && (
            <span className="shrink-0 text-xs text-muted-foreground">{suggestion.description}</span>
          )}
        </button>
      ))}
    </div>
  );
}
