import { useState } from "react";
import { Clock, X } from "lucide-react";

interface SnoozePickerProps {
  isOpen: boolean;
  onClose: () => void;
  onSnooze: (until: string) => void;
}

const QUICK_OPTIONS = [
  { label: "Later today", hours: 3 },
  { label: "Tomorrow morning", tomorrowHour: 9 },
  { label: "Tomorrow evening", tomorrowHour: 18 },
  { label: "This weekend", days: "weekend" as const },
  { label: "Next week", days: 7 },
  { label: "Next month", days: 30 },
] as const;

function computeSnoozeDate(option: (typeof QUICK_OPTIONS)[number]): string {
  const now = new Date();
  if ("tomorrowHour" in option && typeof option.tomorrowHour === "number") {
    now.setDate(now.getDate() + 1);
    now.setHours(option.tomorrowHour, 0, 0, 0);
  } else if ("hours" in option && typeof option.hours === "number") {
    now.setHours(now.getHours() + option.hours);
  } else if ("days" in option && option.days === "weekend") {
    const dayOfWeek = now.getDay();
    const daysUntilSaturday = (6 - dayOfWeek + 7) % 7 || 7;
    now.setDate(now.getDate() + daysUntilSaturday);
    now.setHours(9, 0, 0, 0);
  } else if ("days" in option && typeof option.days === "number") {
    now.setDate(now.getDate() + option.days);
    now.setHours(9, 0, 0, 0);
  }
  return now.toISOString();
}

export function SnoozePicker({ isOpen, onClose, onSnooze }: SnoozePickerProps) {
  const [customDate, setCustomDate] = useState("");
  const [customTime, setCustomTime] = useState("09:00");

  if (!isOpen) return null;

  const handleCustomSnooze = () => {
    if (!customDate) return;
    const dt = new Date(`${customDate}T${customTime}`);
    onSnooze(dt.toISOString());
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/50">
      <div className="w-80 rounded-2xl border border-border bg-background p-4 shadow-xl">
        <div className="mb-3 flex items-center justify-between">
          <div className="flex items-center gap-2">
            <Clock size={16} className="text-primary" />
            <h3 className="text-sm font-semibold text-foreground">Snooze until</h3>
          </div>
          <button
            type="button"
            onClick={onClose}
            className="mail-pressable flex h-8 w-8 items-center justify-center rounded-full text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          >
            <X size={14} />
          </button>
        </div>

        <div className="space-y-1">
          {QUICK_OPTIONS.map((option) => (
            <button
              key={option.label}
              type="button"
              onClick={() => onSnooze(computeSnoozeDate(option))}
              className="flex min-h-10 w-full items-center rounded-lg px-3 text-left text-sm text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-ring"
            >
              {option.label}
            </button>
          ))}
        </div>

        <div className="mt-3 border-t border-border pt-3">
          <p className="mb-2 text-xs text-muted-foreground">Custom date & time</p>
          <div className="flex gap-2">
            <input
              type="date"
              value={customDate}
              onChange={(e) => setCustomDate(e.target.value)}
              className="flex-1 rounded-lg border border-border bg-card px-2 py-1.5 text-xs text-foreground focus:border-ring focus:outline-none"
            />
            <input
              type="time"
              value={customTime}
              onChange={(e) => setCustomTime(e.target.value)}
              className="w-24 rounded-lg border border-border bg-card px-2 py-1.5 text-xs text-foreground focus:border-ring focus:outline-none"
            />
          </div>
          <button
            type="button"
            onClick={handleCustomSnooze}
            disabled={!customDate}
            className="mail-pressable mt-2 flex h-9 w-full items-center justify-center rounded-lg bg-primary px-3 text-xs font-medium text-primary-foreground hover:bg-primary/90 disabled:opacity-50"
          >
            Snooze
          </button>
        </div>
      </div>
    </div>
  );
}
