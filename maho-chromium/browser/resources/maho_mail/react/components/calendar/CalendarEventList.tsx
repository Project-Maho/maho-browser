import { useCallback, useEffect, useMemo, useState } from "react";
import { useTranslation } from "react-i18next";
import { Calendar, MapPin, Plus, RefreshCw } from "lucide-react";
import { Button } from "../ui/Button";
import { Badge } from "../ui/Badge";
import { Skeleton } from "../ui/Skeleton";
import { useToast } from "../ui/Toast";
import { EmptyState } from "../common/EmptyState";
import * as api from "../../api";
import type { AccountSummary, CalendarEvent } from "../../types";

interface CalendarEventListProps {
  accountId: string;
  provider?: string;
  accounts?: AccountSummary[];
  onSelectEvent: (event: CalendarEvent) => void;
  onCreateEvent: () => void;
  onReauthorize?: () => void;
}

type DateGroup = "today" | "upcoming" | "past";

function getDateGroup(dtstart: string): DateGroup {
  const now = new Date();
  const start = new Date(dtstart);
  const todayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate());
  const todayEnd = new Date(todayStart.getTime() + 86400000);

  if (start >= todayStart && start < todayEnd) return "today";
  if (start >= todayEnd) return "upcoming";
  return "past";
}

function formatEventTime(event: CalendarEvent): string {
  if (event.all_day) return "";
  const start = new Date(event.dtstart);
  const time = start.toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit" });
  if (event.dtend) {
    const end = new Date(event.dtend);
    return `${time} – ${end.toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit" })}`;
  }
  return time;
}

function rsvpVariant(status: string | null): "default" | "secondary" | "destructive" | "success" {
  switch (status) {
    case "accepted": return "success";
    case "declined": return "destructive";
    case "tentative": return "secondary";
    default: return "secondary";
  }
}

function EventListSkeleton() {
  return (
    <div className="flex flex-col gap-1 p-2">
      {Array.from({ length: 5 }).map((_, i) => (
        <div key={i} className="flex items-center gap-3 rounded-lg px-3 py-3">
          <Skeleton className="h-8 w-8 shrink-0 rounded-lg" />
          <div className="flex-1 space-y-2">
            <Skeleton className="h-3.5 w-40" />
            <Skeleton className="h-3 w-24" />
          </div>
          <Skeleton className="h-5 w-16 rounded-full" />
        </div>
      ))}
    </div>
  );
}

