import { useState } from "react";
import { useTranslation } from "react-i18next";
import { Clock, X } from "lucide-react";
import { Dialog, DialogContent, DialogDescription, DialogTitle } from "../ui/dialog";
import { useToast } from "../ui/Toast";

interface SnoozePickerProps {
  isOpen: boolean;
  onClose: () => void;
  onSnooze: (until: string) => void;
}

const QUICK_OPTIONS = [
  { labelKey: "email.snoozeLaterToday", hours: 3 },
  { labelKey: "email.snoozeTomorrowMorning", tomorrowHour: 9 },
  { labelKey: "email.snoozeTomorrowEvening", tomorrowHour: 18 },
  { labelKey: "email.snoozeThisWeekend", days: "weekend" as const },
  { labelKey: "email.snoozeNextWeek", days: 7 },
  { labelKey: "email.snoozeNextMonth", days: 30 },
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
  const { t } = useTranslation();
  const { toast } = useToast();

  const handleClose = () => {
    setCustomDate("");
    onClose();
  };

  const handleCustomSnooze = () => {
    if (!customDate) return;
    const dt = new Date(`${customDate}T${customTime}`);
    if (dt.getTime() <= Date.now()) {
      toast("error", t("email.snoozePastTime"));
      return;
    }
    onSnooze(dt.toISOString());
  };

  return (
    <Dialog open={isOpen} onOpenChange={(open) => { if (!open) handleClose(); }}>
      <DialogContent hideDefaultClose className="w-80 max-w-[calc(100vw-2rem)] gap-0 rounded-2xl border border-border bg-background p-4 shadow-xl sm:rounded-2xl">
        <DialogDescription className="sr-only">{t("email.snoozeDescription")}</DialogDescription>
        <div className="mb-3 flex items-center justify-between">
          <div className="flex items-center gap-2">
            <Clock size={16} className="text-primary" />
            <DialogTitle className="text-sm font-semibold text-foreground">{t("email.snoozeTitle")}</DialogTitle>
          </div>
          <button
            type="button"
            onClick={handleClose}
            aria-label={t("common.close")}
            title={t("common.close")}
            className="mail-pressable flex h-8 w-8 items-center justify-center rounded-full text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          >
            <X size={14} />
          </button>
        </div>

        <div className="space-y-1">
          {QUICK_OPTIONS.map((option) => (
            <button
              key={option.labelKey}
              type="button"
              onClick={() => onSnooze(computeSnoozeDate(option))}
              className="flex min-h-10 w-full items-center rounded-lg px-3 text-left text-sm text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-ring"
            >
              {t(option.labelKey)}
            </button>
          ))}
        </div>

        <div className="mt-3 border-t border-border pt-3">
          <p className="mb-2 text-xs text-muted-foreground">{t("email.snoozeCustom")}</p>
          <div className="flex gap-2">
            <input
              type="date"
              aria-label={t("email.snoozeCustomDate")}
              value={customDate}
              onChange={(e) => setCustomDate(e.target.value)}
              className="flex-1 rounded-lg border border-border bg-card px-2 py-1.5 text-xs text-foreground focus:border-ring focus:outline-none"
            />
            <input
              type="time"
              aria-label={t("email.snoozeCustomTime")}
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
            {t("email.snoozeAction")}
          </button>
        </div>
      </DialogContent>
    </Dialog>
  );
}
