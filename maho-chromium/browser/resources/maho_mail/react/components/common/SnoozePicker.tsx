import { useState } from "react";
import { Clock, X } from "lucide-react";

interface SnoozePickerProps {
  isOpen: boolean;
  onClose: () => void;
  onSnooze: (until: string) => void;
}

const QUICK_OPTIONS = [
  { label: "Later today", hours: 3 },
  { label: "Tomorrow morning", hours: 18 },
  { label: "Tomorrow evening", hours: 30 },
  { label: "This weekend", days: "weekend" as const },
  { label: "Next week", days: 7 },
  { label: "Next month", days: 30 },
] as const;

function computeSnoozeDate(option: (typeof QUICK_OPTIONS)[number]): string {
  const now = new Date();
  if ("hours" in option && typeof option.hours === "number") {
    now.setHours(now.getHours() + option.hours);
  } else if (option.days === "weekend") {
    const dayOfWeek = now.getDay();
    const daysUntilSaturday = (6 - dayOfWeek + 7) % 7 || 7;
    now.setDate(now.getDate() + daysUntilSaturday);
    now.setHours(9, 0, 0, 0);
  } else if (typeof option.days === "number") {
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
            className="rounded-full p-1 text-muted-foreground hover:bg-card hover:text-muted-foreground"
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
              className="w-full rounded-lg px-3 py-2 text-left text-sm text-muted-foreground transition-colors hover:bg-card hover:text-foreground"
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
            className="mt-2 w-full rounded-lg bg-primary px-3 py-1.5 text-xs font-medium text-white transition-colors hover:bg-primary/80 disabled:opacity-50"
          >
            Snooze
          </button>
        </div>
      </div>
    </div>
  );
}
