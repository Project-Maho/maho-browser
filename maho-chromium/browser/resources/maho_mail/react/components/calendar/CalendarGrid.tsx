import { useMemo, useState, useCallback, useEffect } from "react";
import { Calendar, dateFnsLocalizer, type Event as RbcEvent } from "react-big-calendar";
import dragAndDropModule from "react-big-calendar/lib/addons/dragAndDrop";
import { format, parse, startOfWeek, endOfWeek, getDay, addMonths, subMonths, addWeeks, subWeeks, addDays, subDays } from "date-fns";
import { enUS, ko } from "date-fns/locale";
import { useTranslation } from "react-i18next";
import { resolveDragAndDrop } from "./dragAndDropImport";
import { RefreshCw, Search, ChevronLeft, ChevronRight, X, Calendar as CalendarIcon, Printer, Lock, Repeat, Video, MapPin, Copy, Download, Globe, Trash2 } from "lucide-react";
import { Button } from "../ui/Button";
import { useToast } from "../ui/Toast";
import * as api from "../../api";
import type { AccountSummary, CalendarEvent } from "../../types";
import { toZonedTime } from "date-fns-tz";
import { MiniCalendar } from "./MiniCalendar";
import { YearView } from "./YearView";
import { useCalendarClient } from "../../hooks/useCalendarClient";
import { openExternalUrl } from "../../utils/openExternal.js";
import { TodayPanel } from "./TodayPanel";
import { MobileDayView } from "./MobileDayView";
import { Tooltip, TooltipTrigger, TooltipContent, TooltipProvider } from "../ui/Tooltip";
import {
  ContextMenu,
  ContextMenuTrigger,
  ContextMenuContent,
  ContextMenuItem,
  ContextMenuSeparator,
  ContextMenuShortcut,
  ContextMenuSub,
  ContextMenuSubContent,
  ContextMenuSubTrigger,
  ContextMenuRadioGroup,
  ContextMenuRadioItem,
} from "../ui/context-menu";
import "react-big-calendar/lib/css/react-big-calendar.css";
import "react-big-calendar/lib/addons/dragAndDrop/styles.css";
import "./calendar-grid.css";

const locales = { "en-US": enUS, ko };

const getGoogleCalendarUrl = (externalId: string, calendarId: string) => {
  try {
    const raw = `${externalId} ${calendarId}`;
    const encoded = btoa(raw).replace(/=/g, "");
    return `https://calendar.google.com/calendar/event?eid=${encoded}`;
  } catch (e) {
    return `https://calendar.google.com/calendar/r/event`;
  }
};

const getAttendeeSummary = (attendeesJson?: string | null) => {
  if (!attendeesJson) return null;
  try {
    const list = JSON.parse(attendeesJson);
    if (!Array.isArray(list) || list.length === 0) return null;
    let accepted = 0;
    let declined = 0;
    let pending = 0;
    list.forEach((att: any) => {
      const status = att.responseStatus || att.response_status || "";
      if (status === "accepted") accepted++;
      else if (status === "declined") declined++;
      else pending++;
    });
    if (accepted === 0 && declined === 0 && pending === 0) return null;
    return { accepted, declined, pending };
  } catch {
    return null;
  }
};

const localizer = dateFnsLocalizer({
  format,
  parse,
  startOfWeek: (date: Date) => {
    const ws = localStorage.getItem("maho-calendar.week_start") || "0";
    const startOn = Number(ws) as 0 | 1 | 6;
    return startOfWeek(date, { weekStartsOn: startOn });
  },
  getDay,
  locales,
});

const DragAndDropCalendar = resolveDragAndDrop(dragAndDropModule)(Calendar);

interface CalendarGridProps {
  accountId: string;
  accounts?: AccountSummary[];
  onSelectEvent: (event: CalendarEvent) => void;
  onCreateEvent: (initialSummary?: string) => void;
  onReauthorize?: () => void;
}

interface RbcCalendarEvent extends RbcEvent {
  resource: CalendarEvent;
}

function toRbcEvent(e: CalendarEvent, timezone: string): RbcCalendarEvent {
  let start: Date;
  let end: Date;

  if (e.all_day) {
    const [y, m, d] = e.dtstart.split("-").map(Number);
    start = new Date(y, m - 1, d);
    if (e.dtend) {
      const [ey, em, ed] = e.dtend.split("-").map(Number);
      end = new Date(ey, em - 1, ed);
    } else {
      end = new Date(y, m - 1, d + 1);
    }
  } else {
    start = toZonedTime(new Date(e.dtstart), timezone);
    end = e.dtend ? toZonedTime(new Date(e.dtend), timezone) : start;
  }

  return {
    title: e.summary || "(No title)",
    start,
    end,
    allDay: e.all_day,
    resource: e,
  };
}

function roundTo15Minutes(date: Date): Date {
  const rounded = new Date(date);
  const minutes = rounded.getMinutes();
  const roundedMinutes = Math.round(minutes / 15) * 15;
  rounded.setMinutes(roundedMinutes, 0, 0);
  return rounded;
}

function readableTextColor(bg: string): string {
  const hex = bg.replace("#", "");
  if (hex.length !== 6) return "#fff";
  const r = parseInt(hex.slice(0, 2), 16);
  const g = parseInt(hex.slice(2, 4), 16);
  const b = parseInt(hex.slice(4, 6), 16);
  const luminance = (0.299 * r + 0.587 * g + 0.114 * b) / 255;
  return luminance > 0.6 ? "#1f2937" : "#ffffff";
}

