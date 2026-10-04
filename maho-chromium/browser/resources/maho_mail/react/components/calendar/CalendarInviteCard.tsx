import { useEffect, useMemo, useState } from "react";
import { useTranslation } from "react-i18next";
import { Calendar, Clock, MapPin, User } from "lucide-react";
import * as api from "../../api";
import type { CalendarEvent } from "../../types";
import { Button } from "../ui/Button";
import { Badge } from "../ui/Badge";
import { useToast } from "../ui/Toast";

interface CalendarInviteCardProps {
  emailId: string;
  accountId: string;
  bodyText: string | null;
  bodyHtml: string | null;
}

const RSVP_OPTIONS = ["accepted", "tentative", "declined"] as const;
const RSVP_LABEL_KEYS: Record<(typeof RSVP_OPTIONS)[number], string> = {
  accepted: "calendar.rsvpAccept",
  tentative: "calendar.rsvpTentative",
  declined: "calendar.rsvpDecline",
};
const RSVP_STATUS_KEYS: Record<(typeof RSVP_OPTIONS)[number], string> = {
  accepted: "calendar.rsvpAccepted",
  tentative: "calendar.rsvpTentativeStatus",
  declined: "calendar.rsvpDeclined",
};
const CALENDAR_INVITE_PATTERN = /BEGIN:(VCALENDAR|VEVENT)/i;

export function hasCalendarInviteContent(bodyText: string | null, bodyHtml: string | null): boolean {
  return CALENDAR_INVITE_PATTERN.test(bodyText ?? "") || CALENDAR_INVITE_PATTERN.test(bodyHtml ?? "");
}

function formatDateTime(iso: string, allDay: boolean): string {
  const date = new Date(iso);

  if (allDay) {
    return date.toLocaleDateString(undefined, {
      weekday: "short",
      year: "numeric",
      month: "short",
      day: "numeric",
    });
  }

  return `${date.toLocaleDateString(undefined, {
    weekday: "short",
    year: "numeric",
    month: "short",
    day: "numeric",
  })} ${date.toLocaleTimeString(undefined, {
    hour: "2-digit",
    minute: "2-digit",
  })}`;
}

export function CalendarInviteCard({ emailId, accountId, bodyText, bodyHtml }: CalendarInviteCardProps) {
  const { t } = useTranslation();
  const { toast } = useToast();
  const [event, setEvent] = useState<CalendarEvent | null>(null);
  const [loading, setLoading] = useState(false);
  const [loadingRsvp, setLoadingRsvp] = useState<string | null>(null);
  const hasInvite = useMemo(() => hasCalendarInviteContent(bodyText, bodyHtml), [bodyText, bodyHtml]);

  useEffect(() => {
    if (!hasInvite) {
      setEvent(null);
      return;
    }

    let cancelled = false;

    const loadInvite = async () => {
      try {
        setLoading(true);
        const events = await api.autoImportCalendarEvents(accountId, emailId, bodyHtml ?? undefined, bodyText ?? undefined);
        if (!cancelled) {
          setEvent(events[0] ?? null);
        }
      } catch {
        if (!cancelled) {
          setEvent(null);
        }
      } finally {
        if (!cancelled) {
          setLoading(false);
        }
      }
    };

    void loadInvite();

    return () => {
      cancelled = true;
    };
  }, [accountId, bodyHtml, bodyText, emailId, hasInvite]);

  const handleRsvp = async (status: string) => {
    if (!event) return;

    try {
      setLoadingRsvp(status);
      const updated = await api.updateRsvp(event.id, status);
      setEvent(updated);
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("calendar.rsvpFailed", "RSVP failed"));
    } finally {
      setLoadingRsvp(null);
    }
  };

  if (!hasInvite) {
    return null;
  }

  return (
    <div className="mb-4 rounded-lg border border-border bg-card/70 p-4 shadow-sm">
      <div className="flex items-start gap-3">
        <div className="flex h-9 w-9 shrink-0 items-center justify-center rounded-lg bg-primary/10">
          <Calendar size={18} className="text-primary" />
        </div>
        <div className="min-w-0 flex-1 space-y-1">
          <div className="flex flex-wrap items-center gap-2">
            <h4 className="truncate font-semibold text-foreground">
              {event?.summary ?? t("calendar.loadingInvite")}
            </h4>
            {event?.rsvp_status && (
              <Badge variant="secondary">
                {t(RSVP_STATUS_KEYS[event.rsvp_status as (typeof RSVP_OPTIONS)[number]] ?? event.rsvp_status)}
              </Badge>
            )}
          </div>
          {event ? (
            <p className="text-sm text-muted-foreground">{formatDateTime(event.dtstart, event.all_day)}{event.dtend ? ` – ${formatDateTime(event.dtend, event.all_day)}` : ""}</p>
          ) : (
            <p className="text-sm text-muted-foreground">{loading ? t("calendar.loadingInvite") : t("calendar.inviteDetected")}</p>
          )}
        </div>
      </div>

      {event && (
        <div className="mt-3 space-y-2 pl-12">
          <div className="flex items-start gap-2 text-sm text-muted-foreground">
            <Clock size={14} className="mt-0.5 shrink-0" />
            <span>{formatDateTime(event.dtstart, event.all_day)}{event.dtend ? ` – ${formatDateTime(event.dtend, event.all_day)}` : ""}</span>
          </div>
          {event.location && (
            <div className="flex items-start gap-2 text-sm text-muted-foreground">
              <MapPin size={14} className="mt-0.5 shrink-0" />
              <span className="truncate">{event.location}</span>
            </div>
          )}
          {event.organizer && (
            <div className="flex items-start gap-2 text-sm text-muted-foreground">
              <User size={14} className="mt-0.5 shrink-0" />
              <span className="truncate">{event.organizer}</span>
            </div>
          )}
          <div className="flex flex-wrap items-center gap-2 pt-1">
            {RSVP_OPTIONS.map((status) => (
              <Button
                key={status}
                variant={event.rsvp_status === status ? "default" : "outline"}
                size="sm"
                loading={loadingRsvp === status}
                onClick={() => void handleRsvp(status)}
              >
                {t(RSVP_LABEL_KEYS[status])}
              </Button>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}
