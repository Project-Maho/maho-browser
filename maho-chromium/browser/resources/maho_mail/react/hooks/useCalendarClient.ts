import { useState, useCallback, useEffect, useRef } from "react";
import { toast as sonnerToast } from "sonner";
import * as api from "../api";
import type { CalendarEvent, GoogleCalendarEntry, CalendarCategory, AccountSummary } from "../types";
import { useToast } from "../components/ui/Toast";
import { useTranslation } from "react-i18next";
import * as chrono from "chrono-node";
import { listen } from "../events.js";

interface UseCalendarClientProps {
  accountId: string;
  accounts?: AccountSummary[];
  onSelectEvent?: (event: CalendarEvent) => void;
  onCreateEvent?: (initialSummary?: string) => void;
  currentDate?: Date;
  currentView?: string;
}

export function useCalendarClient({
  accountId,
  accounts,
  onSelectEvent,
  onCreateEvent,
  currentDate,
  currentView,
}: UseCalendarClientProps) {
  const { t } = useTranslation();
  const { toast } = useToast();

  const [events, setEvents] = useState<CalendarEvent[]>([]);
  const [calendars, setCalendars] = useState<GoogleCalendarEntry[]>([]);
  const [categories, setCategories] = useState<CalendarCategory[]>([]);
  const [loading, setLoading] = useState(false);
  const [syncing, setSyncing] = useState(false);
  const [reauthRequired, setReauthRequired] = useState(false);

  // Layout & display states
  const [hideWeekends, setHideWeekends] = useState(false);
  const [weekStart, setWeekStart] = useState("0");
  const [selectedTimezone, setSelectedTimezone] = useState(Intl.DateTimeFormat().resolvedOptions().timeZone);

  // UX states
  const [quickAddText, setQuickAddText] = useState("");
  const [selectedEventIds, setSelectedEventIds] = useState<string[]>([]);
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number; event: CalendarEvent } | null>(null);

  const [workingHoursStart, setWorkingHoursStart] = useState("09:00");
  const [workingHoursEnd, setWorkingHoursEnd] = useState("18:00");
  const [pendingDeletions, setPendingDeletions] = useState<{ [id: string]: { event: CalendarEvent; timeoutId: any } }>({});

  const eventCacheRef = useRef<Map<string, { timestamp: number; evs: CalendarEvent[]; cals: GoogleCalendarEntry[]; cats: CalendarCategory[] }>>(new Map());
  const CACHE_TTL = 5 * 60 * 1000; // 5 minutes

  const gmailAccounts = accounts
    ? accounts.filter((a) => a.auth_type === "oauth2_gmail" || a.provider === "gmail")
    : [];
  const effectiveAccountId = accountId || gmailAccounts[0]?.id || "";

  const fetchEvents = useCallback(async () => {
    const targetAccounts = (accounts && accounts.length > 0)
      ? accounts
      : (effectiveAccountId ? [{ id: effectiveAccountId }] : []);

    if (targetAccounts.length === 0) {
      setEvents([]);
      setCalendars([]);
      setCategories([]);
      return;
    }

    const formatISODateOnly = (d: Date) => d.toISOString().substring(0, 10);
    const fromDate = currentDate ? formatISODateOnly(new Date(currentDate.getTime() - 45 * 24 * 60 * 60 * 1000)) : undefined;
    const toDate = currentDate ? formatISODateOnly(new Date(currentDate.getTime() + 45 * 24 * 60 * 60 * 1000)) : undefined;

    try {
      setLoading(true);
      const promises = targetAccounts.map(async (acc) => {
        const monthBucket = currentDate ? `${currentDate.getFullYear()}-${currentDate.getMonth()}` : "all";
        const cacheKey = `${acc.id}_${monthBucket}`;
        const cached = eventCacheRef.current.get(cacheKey);
        const now = Date.now();
        if (cached && (now - cached.timestamp < CACHE_TTL)) {
          return { evs: cached.evs, cals: cached.cals, cats: cached.cats };
        }

        try {
          const [evs, cals, cats] = await Promise.all([
            api.listCalendarEvents(acc.id, fromDate, toDate),
            api.listAccountCalendars(acc.id),
            api.listCalendarCategories(acc.id),
          ]);
          eventCacheRef.current.set(cacheKey, { timestamp: now, evs, cals, cats });
          return { evs, cals, cats };
        } catch (e) {
          console.error(`Failed to load calendar data for account ${acc.id}:`, e);
          return { evs: [], cals: [], cats: [] };
        }
      });

      const results = await Promise.all(promises);
      const allEvents = results.flatMap(r => r.evs);
      const allCalendars = results.flatMap(r => r.cals);
      const allCategories = results.flatMap(r => r.cats);

      setEvents(allEvents);
      setCalendars(allCalendars);
      setCategories(allCategories);
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("common.error"));
    } finally {
      setLoading(false);
    }
  }, [accounts, effectiveAccountId, toast, t, currentDate, currentView]);

  const reloadEvents = useCallback(async () => {
    // Mutations invalidate the TTL cache: without this the next fetchEvents()
    // is served from eventCacheRef and the new/changed event stays invisible
    // for up to CACHE_TTL.
    eventCacheRef.current.clear();
    await fetchEvents();
  }, [fetchEvents]);

  const loadPreferences = useCallback(async () => {
    try {
      const hw = await api.getAppSetting("calendar.hide_weekends");
      setHideWeekends(hw === "true");
      
      const ws = await api.getAppSetting("calendar.week_start");
      setWeekStart(ws || "0");
      localStorage.setItem("maho-calendar.week_start", ws || "0");

      const whs = await api.getAppSetting("calendar.working_hours_start");
      if (whs) setWorkingHoursStart(whs);
      const whe = await api.getAppSetting("calendar.working_hours_end");
      if (whe) setWorkingHoursEnd(whe);
    } catch (err) {
      console.error("Failed to load calendar preferences", err);
    }
  }, []);

  const handleRefresh = useCallback(async () => {
    const targets = gmailAccounts.map((a) => a.id);
    if (targets.length === 0) {
      await reloadEvents();
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
          toast("error", t("calendar.refreshError", { error: err instanceof Error ? err.message : String(err) }));
        }
      }
      if (needsReauth) {
        setReauthRequired(true);
        toast("warning", t("calendar.reauthorizeRequired"));
      } else {
        setReauthRequired(false);
        if (total > 0) toast("success", t("calendar.refreshSuccess", { count: total }));
        await reloadEvents();
      }
    } finally {
      setSyncing(false);
    }
  }, [gmailAccounts, reloadEvents, t, toast]);

  const toggleCalendar = useCallback(async (cal: GoogleCalendarEntry) => {
    const next = !cal.visible;
    setCalendars((prev) => prev.map((c) => (c.id === cal.id ? { ...c, visible: next } : c)));
    try {
      await api.setCalendarVisibility(cal.id, next);
      await reloadEvents();
    } catch (err) {
      setCalendars((prev) => prev.map((c) => (c.id === cal.id ? { ...c, visible: cal.visible } : c)));
      toast("error", err instanceof Error ? err.message : t("common.error"));
    }
  }, [reloadEvents, t, toast]);

  const handleDuplicateEvent = async (eventId: string) => {
    try {
      await api.duplicateCalendarEvent(eventId);
      toast("success", "Event duplicated!");
      await reloadEvents();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : String(err));
    }
  };

  const handleExportIcsFile = async (event: CalendarEvent) => {
    try {
      const calendarIds = event.google_calendar_id ? [event.google_calendar_id] : undefined;
      const icsData = await api.exportCalendarIcs(event.account_id, calendarIds, event.dtstart, event.dtstart);
      const blob = new Blob([icsData], { type: "text/calendar;charset=utf-8" });
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");
      link.href = url;
      link.setAttribute("download", `${event.summary || "event"}.ics`);
      document.body.appendChild(link);
      link.click();
      document.body.removeChild(link);
      URL.revokeObjectURL(url);
      toast("success", "Event exported as .ics");
    } catch (err) {
      toast("error", err instanceof Error ? err.message : String(err));
    }
  };

  const handleDeleteEvent = useCallback((eventId: string) => {
    const eventToDelete = events.find(e => e.id === eventId);
    if (!eventToDelete) return;

    // Clear cache to keep it clean
    eventCacheRef.current.clear();

    setEvents(prev => prev.filter(e => e.id !== eventId));

    const timeoutId = setTimeout(async () => {
      try {
        await api.deleteCalendarEvent(eventId);
        setPendingDeletions(prev => {
          const next = { ...prev };
          delete next[eventId];
          return next;
        });
      } catch (err) {
        toast("error", err instanceof Error ? err.message : String(err));
        setEvents(prev => {
          if (prev.some(e => e.id === eventId)) return prev;
          return [...prev, eventToDelete].sort((a, b) => a.dtstart.localeCompare(b.dtstart));
        });
      }
    }, 8000);

    setPendingDeletions(prev => ({
      ...prev,
      [eventId]: { event: eventToDelete, timeoutId }
    }));

    sonnerToast.success(t("calendar.eventDeleted", "Event deleted"), {
      duration: 8000,
      action: {
        label: t("common.undo", "Undo"),
        onClick: () => {
          clearTimeout(timeoutId);
          setEvents(prev => {
            if (prev.some(e => e.id === eventId)) return prev;
            return [...prev, eventToDelete].sort((a, b) => a.dtstart.localeCompare(b.dtstart));
          });
          setPendingDeletions(prev => {
            const next = { ...prev };
            delete next[eventId];
            return next;
          });
          toast("info", t("calendar.deletionUndone", "Deletion undone"));
        }
      }
    });
  }, [events, t, toast]);

  const handleSelectEvent = (event: CalendarEvent, e?: React.SyntheticEvent) => {
    const native = e?.nativeEvent as MouseEvent;
    const isCmdClick = native && (native.metaKey || native.ctrlKey);
    if (isCmdClick) {
      setSelectedEventIds(prev => 
        prev.includes(event.id) 
          ? prev.filter(id => id !== event.id) 
          : [...prev, event.id]
      );
    } else {
      setSelectedEventIds([event.id]);
      if (onSelectEvent) onSelectEvent(event);
    }
  };

  const handleDeleteSelectedEvents = useCallback(() => {
    if (selectedEventIds.length === 0) return;
    const eventsToDelete = events.filter(e => selectedEventIds.includes(e.id));
    if (eventsToDelete.length === 0) return;

    // Clear cache
    eventCacheRef.current.clear();

    const ids = [...selectedEventIds];
    setSelectedEventIds([]);

    setEvents(prev => prev.filter(e => !ids.includes(e.id)));

    const timeoutId = setTimeout(async () => {
      try {
        setLoading(true);
        await Promise.all(ids.map(id => api.deleteCalendarEvent(id)));
        toast("success", t("calendar.bulkDeleted", { count: ids.length }));
        setPendingDeletions(prev => {
          const next = { ...prev };
          ids.forEach(id => delete next[id]);
          return next;
        });
        void reloadEvents();
      } catch (err) {
        toast("error", err instanceof Error ? err.message : String(err));
        setEvents(prev => {
          const toAdd = eventsToDelete.filter(e => !prev.some(x => x.id === e.id));
          return [...prev, ...toAdd].sort((a, b) => a.dtstart.localeCompare(b.dtstart));
        });
      } finally {
        setLoading(false);
      }
    }, 8000);

    setPendingDeletions(prev => {
      const next = { ...prev };
      eventsToDelete.forEach(e => {
        next[e.id] = { event: e, timeoutId };
      });
      return next;
    });

    sonnerToast.success(t("calendar.eventsDeleted", { count: ids.length }), {
      duration: 8000,
      action: {
        label: t("common.undo", "Undo"),
        onClick: () => {
          clearTimeout(timeoutId);
          setEvents(prev => {
            const toAdd = eventsToDelete.filter(e => !prev.some(x => x.id === e.id));
            return [...prev, ...toAdd].sort((a, b) => a.dtstart.localeCompare(b.dtstart));
          });
          setPendingDeletions(prev => {
            const next = { ...prev };
            ids.forEach(id => delete next[id]);
            return next;
          });
          toast("info", t("calendar.deletionUndone", "Deletion undone"));
        }
      }
    });
  }, [selectedEventIds, events, reloadEvents, t, toast]);

  const [quickAddPreview, setQuickAddPreview] = useState<any>(null);

  const parseQuickAdd = useCallback((text: string) => {
    if (!text.trim()) return null;
    const chronoAny: any = chrono;
    let parsed = chronoAny.ko ? chronoAny.ko.parse(text) : [];
    if (parsed.length === 0) {
      parsed = chrono.parse(text);
    }
    if (parsed.length === 0) return null;

    const first = parsed[0];
    let isAssumed = false;
    let start: Date;
    if (first.start.isCertain("hour")) {
      start = first.start.date();
    } else {
      const d = first.start.date();
      d.setHours(9, 0, 0, 0);
      start = d;
      isAssumed = true;
    }

    const end = first.end ? first.end.date() : new Date(start.getTime() + 60 * 60 * 1000);

    let remainingText = text.replace(first.text, "").replace(/\s+/g, " ").trim();
    let location: string | undefined;
    
    const locMatch = remainingText.match(/\b(at|in|에서|곳)\s+([^,.\n]+)/i);
    if (locMatch) {
      location = locMatch[2].trim();
      remainingText = remainingText.replace(locMatch[0], "").trim();
    }

    let summary = remainingText.trim();
    if (!summary) {
      summary = "Quick Added Event";
    }

    return {
      summary,
      start,
      end,
      location,
      isAssumed,
    };
  }, []);

  const formatSlotLabel = (ms: number): string => {
    return new Intl.DateTimeFormat(undefined, {
      weekday: "short",
      month: "short",
      day: "numeric",
      hour: "numeric",
      minute: "2-digit",
      hour12: false,
    }).format(new Date(ms));
  };

  useEffect(() => {
    if (!quickAddText.trim()) {
      setQuickAddPreview(null);
      return;
    }
    const timer = setTimeout(() => {
      const parsed = parseQuickAdd(quickAddText);
      if (parsed) {
        setQuickAddPreview({
          summary: parsed.summary,
          start: formatSlotLabel(parsed.start.getTime()),
          end: formatSlotLabel(parsed.end.getTime()),
          location: parsed.location,
          isAssumed: parsed.isAssumed,
        });
      } else {
        setQuickAddPreview(null);
      }
    }, 200);
    return () => clearTimeout(timer);
  }, [quickAddText, parseQuickAdd]);

  const handleQuickAdd = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!quickAddText.trim() || !effectiveAccountId) return;

    try {
      const parsed = parseQuickAdd(quickAddText);
      if (!parsed) {
        toast("warning", "Couldn't parse exact time, opening full form");
        if (onCreateEvent) {
          onCreateEvent(quickAddText);
        }
        setQuickAddText("");
        return;
      }

      const startIso = parsed.start.toISOString();
      const endIso = parsed.end.toISOString();
      const tz = Intl.DateTimeFormat().resolvedOptions().timeZone;

      await api.createCalendarEvent({
        account_id: effectiveAccountId,
        summary: parsed.summary,
        dtstart: startIso,
        dtend: endIso,
        all_day: false,
        time_zone: tz,
        location: parsed.location,
      });

      toast("success", `Event "${parsed.summary}" created!`);
      setQuickAddText("");
      await reloadEvents();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : String(err));
    }
  };

  const pendingDeletionsRef = useRef(pendingDeletions);
  useEffect(() => {
    pendingDeletionsRef.current = pendingDeletions;
  }, [pendingDeletions]);

  useEffect(() => {
    return () => {
      // Execute any pending deletes immediately on unmount
      Object.entries(pendingDeletionsRef.current).forEach(([id, d]) => {
        clearTimeout(d.timeoutId);
        void api.deleteCalendarEvent(id);
      });
    };
  }, []);

  useEffect(() => {
    if (effectiveAccountId) {
      void fetchEvents();
    }
  }, [currentDate, currentView, effectiveAccountId, fetchEvents]);

  useEffect(() => {
    let cancelled = false;
    let unlistenFn: (() => void) | undefined;
    const setup = async () => {
      const unlisten = await listen<number>("calendar-auto-declined", (event) => {
        const count = event.payload;
        toast("info", t("calendar.autoDeclined", "Auto-declined {{count}} conflicting events", { count }));
      });
      if (cancelled) {
        unlisten();
      } else {
        unlistenFn = unlisten;
      }
    };
    void setup();
    return () => {
      cancelled = true;
      if (unlistenFn) unlistenFn();
    };
  }, [toast, t]);

  useEffect(() => {
    let cancelled = false;
    let unlisten1: (() => void) | undefined;
    let unlisten2: (() => void) | undefined;
    let unlisten3: (() => void) | undefined;

    const setup = async () => {
      const u1 = await listen<{ event_id: string; rsvp_status: string; summary: string; error: string }>(
        "calendar-rsvp-reply-failed",
        ({ payload }) => {
          sonnerToast.error(
            t("calendar.rsvp.reply_failed_title", "Calendar RSVP send failed"),
            {
              description: t(
                "calendar.rsvp.reply_failed_description",
                "Your response to \"{{summary}}\" was saved locally but could not be sent to the organizer: {{error}}",
                { summary: payload.summary, error: payload.error },
              ),
            },
          );
        },
      );
      if (cancelled) {
        u1();
        return;
      }
      unlisten1 = u1;

      const u2 = await listen<{ event_id: string; rsvp_status: string; summary: string }>(
        "calendar-rsvp-reply-queued",
        ({ payload }) => {
          sonnerToast.info(
            t("calendar.rsvp.reply_queued_title", "Calendar RSVP queued"),
            {
              description: t(
                "calendar.rsvp.reply_queued_description",
                "Your response to \"{{summary}}\" will be sent when the network recovers.",
                { summary: payload.summary },
              ),
            },
          );
        },
      );
      if (cancelled) {
        u2();
        return;
      }
      unlisten2 = u2;

      const u3 = await listen<{ event_id: string }>(
        "calendar-rsvp-reply-drained",
        () => {
          sonnerToast.success(
            t("calendar.rsvp.reply_drained_title", "Calendar RSVP sent"),
            {
              description: t(
                "calendar.rsvp.reply_drained_description",
                "Your queued RSVP was sent successfully.",
              ),
            },
          );
        },
      );
      if (cancelled) {
        u3();
        return;
      }
      unlisten3 = u3;
    };

    void setup();
    return () => {
      cancelled = true;
      if (unlisten1) unlisten1();
      if (unlisten2) unlisten2();
      if (unlisten3) unlisten3();
    };
  }, [t]);

  return {
    events,
    calendars,
    categories,
    loading,
    syncing,
    reauthRequired,
    hideWeekends,
    weekStart,
    selectedTimezone,
    setSelectedTimezone,
    quickAddText,
    setQuickAddText,
    selectedEventIds,
    setSelectedEventIds,
    contextMenu,
    setContextMenu,
    fetchEvents,
    reloadEvents,
    loadPreferences,
    handleRefresh,
    toggleCalendar,
    handleDuplicateEvent,
    handleExportIcsFile,
    handleDeleteEvent,
    handleSelectEvent,
    handleDeleteSelectedEvents,
    handleQuickAdd,
    quickAddPreview,
    workingHoursStart,
    workingHoursEnd,
  };
}
