import { useEffect, useMemo, useState } from "react";
import { ChevronLeft, ChevronRight } from "lucide-react";
import { format, startOfYear, addMonths, startOfMonth, endOfMonth, startOfWeek, endOfWeek, addDays, isToday } from "date-fns";
import { enUS, ko } from "date-fns/locale";
import { useTranslation } from "react-i18next";
import { CalendarEvent } from "../../types";

interface YearViewProps {
  date: Date;
  onNavigate: (date: Date) => void;
  onViewChange: (view: "month" | "week" | "day" | "agenda" | "year") => void;
  events: CalendarEvent[];
}

export function YearView({ date, onNavigate, onViewChange, events }: YearViewProps) {
  const { t, i18n } = useTranslation();
  const [currentYear, setCurrentYear] = useState(startOfYear(date));

  useEffect(() => {
    setCurrentYear(startOfYear(date));
  }, [date]);

  const locale = i18n.language === "ko" ? ko : enUS;
  const weekdayKeys = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"] as const;

  const handlePrevYear = () => {
    const prev = new Date(currentYear);
    prev.setFullYear(prev.getFullYear() - 1);
    setCurrentYear(startOfYear(prev));
  };

  const handleNextYear = () => {
    const next = new Date(currentYear);
    next.setFullYear(next.getFullYear() + 1);
    setCurrentYear(startOfYear(next));
  };

  const eventDaysMap = useMemo(() => {
    const map: Record<string, boolean> = {};
    for (const e of events) {
      if (!e.dtstart) continue;
      // Convert dtstart to local Date first to match local day boundaries
      const d = new Date(e.dtstart.includes("T") ? e.dtstart : `${e.dtstart}T00:00:00`);
      if (isNaN(d.getTime())) continue;
      const dateStr = `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
      map[dateStr] = true;
    }
    return map;
  }, [events]);

  const hasEvent = (day: Date) => {
    const key = `${day.getFullYear()}-${String(day.getMonth() + 1).padStart(2, "0")}-${String(day.getDate()).padStart(2, "0")}`;
    return !!eventDaysMap[key];
  };

  const renderMonth = (monthOffset: number) => {
    const monthStart = startOfMonth(addMonths(currentYear, monthOffset));
    const monthEnd = endOfMonth(monthStart);
    const startDate = startOfWeek(monthStart);
    const endDate = endOfWeek(monthEnd);

    const weeks = [];
    let day = startDate;

    while (day <= endDate) {
      const week = [];
      for (let i = 0; i < 7; i++) {
        week.push(day);
        day = addDays(day, 1);
      }
      weeks.push(week);
    }

    return (
      <div key={monthOffset} className="flex flex-col border border-border/40 rounded-xl p-3 bg-card/30">
        <h4 className="text-xs font-semibold text-foreground mb-2 text-center">
          {format(monthStart, "MMMM", { locale })}
        </h4>
        <div className="grid grid-cols-7 gap-y-1 text-center text-[9px] font-medium text-muted-foreground mb-1">
          {weekdayKeys.map((k) => (
            <span key={k}>{t(`calendar.weekday.${k}`)}</span>
          ))}
        </div>
        <div className="flex flex-col gap-y-1">
          {weeks.map((week, wIdx) => (
            <div key={wIdx} className="grid grid-cols-7 text-center">
              {week.map((d, dIdx) => {
                const sameMonth = d.getMonth() === monthStart.getMonth();
                const isT = isToday(d);
                const dayHasEvent = hasEvent(d);

                if (!sameMonth) {
                  return <span key={dIdx} className="h-5 w-5" />;
                }

                return (
                  <button
                    key={dIdx}
                    type="button"
                    onClick={() => {
                      onNavigate(d);
                      onViewChange("month");
                    }}
                    className={`relative flex h-5 w-5 items-center justify-center rounded-full text-[10px] tabular-nums transition-colors mx-auto ${
                      isT
                        ? "bg-accent text-accent-foreground font-semibold"
                        : dayHasEvent
                        ? "bg-primary/10 text-foreground font-medium hover:bg-primary/15"
                        : "text-foreground hover:bg-surface-hover"
                    }`}
                  >
                    <span>{d.getDate()}</span>
                    {dayHasEvent && !isT && (
                      <span className="absolute bottom-0.5 left-1/2 -translate-x-1/2 h-0.5 w-0.5 bg-primary rounded-full" />
                    )}
                  </button>
                );
              })}
            </div>
          ))}
        </div>
      </div>
    );
  };

  return (
    <div className="flex h-full w-full flex-col overflow-y-auto p-4 space-y-4">
      <div className="flex items-center justify-between border-b border-border/60 pb-3">
        <h3 className="text-base font-semibold text-foreground">
          {format(currentYear, "yyyy", { locale })}
        </h3>
        <div className="flex items-center gap-1">
          <button
            type="button"
            onClick={handlePrevYear}
            className="mail-pressable flex h-8 w-8 items-center justify-center rounded-md text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
            aria-label="Previous year"
          >
            <ChevronLeft size={16} />
          </button>
          <button
            type="button"
            onClick={handleNextYear}
            className="mail-pressable flex h-8 w-8 items-center justify-center rounded-md text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
            aria-label="Next year"
          >
            <ChevronRight size={16} />
          </button>
        </div>
      </div>

      <div className="grid grid-cols-1 sm:grid-cols-2 md:grid-cols-3 lg:grid-cols-4 gap-4">
        {Array.from({ length: 12 }).map((_, i) => renderMonth(i))}
      </div>
    </div>
  );
}
