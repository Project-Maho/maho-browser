import React, { useState, useMemo } from "react";
import type { CalendarEvent } from "../../types";
import { format, addDays, subDays } from "date-fns";
import { ChevronLeft, ChevronRight, MapPin } from "lucide-react";

interface MobileDayViewProps {
  date: Date;
  onNavigate: (date: Date) => void;
  events: CalendarEvent[];
  onSelectEvent: (event: CalendarEvent) => void;
}

export function MobileDayView({ date, onNavigate, events, onSelectEvent }: MobileDayViewProps) {
  const [touchStart, setTouchStart] = useState<number | null>(null);

  const dayEvents = useMemo(() => {
    const startOfDayStr = format(date, "yyyy-MM-dd");
    return events
      .filter((e) => {
        if (!e.dtstart) return false;
        // Convert dtstart to local Date first to match local day boundaries
        const d = new Date(e.dtstart.includes("T") ? e.dtstart : `${e.dtstart}T00:00:00`);
        if (isNaN(d.getTime())) return false;
        return format(d, "yyyy-MM-dd") === startOfDayStr;
      })
      .sort((a, b) => a.dtstart.localeCompare(b.dtstart));
  }, [events, date]);

  const handleTouchStart = (e: React.TouchEvent) => {
    setTouchStart(e.targetTouches[0].clientX);
  };

  const handleTouchEnd = (e: React.TouchEvent) => {
    if (touchStart === null) return;
    const touchEnd = e.changedTouches[0].clientX;
    const diff = touchStart - touchEnd;

    // Threshold of 50px for swipe
    if (diff > 50) {
      onNavigate(addDays(date, 1));
    } else if (diff < -50) {
      onNavigate(subDays(date, 1));
    }
    setTouchStart(null);
  };

  const hours = Array.from({ length: 24 }, (_, i) => i);

  return (
    <div
      onTouchStart={handleTouchStart}
      onTouchEnd={handleTouchEnd}
      className="flex flex-col h-full bg-background select-none overflow-hidden"
    >
      <div className="flex items-center justify-between p-3 border-b border-border bg-muted/20">
        <button
          onClick={() => onNavigate(subDays(date, 1))}
          className="mail-pressable flex h-11 w-11 items-center justify-center rounded-lg text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
        >
          <ChevronLeft size={16} />
        </button>
        <span className="text-sm font-semibold text-foreground">
          {format(date, "EEEE, MMMM d, yyyy")}
        </span>
        <button
          onClick={() => onNavigate(addDays(date, 1))}
          className="mail-pressable flex h-11 w-11 items-center justify-center rounded-lg text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
        >
          <ChevronRight size={16} />
        </button>
      </div>

      <div className="flex-1 overflow-y-auto relative p-2">
        {hours.map((hour) => {
          const formattedHour = `${String(hour).padStart(2, "0")}:00`;
          return (
            <div key={hour} className="flex border-b border-border/40 h-16 relative">
              <span className="w-12 text-[10px] text-muted-foreground pt-1 pr-2 text-right">
                {formattedHour}
              </span>
              <div className="flex-1 relative" />
            </div>
          );
        })}

        {dayEvents.map((ev) => {
          const startDate = new Date(ev.dtstart);
          const startHour = startDate.getHours();
          const startMin = startDate.getMinutes();

          const endDate = ev.dtend ? new Date(ev.dtend) : new Date(startDate.getTime() + 60 * 60 * 1000);
          let durationHours = (endDate.getTime() - startDate.getTime()) / (1000 * 60 * 60);
          if (durationHours <= 0) durationHours = 1;

          const topPos = startHour * 64 + (startMin / 60) * 64 + 8;
          const heightPos = durationHours * 64 - 4;

          return (
            <button
              key={ev.id}
              onClick={() => onSelectEvent(ev)}
              className="absolute left-14 right-4 rounded-md border border-primary/20 bg-primary/10 hover:bg-primary/15 text-left p-2 transition-colors flex flex-col justify-between overflow-hidden shadow-sm"
              style={{
                top: `${topPos}px`,
                height: `${heightPos}px`,
                borderLeft: "4px solid var(--primary)",
              }}
            >
              <div>
                <p className="text-xs font-semibold truncate text-foreground">
                  {ev.summary || "(No Title)"}
                </p>
                {ev.location && (
                  <p className="flex items-center gap-1 text-[10px] text-muted-foreground truncate">
                    <MapPin size={10} className="shrink-0" aria-hidden="true" /> {ev.location}
                  </p>
                )}
              </div>
              <span className="text-[9px] text-primary/80 font-medium">
                {format(startDate, "HH:mm")} - {format(endDate, "HH:mm")}
              </span>
            </button>
          );
        })}

        {dayEvents.length === 0 && (
          <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
            <p className="text-xs text-muted-foreground">No events scheduled today</p>
          </div>
        )}
      </div>
    </div>
  );
}
