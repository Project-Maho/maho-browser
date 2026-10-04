import { useEffect, useState, useMemo } from "react";
import type { CalendarEvent } from "../../types";
import { format } from "date-fns";
import { Clock, MapPin } from "lucide-react";

interface TodayPanelProps {
  events: CalendarEvent[];
  onNavigateDate: (date: Date) => void;
}

export function TodayPanel({ events, onNavigateDate }: TodayPanelProps) {
  const [now, setNow] = useState(new Date());

  useEffect(() => {
    const timer = setInterval(() => setNow(new Date()), 60000);
    return () => clearInterval(timer);
  }, []);

  const todayEvents = useMemo(() => {
    const endOfToday = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 23, 59, 59, 999);

    return events
      .filter(e => {
        const d = new Date(e.dtstart);
        return d >= now && d <= endOfToday;
      })
      .sort((a, b) => a.dtstart.localeCompare(b.dtstart))
      .slice(0, 3);
  }, [events, now]);

  const tomorrowEvent = useMemo(() => {
    const startOfTomorrow = new Date(now.getFullYear(), now.getMonth(), now.getDate() + 1);
    const endOfTomorrow = new Date(now.getFullYear(), now.getMonth(), now.getDate() + 1, 23, 59, 59, 999);

    return events
      .filter(e => {
        const d = new Date(e.dtstart);
        return d >= startOfTomorrow && d <= endOfTomorrow;
      })
      .sort((a, b) => a.dtstart.localeCompare(b.dtstart))[0] || null;
  }, [events, now]);

  const handleEventClick = (dtstart: string) => {
    onNavigateDate(new Date(dtstart));
  };

  if (todayEvents.length === 0 && !tomorrowEvent) {
    return null;
  }

  return (
    <div className="space-y-3 p-3 bg-muted/20 border border-border/40 rounded-lg text-xs">
      <h3 className="font-semibold text-[11px] text-muted-foreground uppercase tracking-wider">
        Schedule
      </h3>
      <div className="space-y-2">
        {todayEvents.map(e => {
          const d = new Date(e.dtstart);
          return (
            <button
              key={e.id}
              onClick={() => handleEventClick(e.dtstart)}
              className="w-full text-left px-2.5 py-2 hover:bg-surface-hover rounded-lg border border-transparent hover:border-border/40 transition-colors flex flex-col gap-1 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
            >
              <div className="flex items-center justify-between font-medium">
                <span className="truncate text-foreground font-semibold">{e.summary || "(No Title)"}</span>
                <span className="text-[10px] text-primary shrink-0 flex items-center gap-0.5">
                  <Clock size={10} /> {format(d, "HH:mm")}
                </span>
              </div>
              {e.location && (
                <div className="text-[10px] text-muted-foreground flex items-center gap-1">
                  <MapPin size={9} /> <span className="truncate">{e.location}</span>
                </div>
              )}
            </button>
          );
        })}

        {tomorrowEvent && (
          <div className="mt-3 pt-3 border-t border-border/40">
            <span className="text-[10px] text-muted-foreground font-semibold uppercase block mb-1">
              Tomorrow
            </span>
            <button
              onClick={() => handleEventClick(tomorrowEvent.dtstart)}
              className="w-full text-left px-2.5 py-2 hover:bg-surface-hover rounded-lg border border-transparent hover:border-border/40 transition-colors flex flex-col gap-1 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
            >
              <div className="flex items-center justify-between font-medium">
                <span className="truncate text-foreground font-semibold">{tomorrowEvent.summary || "(No Title)"}</span>
                <span className="text-[10px] text-muted-foreground shrink-0 flex items-center gap-0.5">
                  <Clock size={10} /> {format(new Date(tomorrowEvent.dtstart), "HH:mm")}
                </span>
              </div>
              {tomorrowEvent.location && (
                <div className="text-[10px] text-muted-foreground flex items-center gap-1">
                  <MapPin size={9} /> <span className="truncate">{tomorrowEvent.location}</span>
                </div>
              )}
            </button>
          </div>
        )}
      </div>
    </div>
  );
}