export function CalendarEventList({ accountId, provider, accounts, onSelectEvent, onCreateEvent, onReauthorize }: CalendarEventListProps) {
  const { t } = useTranslation();
  const { toast } = useToast();
  const [events, setEvents] = useState<CalendarEvent[]>([]);
  const [loading, setLoading] = useState(false);
  const [syncing, setSyncing] = useState(false);
  const [reauthRequired, setReauthRequired] = useState(false);

  const gmailAccounts = useMemo(
    () => (accounts ?? []).filter((a) => a.auth_type === "oauth2_gmail" || a.provider === "gmail"),
    [accounts],
  );

  const effectiveAccountId = accountId || gmailAccounts[0]?.id || "";
  const isGmail =
    provider === "gmail" ||
    !!gmailAccounts.find((a) => a.id === effectiveAccountId);

  const fetchEvents = useCallback(async () => {
    if (!effectiveAccountId) {
      setEvents([]);
      return;
    }
    try {
      setLoading(true);
      const result = await api.listCalendarEvents(effectiveAccountId);
      result.sort((a, b) => new Date(a.dtstart).getTime() - new Date(b.dtstart).getTime());
      setEvents(result);
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("common.error"));
    } finally {
      setLoading(false);
    }
  }, [effectiveAccountId, toast, t]);

  useEffect(() => {
    fetchEvents();
  }, [fetchEvents]);

  useEffect(() => {
    const handler = (e: Event) => {
      const detail = (e as CustomEvent).detail;
      if (typeof detail === "string" && detail !== effectiveAccountId) return;
      void fetchEvents();
    };
    window.addEventListener("calendar-events-changed", handler);
    return () => window.removeEventListener("calendar-events-changed", handler);
  }, [effectiveAccountId, fetchEvents]);

  const handleRefresh = useCallback(async () => {
    const targets = gmailAccounts.length > 0
      ? gmailAccounts.map((a) => a.id)
      : (isGmail && effectiveAccountId ? [effectiveAccountId] : []);
    if (targets.length === 0) {
      await fetchEvents();
      return;
    }
    setSyncing(true);
    try {
      let total = 0;
      let needsReauth = false;
      for (const id of targets) {
        try {
          const result = await api.syncGoogleCalendar(id);
          if (result.reauthorize_required) needsReauth = true;
          else total += result.events_upserted + result.events_deleted;
        } catch (err) {
          const message = err instanceof Error ? err.message : String(err);
          toast("error", t("calendar.refreshError", { error: message }));
        }
      }
      if (needsReauth) {
        setReauthRequired(true);
        toast("warning", t("calendar.reauthorizeRequired"));
      } else {
        setReauthRequired(false);
        if (total > 0) toast("success", t("calendar.refreshSuccess", { count: total }));
        await fetchEvents();
      }
    } finally {
      setSyncing(false);
    }
  }, [gmailAccounts, isGmail, effectiveAccountId, fetchEvents, t, toast]);

  const renderHeader = (showCreate: boolean) => (
    <div className="flex items-center justify-between border-b border-border px-4 py-3">
      <h2 className="text-sm font-semibold text-foreground">{t("calendar.title")}</h2>
      <div className="flex items-center gap-1">
        <Button variant="ghost" size="sm" onClick={() => void handleRefresh()} disabled={syncing} aria-label={t("calendar.refresh")} title={t("calendar.refresh")}>
          <RefreshCw size={16} className={syncing ? "animate-spin" : ""} />
        </Button>
        {showCreate && (
          <Button variant="ghost" size="sm" onClick={onCreateEvent}>
            <Plus size={16} />
            {t("calendar.createEvent")}
          </Button>
        )}
      </div>
    </div>
  );

  const reauthBanner = reauthRequired ? (
    <div className="border-b border-border bg-amber-500/10 px-4 py-3">
      <div className="flex items-center justify-between gap-3">
        <p className="text-xs text-amber-700 dark:text-amber-400">
          {t("calendar.reauthorizeRequired")}
        </p>
        {onReauthorize && (
          <Button variant="secondary" size="sm" onClick={onReauthorize}>
            {t("calendar.reauthorizeAction")}
          </Button>
        )}
      </div>
    </div>
  ) : null;

  if (loading) {
    return (
      <div className="flex h-full flex-col">
        {renderHeader(false)}
        {reauthBanner}
        <EventListSkeleton />
      </div>
    );
  }

  if (events.length === 0) {
    return (
      <div className="flex h-full flex-col">
        {renderHeader(true)}
        {reauthBanner}
        <div className="flex flex-1 items-center justify-center p-8">
          <EmptyState
            title={t("calendar.emptyTitle")}
            description={t("calendar.emptyDescription")}
            icon={<Calendar size={28} className="text-muted-foreground" />}
            action={{ label: t("calendar.createEvent"), onClick: onCreateEvent }}
          />
        </div>
      </div>
    );
  }

  const grouped: Record<DateGroup, CalendarEvent[]> = { today: [], upcoming: [], past: [] };
  for (const event of events) {
    grouped[getDateGroup(event.dtstart)].push(event);
  }

  const groupOrder: DateGroup[] = ["today", "upcoming", "past"];

  return (
    <div className="flex h-full flex-col">
      {renderHeader(true)}
      {reauthBanner}
      <div className="flex-1 overflow-y-auto">
        {groupOrder.map((group) => {
          const items = grouped[group];
          if (items.length === 0) return null;
          return (
            <div key={group}>
              <div className="sticky top-0 z-10 bg-background px-4 py-2">
                <span className="text-xs font-semibold uppercase tracking-wider text-muted-foreground">
                  {t(`calendar.group.${group}`)}
                </span>
              </div>
              <div className="flex flex-col gap-0.5 px-2">
                {items.map((event) => (
                  <button
                    key={event.id}
                    type="button"
                    onClick={() => onSelectEvent(event)}
                    className="flex items-center gap-3 rounded-lg px-3 py-3 text-left transition-colors hover:bg-surface-hover focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary"
                  >
                    <div className="flex h-8 w-8 shrink-0 items-center justify-center rounded-lg bg-primary/10">
                      <Calendar size={16} className="text-primary" />
                    </div>
                    <div className="min-w-0 flex-1">
                      <p className="truncate text-sm font-medium text-foreground">{event.summary}</p>
                      <p className="text-xs text-muted-foreground">
                        {event.all_day ? t("calendar.allDay") : formatEventTime(event)}
                      </p>
                    </div>
                    <div className="flex shrink-0 items-center gap-2">
                      {event.location && (
                        <div className="flex items-center gap-1 text-xs text-muted-foreground">
                          <MapPin size={12} />
                        </div>
                      )}
                      {event.rsvp_status && (
                        <Badge variant={rsvpVariant(event.rsvp_status)}>
                          {t(`calendar.rsvp.${event.rsvp_status}`)}
                        </Badge>
                      )}
                    </div>
                  </button>
                ))}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}
