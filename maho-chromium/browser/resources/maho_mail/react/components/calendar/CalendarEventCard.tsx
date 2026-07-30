import { useTranslation } from "react-i18next";
import { Calendar, MapPin, User } from "lucide-react";
import { Button } from "../ui/Button";
import { Badge } from "../ui/Badge";
import type { CalendarEvent } from "../../types";

interface CalendarEventCardProps {
  event: CalendarEvent;
  accountEmail: string;
  onRsvp: (eventId: string, status: string) => void;
  loading?: boolean;
}

function formatEventTime(event: CalendarEvent): string {
  const start = new Date(event.dtstart);
  if (event.all_day) {
    return start.toLocaleDateString();
  }
  const startStr = start.toLocaleDateString(undefined, {
    weekday: "short",
    month: "short",
    day: "numeric",
  }) + " " + start.toLocaleTimeString(undefined, {
    hour: "2-digit",
    minute: "2-digit",
  });
  if (event.dtend) {
    const end = new Date(event.dtend);
    const endStr = end.toLocaleTimeString(undefined, {
      hour: "2-digit",
      minute: "2-digit",
    });
    return `${startStr} – ${endStr}`;
  }
  return startStr;
}

const RSVP_OPTIONS = ["accepted", "declined", "tentative"] as const;

export function CalendarEventCard({ event, accountEmail: _accountEmail, onRsvp, loading }: CalendarEventCardProps) {
  const { t } = useTranslation();

  return (
    <div className="rounded-lg border border-border bg-card p-4 space-y-3">
      <div className="flex items-start gap-3">
        <div className="flex h-9 w-9 shrink-0 items-center justify-center rounded-lg bg-primary/10">
          <Calendar size={18} className="text-primary" />
        </div>
        <div className="min-w-0 flex-1 space-y-1">
          <h4 className="font-semibold text-foreground truncate">{event.summary}</h4>
          <p className="text-sm text-muted-foreground">{formatEventTime(event)}</p>
          {event.all_day && (
            <Badge variant="secondary">{t("calendar.allDay")}</Badge>
          )}
        </div>
      </div>

      {(event.location || event.organizer) && (
        <div className="space-y-1.5 pl-12">
          {event.location && (
            <div className="flex items-center gap-2 text-sm text-muted-foreground">
              <MapPin size={14} className="shrink-0" />
              <span className="truncate">{event.location}</span>
            </div>
          )}
          {event.organizer && (
            <div className="flex items-center gap-2 text-sm text-muted-foreground">
              <User size={14} className="shrink-0" />
              <span className="truncate">{event.organizer}</span>
            </div>
          )}
        </div>
      )}

      <div className="flex items-center gap-2 pl-12">
        {RSVP_OPTIONS.map((status) => (
          <Button
            key={status}
            variant={event.rsvp_status === status ? "default" : "outline"}
            size="sm"
            loading={loading}
            onClick={() => onRsvp(event.id, status)}
          >
            {t(`calendar.rsvp.${status}`)}
          </Button>
        ))}
      </div>
    </div>
  );
}
