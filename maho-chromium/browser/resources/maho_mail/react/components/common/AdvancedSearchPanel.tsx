import { useState } from "react";
import { Filter, X } from "lucide-react";

export interface SearchFilters {
  from?: string;
  to?: string;
  hasAttachment?: boolean;
  isUnread?: boolean;
  isStarred?: boolean;
  dateFrom?: string;
  dateTo?: string;
}

interface AdvancedSearchProps {
  filters: SearchFilters;
  onChange: (filters: SearchFilters) => void;
}

export function AdvancedSearchPanel({ filters, onChange }: AdvancedSearchProps) {
  const [open, setOpen] = useState(false);
  const activeCount = Object.values(filters).filter(Boolean).length;

  function updateFilter<K extends keyof SearchFilters>(key: K, value: SearchFilters[K]) {
    onChange({ ...filters, [key]: value || undefined });
  }

  function clearAll() {
    onChange({});
  }

  return (
    <div>
      <button
        type="button"
        onClick={() => setOpen(!open)}
        className={`mail-pressable relative flex h-7 w-7 items-center justify-center rounded-md ${
          activeCount > 0
            ? "text-primary hover:bg-primary/80/10"
            : "text-muted-foreground hover:bg-surface-hover hover:text-foreground"
        }`}
        title="Search filters"
      >
        <Filter size={14} />
        {activeCount > 0 && (
          <span className="absolute -right-1 -top-1 flex h-3.5 w-3.5 items-center justify-center rounded-full bg-primary text-[8px] font-bold text-primary-foreground tabular-nums">
            {activeCount}
          </span>
        )}
      </button>

      {open && (
        <div className="absolute left-0 right-0 top-full z-40 border-b border-border bg-background p-3 shadow-lg">
          <div className="mb-2 flex items-center justify-between">
            <span className="text-xs font-medium text-muted-foreground">Filters</span>
            {activeCount > 0 && (
              <button
                type="button"
                onClick={clearAll}
                className="flex items-center gap-1 text-xs text-muted-foreground hover:text-muted-foreground"
              >
                <X size={10} /> Clear all
              </button>
            )}
          </div>
          <div className="grid grid-cols-2 gap-2">
            <input
              type="text"
              placeholder="From..."
              value={filters.from ?? ""}
              onChange={(e) => updateFilter("from", e.target.value)}
              className="rounded border border-border bg-card px-2 py-1.5 text-xs text-foreground placeholder:text-muted-foreground focus:border-ring focus:outline-none"
            />
            <input
              type="text"
              placeholder="To..."
              value={filters.to ?? ""}
              onChange={(e) => updateFilter("to", e.target.value)}
              className="rounded border border-border bg-card px-2 py-1.5 text-xs text-foreground placeholder:text-muted-foreground focus:border-ring focus:outline-none"
            />
            <input
              type="date"
              value={filters.dateFrom ?? ""}
              onChange={(e) => updateFilter("dateFrom", e.target.value)}
              className="rounded border border-border bg-card px-2 py-1.5 text-xs text-foreground focus:border-ring focus:outline-none"
              title="From date"
            />
            <input
              type="date"
              value={filters.dateTo ?? ""}
              onChange={(e) => updateFilter("dateTo", e.target.value)}
              className="rounded border border-border bg-card px-2 py-1.5 text-xs text-foreground focus:border-ring focus:outline-none"
              title="To date"
            />
          </div>
          <div className="mt-2 flex flex-wrap gap-3">
            <label className="flex items-center gap-1.5 text-xs text-muted-foreground">
              <input
                type="checkbox"
                checked={filters.hasAttachment ?? false}
                onChange={(e) => updateFilter("hasAttachment", e.target.checked || undefined)}
                className="rounded border-input bg-card text-primary focus:ring-ring"
              />
              Has attachment
            </label>
            <label className="flex items-center gap-1.5 text-xs text-muted-foreground">
              <input
                type="checkbox"
                checked={filters.isUnread ?? false}
                onChange={(e) => updateFilter("isUnread", e.target.checked || undefined)}
                className="rounded border-input bg-card text-primary focus:ring-ring"
              />
              Unread only
            </label>
            <label className="flex items-center gap-1.5 text-xs text-muted-foreground">
              <input
                type="checkbox"
                checked={filters.isStarred ?? false}
                onChange={(e) => updateFilter("isStarred", e.target.checked || undefined)}
                className="rounded border-input bg-card text-primary focus:ring-ring"
              />
              Starred only
            </label>
          </div>
        </div>
      )}
    </div>
  );
}