export function CalendarGrid({ accountId, accounts, onSelectEvent, onCreateEvent, onReauthorize }: CalendarGridProps) {
  const { t, i18n } = useTranslation();
  const { toast } = useToast();

  const [view, setView] = useState<"month" | "week" | "day" | "agenda" | "year">("month");
  const [date, setDate] = useState(new Date());

  const {
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
  } = useCalendarClient({
    accountId,
    accounts,
    onSelectEvent,
    onCreateEvent,
    currentDate: date,
    currentView: view,
  });

  const [isMobile, setIsMobile] = useState(false);
  const [altKeyPressed, setAltKeyPressed] = useState(false);

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === "Alt") setAltKeyPressed(true);

      const target = e.target as HTMLElement;
      if (target.tagName === "INPUT" || target.tagName === "TEXTAREA" || target.isContentEditable) {
        return;
      }

      if (selectedEventIds.length > 0) {
        const lastSelectedId = selectedEventIds[selectedEventIds.length - 1];
        const lastEvent = events.find((ev) => ev.id === lastSelectedId);
        
        if (e.key === "d" || e.key === "D") {
          e.preventDefault();
          if (lastEvent) {
            void handleDeleteEvent(lastSelectedId);
          }
        } else if (e.key === "c" || e.key === "C") {
          e.preventDefault();
          if (lastEvent) {
            void handleDuplicateEvent(lastSelectedId);
          }
        } else if (e.key === "e" || e.key === "E") {
          e.preventDefault();
          if (lastEvent) {
            onSelectEvent(lastEvent);
          }
        }
      }
    };
    const handleKeyUp = (e: KeyboardEvent) => {
      if (e.key === "Alt") setAltKeyPressed(false);
    };
    window.addEventListener("keydown", handleKeyDown);
    window.addEventListener("keyup", handleKeyUp);
    return () => {
      window.removeEventListener("keydown", handleKeyDown);
      window.removeEventListener("keyup", handleKeyUp);
    };
  }, [selectedEventIds, events, handleDeleteEvent, handleDuplicateEvent, onSelectEvent]);

  useEffect(() => {
    const checkMobile = () => setIsMobile(window.innerWidth < 640);
    checkMobile();
    window.addEventListener("resize", checkMobile);
    return () => window.removeEventListener("resize", checkMobile);
  }, []);

  const handleNavigate = useCallback((action: "PREV" | "NEXT" | "TODAY") => {
    if (action === "TODAY") {
      setDate(new Date());
      return;
    }

    setDate((prev) => {
      switch (view) {
        case "month":
        case "agenda":
          return action === "PREV" ? subMonths(prev, 1) : addMonths(prev, 1);
        case "week":
          return action === "PREV" ? subWeeks(prev, 1) : addWeeks(prev, 1);
        case "day":
          return action === "PREV" ? subDays(prev, 1) : addDays(prev, 1);
        case "year":
          const yearDiff = action === "PREV" ? -1 : 1;
          const nextYear = new Date(prev);
          nextYear.setFullYear(nextYear.getFullYear() + yearDiff);
          return nextYear;
        default:
          return prev;
      }
    });
  }, [view]);

  // Search states
  const [searchQuery, setSearchQuery] = useState("");
  const [searchResults, setSearchResults] = useState<CalendarEvent[]>([]);
  const [searchFocused, setSearchFocused] = useState(false);

  const gmailAccounts = useMemo(
    () => (accounts ?? []).filter((a) => a.auth_type === "oauth2_gmail" || a.provider === "gmail"),
    [accounts],
  );
  const effectiveAccountId = accountId || gmailAccounts[0]?.id || "";

  useEffect(() => {
    void fetchEvents();
    void loadPreferences();
  }, [fetchEvents, loadPreferences, effectiveAccountId]);

  useEffect(() => {
    const handler = (e: Event) => {
      const detail = (e as CustomEvent).detail;
      if (typeof detail === "string" && detail !== effectiveAccountId) return;
      // Another surface mutated calendar state, so the TTL cache is stale.
      void reloadEvents();
    };
    window.addEventListener("calendar-events-changed", handler);
    return () => window.removeEventListener("calendar-events-changed", handler);
  }, [effectiveAccountId, reloadEvents]);

  useEffect(() => {
    const handler = () => {
      void loadPreferences();
      void fetchEvents();
    };
    window.addEventListener("calendar-preferences-changed", handler);
    return () => window.removeEventListener("calendar-preferences-changed", handler);
  }, [loadPreferences, fetchEvents]);

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (document.activeElement?.tagName === "INPUT" || document.activeElement?.tagName === "TEXTAREA") {
        return;
      }
      
      switch (e.key.toLowerCase()) {
        case "j":
          handleNavigate("NEXT");
          break;
        case "k":
          handleNavigate("PREV");
          break;
        case "t":
          handleNavigate("TODAY");
          break;
        case "n":
          onCreateEvent();
          break;
        case "m":
          setView("month");
          break;
        case "w":
          setView("week");
          break;
        case "d":
          if (selectedEventIds.length === 0) setView("day");
          break;
        case "a":
          setView("agenda");
          break;
        case "y":
          setView("year");
          break;
        default:
          break;
      }
    };
    
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [handleNavigate, onCreateEvent, selectedEventIds]);

  const handleSearchChange = useCallback(async (val: string) => {
    setSearchQuery(val);
    if (!val.trim()) {
      setSearchResults([]);
      return;
    }
    try {
      const results = await api.searchCalendarEvents(effectiveAccountId, val);
      setSearchResults(results);
    } catch (err) {
      console.error(err);
    }
  }, [effectiveAccountId]);

  const rbcEvents = useMemo(() => events.map(e => toRbcEvent(e, selectedTimezone)), [events, selectedTimezone]);

  const eventPropGetter = useCallback((event: RbcEvent) => {
    const resource = (event as RbcCalendarEvent).resource;
    let bg = resource.color || resource.calendar_color || "#3b82f6";
    if (resource.category) {
      const match = categories.find(c => c.name.toLowerCase() === resource.category?.toLowerCase());
      if (match) {
        bg = match.color;
      }
    }

    const isSelected = selectedEventIds.includes(resource.id);
    const style: React.CSSProperties = {
      outline: isSelected ? "3px solid var(--primary)" : undefined,
      outlineOffset: isSelected ? "1.5px" : undefined,
      zIndex: isSelected ? 15 : undefined,
    };

    if (resource.event_type === "focusTime") {
      style.backgroundColor = "rgba(139, 92, 246, 0.15)";
      style.color = "#7c3aed";
      style.borderLeft = "4px solid #7c3aed";
      style.borderColor = "rgba(139, 92, 246, 0.3)";
    } else if (resource.event_type === "outOfOffice") {
      style.background = "linear-gradient(135deg, #ffedd5 0%, #fed7aa 100%)";
      style.color = "#c2410c";
      style.borderLeft = "4px solid #ea580c";
      style.borderColor = "#fdba74";
    } else {
      style.backgroundColor = bg;
      style.color = readableTextColor(bg);
      style.borderColor = bg;
    }

    return { style };
  }, [categories, selectedEventIds]);

  const slotPropGetter = useCallback((slotDate: Date) => {
    try {
      const hour = slotDate.getHours();
      const startParts = workingHoursStart.split(":");
      const endParts = workingHoursEnd.split(":");
      const startHour = startParts.length > 0 ? Number(startParts[0]) : 9;
      const endHour = endParts.length > 0 ? Number(endParts[0]) : 18;

      if (hour < startHour || hour >= endHour) {
        return {
          className: "non-work-hour",
          style: {
            backgroundColor: "rgba(0, 0, 0, 0.035)",
          }
        };
      }
    } catch (e) {
      console.error("Error in slotPropGetter", e);
    }
    return {};
  }, [workingHoursStart, workingHoursEnd]);

  type DropArgs = {
    event: RbcEvent;
    start: Date | string;
    end: Date | string;
    isAllDay?: boolean;
  };

  const handleEventDrop = useCallback(async ({ event, start, end, isAllDay }: DropArgs) => {
    const resource = (event as RbcCalendarEvent).resource;
    let dtstartStr = "";
    let dtendStr: string | undefined = undefined;

    const allDay = !!isAllDay;
    if (allDay) {
      const startDate = new Date(start);
      const endDate = new Date(end);
      dtstartStr = `${startDate.getFullYear()}-${String(startDate.getMonth() + 1).padStart(2, "0")}-${String(startDate.getDate()).padStart(2, "0")}`;
      dtendStr = `${endDate.getFullYear()}-${String(endDate.getMonth() + 1).padStart(2, "0")}-${String(endDate.getDate()).padStart(2, "0")}`;
    } else {
      dtstartStr = roundTo15Minutes(new Date(start)).toISOString();
      dtendStr = roundTo15Minutes(new Date(end)).toISOString();
    }

    try {
      if (altKeyPressed) {
        await api.createCalendarEvent({
          account_id: resource.account_id,
          summary: resource.summary,
          description: resource.description || undefined,
          dtstart: dtstartStr,
          dtend: dtendStr,
          location: resource.location || undefined,
          all_day: allDay,
          color: resource.color || undefined,
          time_zone: allDay ? undefined : selectedTimezone,
          calendar_id: resource.google_calendar_id || undefined,
          category: resource.category || undefined,
          travel_time_minutes: resource.travel_time_minutes || undefined,
          event_type: resource.event_type || undefined,
        });
        toast("success", "Event duplicated via drag!");
      } else {
        await api.updateCalendarEvent({
          event_id: resource.id,
          summary: resource.summary,
          description: resource.description || undefined,
          location: resource.location || undefined,
          dtstart: dtstartStr,
          dtend: dtendStr,
          all_day: allDay,
          time_zone: allDay ? undefined : selectedTimezone,
        });
        toast("success", t("calendar.eventUpdated"));
      }
      await reloadEvents();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : String(err));
    }
  }, [reloadEvents, t, toast, altKeyPressed, selectedTimezone]);

  type ResizeArgs = {
    event: RbcEvent;
    start: Date | string;
    end: Date | string;
  };

  const handleEventResize = useCallback(async ({ event, start, end }: ResizeArgs) => {
    const resource = (event as RbcCalendarEvent).resource;
    try {
      await api.updateCalendarEvent({
        event_id: resource.id,
        summary: resource.summary,
        description: resource.description || undefined,
        location: resource.location || undefined,
        dtstart: roundTo15Minutes(new Date(start)).toISOString(),
        dtend: roundTo15Minutes(new Date(end)).toISOString(),
        all_day: resource.all_day,
        time_zone: resource.all_day ? undefined : selectedTimezone,
      });
      toast("success", t("calendar.eventUpdated"));
      await reloadEvents();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : String(err));
    }
  }, [reloadEvents, t, toast, selectedTimezone]);

  const handleOpenInGoogleCalendar = async (externalId: string, calendarId: string) => {
    try {
      const url = getGoogleCalendarUrl(externalId, calendarId);
      openExternalUrl(url);
    } catch (err) {
      console.error("Failed to open external url", err);
    }
  };

  const handleExportSelectedEvents = async () => {
    if (selectedEventIds.length === 0) return;
    try {
      const parts: string[] = [];
      for (const id of selectedEventIds) {
        const ev = events.find(e => e.id === id);
        if (ev) {
          const calendarIds = ev.google_calendar_id ? [ev.google_calendar_id] : undefined;
          const icsData = await api.exportCalendarIcs(ev.account_id, calendarIds, ev.dtstart, ev.dtstart);
          const startIdx = icsData.indexOf("BEGIN:VEVENT");
          const endIdx = icsData.lastIndexOf("END:VEVENT");
          if (startIdx !== -1 && endIdx !== -1) {
            parts.push(icsData.substring(startIdx, endIdx + 10));
          }
        }
      }
      if (parts.length === 0) {
        toast("warning", "No events found to export");
        return;
      }
      const combined = [
        "BEGIN:VCALENDAR",
        "VERSION:2.0",
        "PRODID:-//Maho Mail//Calendar//EN",
        ...parts,
        "END:VCALENDAR"
      ].join("\r\n");

      const blob = new Blob([combined], { type: "text/calendar;charset=utf-8" });
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");
      link.href = url;
      link.setAttribute("download", `exported_events.ics`);
      document.body.appendChild(link);
      link.click();
      document.body.removeChild(link);
      URL.revokeObjectURL(url);
      toast("success", `Exported ${selectedEventIds.length} events as .ics`);
      setSelectedEventIds([]);
    } catch (err) {
      toast("error", err instanceof Error ? err.message : String(err));
    }
  };



  const toolbarLabel = useMemo(() => {
    const culture = i18n.language === "ko" ? "ko" : "en-US";
    const loc = culture === "ko" ? ko : enUS;

    switch (view) {
      case "month":
      case "agenda":
        return format(date, "MMMM yyyy", { locale: loc });
      case "week":
        return `${format(startOfWeek(date), "MMM d")} - ${format(endOfWeek(date), "MMM d, yyyy", { locale: loc })}`;
      case "day":
        return format(date, "MMMM d, yyyy", { locale: loc });
      case "year":
        return format(date, "yyyy");
      default:
        return "";
    }
  }, [date, view, i18n.language]);

  const culture = i18n.language === "ko" ? "ko" : "en-US";

  return (
    <div className="flex h-full w-full min-w-0 flex-1 flex-row overflow-hidden bg-background">
      {/* Left panel for Mini Calendar and Filters */}
      <div className="hidden lg:flex w-60 flex-shrink-0 flex-col border-r border-border bg-card/15 p-4 space-y-6 overflow-y-auto">
        <MiniCalendar value={date} onChange={setDate} events={events} />
        
        {calendars.length > 0 && (
          <div className="space-y-4 pt-2 border-t border-border/60">
            <span className="text-[11px] font-semibold text-muted-foreground uppercase tracking-wider">{t("calendar.filter.label")}</span>
            <div className="space-y-3">
              {(accounts || []).map((acc) => {
                const accCals = calendars.filter(c => c.account_id === acc.id);
                if (accCals.length === 0) return null;
                const isReauth = reauthRequired && acc.id === accountId;

                return (
                  <div key={acc.id} className="space-y-1">
                    <div className="flex items-center justify-between px-1 py-0.5">
                      <span className="text-[10px] font-semibold text-muted-foreground truncate max-w-[150px]">
                        {acc.email}
                      </span>
                      {isReauth && (
                        <span className="text-[8px] bg-amber-500/10 text-amber-600 border border-amber-500/30 px-1 rounded font-semibold animate-pulse shrink-0">
                          Reauth Required
                        </span>
                      )}
                    </div>
                    <div className="flex flex-col gap-0.5 pl-1">
                      {accCals.map((cal) => {
                        const color = cal.background_color || "#3b82f6";
                        const active = cal.visible;
                        const calCal = calendars.find(c => c.calendar_id === cal.calendar_id);
                        const isReadOnly = calCal ? !["owner", "writer"].includes(calCal.access_role || "") : false;

                        return (
                          <button
                            key={cal.id}
                            type="button"
                            onClick={() => void toggleCalendar(cal)}
                            className={`flex items-center gap-2 w-full text-left px-2 py-1.5 rounded-lg text-xs transition-colors hover:bg-surface-hover ${
                              active ? "text-foreground font-medium" : "text-muted-foreground line-through opacity-60"
                            }`}
                            title={cal.calendar_id}
                          >
                            <span
                              className="inline-block h-3 w-3 rounded-md flex-shrink-0"
                              style={{ backgroundColor: active ? color : "transparent", border: `2px solid ${color}` }}
                            />
                            <span className="truncate flex-1">{cal.summary}</span>
                            {isReadOnly && (
                              <Lock size={11} className="shrink-0 text-muted-foreground opacity-60" aria-label="Read-only" />
                            )}
                          </button>
                        );
                      })}
                    </div>
                  </div>
                );
              })}
            </div>
          </div>
        )}

        {/* Legend */}
        <div className="pt-2 border-t border-border/60 text-[10px] text-muted-foreground flex flex-wrap gap-x-2 gap-y-1">
          <span>○ Meeting</span>
          <span>⧗ Focus Time</span>
          <span>◇ Out of Office</span>
        </div>

        {/* Today Panel */}
        <TodayPanel events={events} onNavigateDate={setDate} />
      </div>

      {/* Main content grid */}
      <div className="flex flex-col flex-1 min-w-0 h-full overflow-hidden">
        {/* Header row */}
        <div className="flex items-center justify-between border-b border-border px-4 py-3 flex-shrink-0">
          <h2 className="text-sm font-semibold text-foreground flex items-center gap-1.5">
            <CalendarIcon size={16} className="text-primary" />
            {t("calendar.title")}
          </h2>
          <div className="flex items-center gap-2">
            {/* Search Input Box */}
            <div className="relative w-48 sm:w-64">
              <Search className="absolute left-2.5 top-1/2 -translate-y-1/2 text-muted-foreground" size={14} />
              <input
                type="text"
                placeholder={t("calendar.searchPlaceholder") || "Search events..."}
                value={searchQuery}
                onChange={(e) => void handleSearchChange(e.target.value)}
                onFocus={() => setSearchFocused(true)}
                onBlur={() => setTimeout(() => setSearchFocused(false), 200)}
                className="w-full pl-8 pr-8 py-1.5 text-xs bg-muted/65 hover:bg-surface-hover text-foreground border border-border rounded-xl focus:outline-none focus:ring-1 focus:ring-primary focus:border-primary"
              />
              {searchQuery && (
                <button
                  type="button"
                  onClick={() => {
                    setSearchQuery("");
                    setSearchResults([]);
                  }}
                  className="absolute right-2.5 top-1/2 -translate-y-1/2 text-muted-foreground hover:text-foreground"
                >
                  <X size={14} />
                </button>
              )}

              {searchFocused && searchResults.length > 0 && (
                <div className="absolute top-full left-0 right-0 mt-1 max-h-60 overflow-y-auto border border-border bg-popover text-popover-foreground rounded-lg shadow-lg z-50 p-1">
                  {searchResults.map((ev) => (
                    <button
                      key={ev.id}
                      type="button"
                      onMouseDown={() => {
                        const d = new Date(ev.dtstart);
                        setDate(d);
                        onSelectEvent(ev);
                        setSearchQuery("");
                        setSearchResults([]);
                      }}
                      className="w-full text-left px-3 py-2 text-xs rounded-md hover:bg-surface-hover transition-colors flex flex-col gap-0.5"
                    >
                      <span className="font-semibold text-foreground truncate">{ev.summary || "(No title)"}</span>
                      <span className="text-[10px] text-muted-foreground">
                        {ev.dtstart.substring(0, 10)}
                        {ev.location ? ` @ ${ev.location}` : ""}
                      </span>
                    </button>
                  ))}
                </div>
              )}
            </div>

            <Button
              variant="ghost"
              size="sm"
              onClick={() => window.print()}
              title={t("calendar.print") || "Print calendar"}
              aria-label={t("calendar.print") || "Print calendar"}
            >
              <Printer size={15} />
            </Button>
            <Button
              variant="ghost"
              size="sm"
              onClick={() => void handleRefresh()}
              disabled={syncing}
              aria-label={t("calendar.refresh")}
              title={t("calendar.refresh")}
            >
              <RefreshCw size={15} className={syncing ? "animate-spin" : ""} />
            </Button>
            <Button variant="default" size="sm" onClick={() => onCreateEvent()}>
              + {t("calendar.createEvent")}
            </Button>
          </div>
        </div>

        {/* Custom Navigation Toolbar */}
        <div className="flex flex-wrap items-center justify-between px-4 py-2 border-b border-border/45 bg-card/5 gap-2">
          <div className="flex items-center gap-1">
            <Button variant="ghost" size="sm" onClick={() => handleNavigate("TODAY")}>
              {t("calendar.today") || "Today"}
            </Button>
            <button
              type="button"
              onClick={() => handleNavigate("PREV")}
              className="mail-pressable flex h-8 w-8 items-center justify-center rounded-md text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
            >
              <ChevronLeft size={16} />
            </button>
            <button
              type="button"
              onClick={() => handleNavigate("NEXT")}
              className="mail-pressable flex h-8 w-8 items-center justify-center rounded-md text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
            >
              <ChevronRight size={16} />
            </button>
            <span className="text-sm font-semibold text-foreground ml-2">
              {toolbarLabel}
            </span>
          </div>

          {/* Quick Add, Timezone, and Multi-select controls */}
          <div className="flex items-center flex-wrap gap-2 flex-1 max-w-xl justify-end">
            {selectedEventIds.length > 0 && (
              <div className="flex items-center gap-2 pr-2 border-r border-border/60">
                <span className="text-[10px] bg-primary/10 text-primary px-2 py-1 rounded font-semibold">
                  {selectedEventIds.length} selected
                </span>
                 <Button
                  variant="destructive"
                  size="sm"
                  onClick={handleDeleteSelectedEvents}
                  className="h-8 px-2 text-[10px]"
                >
                  Delete Selected
                </Button>
                {calendars.length > 1 && (
                  <select
                    onChange={async (e) => {
                      const newCalId = e.target.value;
                      if (newCalId) {
                        try {
                          await Promise.all(selectedEventIds.map(async (id) => {
                            const ev = events.find(x => x.id === id);
                            if (ev) {
                              await api.updateCalendarEvent({
                                event_id: ev.id,
                                summary: ev.summary,
                                dtstart: ev.dtstart,
                                all_day: ev.all_day,
                                target_calendar_id: newCalId,
                              });
                            }
                          }));
                          toast("success", `Moved ${selectedEventIds.length} events successfully!`);
                          void reloadEvents();
                          setSelectedEventIds([]);
                        } catch (err) {
                          toast("error", err instanceof Error ? err.message : String(err));
                        }
                      }
                      e.target.value = "";
                    }}
                    className="h-8 rounded-lg border border-input bg-background/50 px-2 text-[10px] focus:outline-none focus:ring-1 focus:ring-ring"
                  >
                    <option value="">Move selected to...</option>
                    {calendars.map(cal => (
                      <option key={cal.id} value={cal.calendar_id}>
                        {cal.summary}
                      </option>
                    ))}
                  </select>
                )}
                <Button
                  variant="outline"
                  size="sm"
                  onClick={() => void handleExportSelectedEvents()}
                  className="h-8 px-2 text-[10px]"
                >
                  Export .ics
                </Button>
                <Button
                  variant="outline"
                  size="sm"
                  onClick={() => setSelectedEventIds([])}
                  className="h-8 px-2 text-[10px]"
                >
                  Deselect
                </Button>
              </div>
            )}

            <div className="flex flex-col gap-1 max-w-xs w-full">
              <form onSubmit={handleQuickAdd} className="flex items-center gap-1.5 w-full">
                <input
                  type="text"
                  value={quickAddText}
                  onChange={(e) => setQuickAddText(e.target.value)}
                  placeholder="Quick add: 'Dinner tomorrow at 7pm'"
                  className="flex h-8 w-full rounded-lg border border-input bg-transparent px-3 py-1 text-xs shadow-sm transition-colors placeholder:text-muted-foreground focus:outline-none focus:ring-1 focus:ring-ring"
                />
                <Button type="submit" size="sm" className="h-8 px-3 text-xs shrink-0 bg-primary/15 hover:bg-primary/25 border border-primary/35 text-foreground hover:text-foreground">
                  Add
                </Button>
              </form>
              {quickAddPreview && (
                <div className="text-[10px] text-muted-foreground truncate max-w-xs px-1">
                  Preview: <span className="font-medium text-foreground">{quickAddPreview.summary}</span> · {quickAddPreview.start} · {quickAddPreview.end}
                  {quickAddPreview.location && ` · ${quickAddPreview.location}`}
                  {quickAddPreview.isAssumed && <span className="ml-1 text-amber-500 font-semibold">(assumed time)</span>}
                </div>
              )}
            </div>

            <div className="flex items-center gap-1">
              <select
                value={selectedTimezone}
                onChange={(e) => setSelectedTimezone(e.target.value)}
                className="h-8 rounded-lg border border-input bg-background/50 px-2 text-xs focus:outline-none focus:ring-1 focus:ring-ring"
              >
                <option value={Intl.DateTimeFormat().resolvedOptions().timeZone}>Local ({Intl.DateTimeFormat().resolvedOptions().timeZone.split("/").pop()})</option>
                <option value="UTC">UTC</option>
                <option value="America/New_York">New York (EST/EDT)</option>
                <option value="Europe/London">London (GMT/BST)</option>
                <option value="Asia/Tokyo">Tokyo (JST)</option>
                <option value="Asia/Seoul">Seoul (KST)</option>
              </select>
            </div>
          </div>

          {isMobile ? (
            <div className="flex items-center border border-border rounded-xl overflow-hidden p-0.5 bg-muted/40">
              {(["day", "agenda"] as const).map((v) => (
                <button
                  key={v}
                  type="button"
                  onClick={() => setView(v)}
                  className={`px-3 py-1 text-xs font-medium rounded-lg transition-colors ${
                    view === v || (v === "day" && view !== "agenda")
                      ? "bg-background text-foreground shadow-sm"
                      : "text-muted-foreground hover:text-foreground hover:bg-surface-hover"
                  }`}
                >
                  {v === "day" ? "Day" : "Agenda"}
                </button>
              ))}
            </div>
          ) : (
            <div className="flex items-center border border-border rounded-xl overflow-hidden p-0.5 bg-muted/40">
              {(["month", "week", "day", "agenda", "year"] as const).map((v) => (
                <button
                  key={v}
                  type="button"
                  onClick={() => setView(v)}
                  className={`px-3 py-1 text-xs font-medium rounded-lg transition-colors ${
                    view === v
                      ? "bg-background text-foreground shadow-sm"
                      : "text-muted-foreground hover:text-foreground hover:bg-surface-hover"
                  }`}
                >
                  {t(`calendar.view.${v}`) || v.charAt(0).toUpperCase() + v.slice(1)}
                </button>
              ))}
            </div>
          )}
        </div>

        {reauthRequired && (
          <div className="border-b border-border bg-amber-500/10 px-4 py-3 flex-shrink-0">
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
        )}

        {/* Content area: Grid or Year View */}
        <div className="flex-1 overflow-hidden relative">
          {view === "year" && !isMobile ? (
            <YearView
              date={date}
              onNavigate={setDate}
              onViewChange={setView}
              events={events}
            />
          ) : isMobile && view !== "agenda" ? (
            <MobileDayView
              date={date}
              onNavigate={setDate}
              events={events}
              onSelectEvent={(ev) => onSelectEvent?.(ev)}
            />
          ) : (
            <div className={`maho-rbc-wrap relative h-full p-3 ${hideWeekends ? 'calendar-hide-weekends' : ''} week-start-${weekStart}`}>
              <DragAndDropCalendar
                key={`${view}-${date.toISOString()}-${weekStart}-${hideWeekends}`}
                localizer={localizer}
                culture={culture}
                events={rbcEvents}
                startAccessor={(event: any) => new Date(event.start)}
                endAccessor={(event: any) => new Date(event.end)}
                view={isMobile ? "agenda" : view as any}
                onView={(v) => { if (!isMobile) setView(v as any); }}
                date={date}
                onNavigate={setDate}
                views={["month", "week", "day", "agenda"]}
                style={{ height: "100%" }}
                onSelectEvent={(rbcEvent, e) => handleSelectEvent((rbcEvent as RbcCalendarEvent).resource, e)}
                eventPropGetter={eventPropGetter}
                slotPropGetter={slotPropGetter}
                onEventDrop={handleEventDrop}
                onEventResize={handleEventResize}
                resizable
                selectable
                step={15}
                timeslots={4}
                toolbar={false}
                components={{
                  event: ({ event }: any) => {
                    const resource = (event as RbcCalendarEvent).resource;
                    const isRecurring = !!resource.recurrence_rule || !!resource.recurring_event_id;
                    const calCal = calendars.find(c => c.calendar_id === resource.google_calendar_id);
                    const isReadOnly = calCal ? !["owner", "writer"].includes(calCal.access_role || "") : false;
                    const attSummary = getAttendeeSummary(resource.attendees_json);
                    const isBulk = selectedEventIds.includes(resource.id) && selectedEventIds.length > 1;

                    return (
                      <ContextMenu>
                        <ContextMenuTrigger asChild>
                          <div className={`w-full h-full truncate focus:outline-none ${isReadOnly ? "opacity-80" : ""}`}>
                            <TooltipProvider delayDuration={300}>
                              <Tooltip>
                                <TooltipTrigger asChild>
                                  <div className="w-full h-full truncate text-xs font-medium flex items-center justify-between gap-1">
                                    <span className="truncate flex items-center gap-1">
                                      {isReadOnly && <Lock size={11} className="shrink-0" aria-label="Read-only" />}
                                      {resource.event_type === "outOfOffice" && <span className="text-[9px] font-bold text-orange-700 bg-orange-100 px-0.5 rounded shrink-0">OOO</span>}
                                      {resource.event_type === "focusTime" && <span className="text-[9px] font-bold text-purple-700 bg-purple-100 px-0.5 rounded shrink-0">Focus</span>}
                                      {event.title}
                                    </span>
                                    <div className="flex items-center gap-1 shrink-0">
                                      {isRecurring && (
                                        <Repeat size={11} className="opacity-75" aria-label="Recurring event" />
                                      )}
                                      {resource.hangout_link && (
                                        <Video size={11} className="opacity-75" aria-label="Has Meet Link" />
                                      )}
                                      {attSummary && (
                                        <span className="text-[9px] bg-muted/60 px-1 rounded select-none font-semibold text-muted-foreground" title={`Accepted: ${attSummary.accepted}, Declined: ${attSummary.declined}, Pending: ${attSummary.pending}`}>
                                          ✓{attSummary.accepted} ✗{attSummary.declined} ?{attSummary.pending}
                                        </span>
                                      )}
                                    </div>
                                  </div>
                                </TooltipTrigger>
                                <TooltipContent side="top" className="p-3 max-w-sm space-y-1 bg-popover text-popover-foreground border border-border shadow-md rounded-md z-50">
                                  <p className="font-semibold text-sm">{resource.summary || "(No title)"}</p>
                                  <p className="text-xs text-muted-foreground">
                                    {resource.dtstart.substring(0, 10)}
                                    {resource.dtend && ` - ${resource.dtend.substring(0, 10)}`}
                                  </p>
                                  {resource.location && (
                                    <p className="text-xs flex items-center gap-1 text-muted-foreground">
                                      <MapPin size={12} className="shrink-0" aria-hidden="true" /> {resource.location}
                                    </p>
                                  )}
                                  {resource.description && (
                                    <p className="text-xs text-muted-foreground border-t border-border/40 pt-1 mt-1 max-h-24 overflow-y-auto whitespace-pre-line">
                                      {resource.description.slice(0, 200)}
                                      {resource.description.length > 200 && "..."}
                                    </p>
                                  )}
                                </TooltipContent>
                              </Tooltip>
                            </TooltipProvider>
                          </div>
                        </ContextMenuTrigger>
                        <ContextMenuContent className="w-48 no-print z-[150]">
                          {!isBulk ? (
                            <>
                              <ContextMenuItem onClick={() => onSelectEvent(resource)}>
                                View Details
                              </ContextMenuItem>
                              <ContextMenuItem onClick={() => onSelectEvent(resource)} disabled={isReadOnly}>
                                Edit Event
                                <ContextMenuShortcut>E</ContextMenuShortcut>
                              </ContextMenuItem>
                              <ContextMenuItem onClick={() => void handleDuplicateEvent(resource.id)} disabled={isReadOnly}>
                                <Copy size={14} className="mr-2" aria-hidden="true" />Duplicate
                                <ContextMenuShortcut>C</ContextMenuShortcut>
                              </ContextMenuItem>
                              <ContextMenuItem onClick={() => void handleExportIcsFile(resource)}>
                                <Download size={14} className="mr-2" aria-hidden="true" />Export .ics
                              </ContextMenuItem>
                              {resource.uid && (
                                <ContextMenuItem onClick={() => void handleOpenInGoogleCalendar(resource.uid, resource.google_calendar_id || "")}>
                                  <Globe size={14} className="mr-2" aria-hidden="true" />Open in Google
                                </ContextMenuItem>
                              )}
                              <ContextMenuSeparator />
                              <ContextMenuItem onClick={() => void handleDeleteEvent(resource.id)} disabled={isReadOnly} className="text-destructive focus:bg-destructive/10 focus:text-destructive">
                                <Trash2 size={14} className="mr-2" aria-hidden="true" />Delete
                                <ContextMenuShortcut>D</ContextMenuShortcut>
                              </ContextMenuItem>

                              {calendars.length > 1 && (
                                <>
                                  <ContextMenuSeparator />
                                  <ContextMenuSub>
                                    <ContextMenuSubTrigger>Change Calendar</ContextMenuSubTrigger>
                                    <ContextMenuSubContent className="w-48 z-[150]">
                                      <ContextMenuRadioGroup
                                        value={resource.google_calendar_id || ""}
                                        onValueChange={async (newCalId) => {
                                          if (newCalId && newCalId !== resource.google_calendar_id) {
                                            try {
                                              await api.updateCalendarEvent({
                                                event_id: resource.id,
                                                summary: resource.summary,
                                                dtstart: resource.dtstart,
                                                all_day: resource.all_day,
                                                target_calendar_id: newCalId,
                                              });
                                              toast("success", "Event moved successfully!");
                                              void reloadEvents();
                                            } catch (err) {
                                              toast("error", err instanceof Error ? err.message : String(err));
                                            }
                                          }
                                        }}
                                      >
                                        {calendars.map(cal => (
                                          <ContextMenuRadioItem key={cal.id} value={cal.calendar_id}>
                                            {cal.summary}
                                          </ContextMenuRadioItem>
                                        ))}
                                      </ContextMenuRadioGroup>
                                    </ContextMenuSubContent>
                                  </ContextMenuSub>
                                </>
                              )}
                            </>
                          ) : (
                            <>
                              <ContextMenuItem onClick={() => void handleExportSelectedEvents()}>
                                <Download size={14} className="mr-2" aria-hidden="true" />Export {selectedEventIds.length} events
                              </ContextMenuItem>
                              <ContextMenuItem onClick={() => void handleDeleteSelectedEvents()} className="text-destructive focus:bg-destructive/10 focus:text-destructive">
                                <Trash2 size={14} className="mr-2" aria-hidden="true" />Delete {selectedEventIds.length} events
                                <ContextMenuShortcut>D</ContextMenuShortcut>
                              </ContextMenuItem>

                              {calendars.length > 1 && (
                                <>
                                  <ContextMenuSeparator />
                                  <ContextMenuSub>
                                    <ContextMenuSubTrigger>Change Calendar for {selectedEventIds.length} events</ContextMenuSubTrigger>
                                    <ContextMenuSubContent className="w-48 z-[150]">
                                      <ContextMenuRadioGroup
                                        value=""
                                        onValueChange={async (newCalId) => {
                                          if (newCalId) {
                                            try {
                                              await Promise.all(selectedEventIds.map(async (id) => {
                                                const ev = events.find(e => e.id === id);
                                                if (ev) {
                                                  await api.updateCalendarEvent({
                                                    event_id: ev.id,
                                                    summary: ev.summary,
                                                    dtstart: ev.dtstart,
                                                    all_day: ev.all_day,
                                                    target_calendar_id: newCalId,
                                                  });
                                                }
                                              }));
                                              toast("success", `Moved ${selectedEventIds.length} events successfully!`);
                                              void reloadEvents();
                                              setSelectedEventIds([]);
                                            } catch (err) {
                                              toast("error", err instanceof Error ? err.message : String(err));
                                            }
                                          }
                                        }}
                                      >
                                        {calendars.map(cal => (
                                          <ContextMenuRadioItem key={cal.id} value={cal.calendar_id}>
                                            {cal.summary}
                                          </ContextMenuRadioItem>
                                        ))}
                                      </ContextMenuRadioGroup>
                                    </ContextMenuSubContent>
                                  </ContextMenuSub>
                                </>
                              )}
                            </>
                          )}
                        </ContextMenuContent>
                      </ContextMenu>
                    );
                  }
                }}
              />
            </div>
          )}

          {loading && (
            <div className="pointer-events-none absolute inset-0 flex items-center justify-center bg-background/50 z-20">
              <RefreshCw size={20} className="animate-spin text-muted-foreground" />
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
