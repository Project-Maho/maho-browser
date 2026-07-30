import { useState } from "react";
import { useTranslation } from "react-i18next";
import { Clock, X } from "lucide-react";
import { useToast } from "../ui/Toast";

interface SchedulePickerProps {
  isOpen: boolean;
  onClose: () => void;
  onSchedule: (scheduledAt: string) => void;
}

const QUICK_OPTIONS = [
  { labelKey: "compose.scheduleIn1Hour", hours: 1 },
  { labelKey: "compose.scheduleIn2Hours", hours: 2 },
  { labelKey: "compose.scheduleIn4Hours", hours: 4 },
  { labelKey: "compose.scheduleTomorrowMorning", hours: 18 },
  { labelKey: "compose.scheduleTomorrowAfternoon", hours: 24 },
  { labelKey: "compose.scheduleMondayMorning", days: "monday" as const },
] as const;

function computeScheduleDate(option: (typeof QUICK_OPTIONS)[number]): string {
  const now = new Date();
  if ("hours" in option && typeof option.hours === "number") {
    now.setHours(now.getHours() + option.hours);
  } else if (option.days === "monday") {
    const dayOfWeek = now.getDay();
    const daysUntilMonday = (1 - dayOfWeek + 7) % 7 || 7;
    now.setDate(now.getDate() + daysUntilMonday);
    now.setHours(9, 0, 0, 0);
  }
  return now.toISOString();
}

export function SchedulePicker({ isOpen, onClose, onSchedule }: SchedulePickerProps) {
  const [customDate, setCustomDate] = useState("");
  const [customTime, setCustomTime] = useState("09:00");
  const { t } = useTranslation();
  const { toast } = useToast();

  if (!isOpen) return null;

  const handleCustomSchedule = () => {
    if (!customDate) return;
    const dt = new Date(`${customDate}T${customTime}`);
    if (dt.getTime() <= Date.now()) {
      toast("error", t("compose.schedulePastTime"));
      return;
    }
    onSchedule(dt.toISOString());
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/50">
      <div className="w-80 rounded-2xl border border-border bg-background p-4 shadow-xl">
        <div className="mb-3 flex items-center justify-between">
          <div className="flex items-center gap-2">
            <Clock size={16} className="text-amber-400" />
            <h3 className="text-sm font-semibold text-foreground">{t("compose.scheduleSendTitle")}</h3>
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
              key={option.labelKey}
              type="button"
              onClick={() => onSchedule(computeScheduleDate(option))}
              className="w-full rounded-lg px-3 py-2 text-left text-sm text-muted-foreground transition-colors hover:bg-card hover:text-foreground"
            >
              {t(option.labelKey)}
            </button>
          ))}
        </div>

        <div className="mt-3 border-t border-border pt-3">
          <p className="mb-2 text-xs text-muted-foreground">{t("compose.scheduleCustomLabel")}</p>
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
            onClick={handleCustomSchedule}
            disabled={!customDate}
            className="mt-2 w-full rounded-lg bg-amber-600 px-3 py-1.5 text-xs font-medium text-white transition-colors hover:bg-amber-500 disabled:opacity-50"
          >
            {t("compose.scheduleSendButton")}
          </button>
        </div>
      </div>
    </div>
  );
}
