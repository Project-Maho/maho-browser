import { useEffect, useMemo, useState } from "react";
import { ChevronLeft, ChevronRight } from "lucide-react";
import { format, startOfMonth, endOfMonth, startOfWeek, endOfWeek, addDays, isSameMonth, isSameDay, addMonths, subMonths } from "date-fns";
import { enUS, ko } from "date-fns/locale";
import { useTranslation } from "react-i18next";
import { CalendarEvent } from "../../types";

interface MiniCalendarProps {
  value: Date;
  onChange: (date: Date) => void;
  events: CalendarEvent[];
}

export function MiniCalendar({ value, onChange, events }: MiniCalendarProps) {
  const { t, i18n } = useTranslation();
  const [currentMonth, setCurrentMonth] = useState(startOfMonth(value));

  useEffect(() => {
    setCurrentMonth(startOfMonth(value));
  }, [value]);

  const handlePrevMonth = () => setCurrentMonth(subMonths(currentMonth, 1));
  const handleNextMonth = () => setCurrentMonth(addMonths(currentMonth, 1));

  const locale = i18n.language === "ko" ? ko : enUS;
  const weekdayKeys = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"] as const;

  const weeks = useMemo(() => {
    const monthStart = startOfMonth(currentMonth);
    const monthEnd = endOfMonth(monthStart);
    const startDate = startOfWeek(monthStart);
    const endDate = endOfWeek(monthEnd);

    const rows = [];
    let day = startDate;

    while (day <= endDate) {
      const week = [];
      for (let i = 0; i < 7; i++) {
        week.push(day);
        day = addDays(day, 1);
      }
      rows.push(week);
    }
    return rows;
  }, [currentMonth]);

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

  return (
    <div className="flex flex-col select-none">
      <div className="flex items-center justify-between mb-3">
        <span className="text-xs font-semibold text-foreground">
          {format(currentMonth, "MMMM yyyy", { locale })}
        </span>
        <div className="flex items-center gap-1">
          <button
            type="button"
            onClick={handlePrevMonth}
            className="mail-pressable flex h-7 w-7 items-center justify-center rounded-md text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
            aria-label="Previous month"
          >
            <ChevronLeft size={14} />
          </button>
          <button
            type="button"
            onClick={handleNextMonth}
            className="mail-pressable flex h-7 w-7 items-center justify-center rounded-md text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
            aria-label="Next month"
          >
            <ChevronRight size={14} />
          </button>
        </div>
      </div>

      <div className="grid grid-cols-7 gap-y-1.5 text-center text-[10px] font-medium text-muted-foreground mb-1.5">
        {weekdayKeys.map((k) => (
          <span key={k}>{t(`calendar.weekday.${k}`)}</span>
        ))}
      </div>

      <div className="flex flex-col gap-y-1">
        {weeks.map((week, wIdx) => (
          <div key={wIdx} className="grid grid-cols-7 text-center">
            {week.map((day, dIdx) => {
              const selected = isSameDay(day, value);
              const sameMonth = isSameMonth(day, currentMonth);
              const dayHasEvent = hasEvent(day);

              return (
                <button
                  key={dIdx}
                  type="button"
                  onClick={() => onChange(day)}
                  className={`relative flex h-7 w-7 items-center justify-center rounded-full text-xs transition-colors mx-auto ${
                    selected
                      ? "bg-primary text-primary-foreground font-semibold"
                      : sameMonth
                      ? "text-foreground hover:bg-surface-hover"
                      : "text-muted-foreground/45 hover:bg-surface-hover"
                  }`}
                >
                  <span>{day.getDate()}</span>
                  {dayHasEvent && !selected && (
                    <span className="absolute bottom-1 left-1/2 -translate-x-1/2 h-1 w-1 bg-accent rounded-full" />
                  )}
                </button>
              );
            })}
          </div>
        ))}
      </div>
    </div>
  );
}
