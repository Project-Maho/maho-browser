import { useEffect, useState, useMemo } from "react";
import { useTranslation } from "react-i18next";
import { fromZonedTime } from "date-fns-tz";
import { TimezoneConverter } from "./TimezoneConverter";
import { SchedulingAssistant } from "./SchedulingAssistant";
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogDescription,
  DialogFooter,
} from "../ui/dialog";
import { Button } from "../ui/Button";
import { Input } from "../ui/Input";
import { Textarea } from "../ui/Textarea";
import { Switch } from "../ui/Switch";
import { useToast } from "../ui/Toast";
import { RefreshCw, X, TriangleAlert } from "lucide-react";
import * as api from "../../api";
import type { CalendarEvent, CreateCalendarEventRequest, UpdateCalendarEventRequest } from "../../types";
import { ContactAutocomplete } from "../compose/ContactAutocomplete";

interface CreateEventDialogProps {
  isOpen: boolean;
  onClose: () => void;
  accountId: string;
  editEvent?: CalendarEvent | null;
  onEventSaved: (event: CalendarEvent) => void;
  initialSummary?: string;
}

function toDateTimeLocal(iso: string): string {
  const d = new Date(iso);
  const pad = (n: number) => n.toString().padStart(2, "0");
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

function toDateLocal(iso: string): string {
  const d = new Date(iso);
  const pad = (n: number) => n.toString().padStart(2, "0");
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
}

import type { CalendarCategory, GoogleCalendarEntry } from "../../types";

export function CreateEventDialog({ isOpen, onClose, accountId, editEvent, onEventSaved, initialSummary }: CreateEventDialogProps) {
  const { t } = useTranslation();
  const { toast } = useToast();
  const [loading, setLoading] = useState(false);

  const [summary, setSummary] = useState("");
  const [description, setDescription] = useState("");
  const [dtstart, setDtstart] = useState("");
  const [dtend, setDtend] = useState("");
  const [location, setLocation] = useState("");
  const [allDay, setAllDay] = useState(false);
  const [addMeet, setAddMeet] = useState(false);
  const [attendees, setAttendees] = useState("");
  const [color, setColor] = useState("");

  // Writable google calendars & selected calendar
  const [calendars, setCalendars] = useState<GoogleCalendarEntry[]>([]);
  const [selectedCalendarId, setSelectedCalendarId] = useState("");

  // Categories list & selected category
  const [categories, setCategories] = useState<CalendarCategory[]>([]);
  const [selectedCategory, setSelectedCategory] = useState("");

  // Travel time & Event type
  const [travelTime, setTravelTime] = useState<number>(0);
  const [eventType, setEventType] = useState<string>("default");
  const [timeZone, setTimeZone] = useState(Intl.DateTimeFormat().resolvedOptions().timeZone);

  // Conflict warning states
  const [conflicts, setConflicts] = useState<CalendarEvent[]>([]);

  // Reminders states
  interface ReminderOverride {
    method: "popup";
    minutes: number;
  }
  const [remindersList, setRemindersList] = useState<ReminderOverride[]>([]);
  const [newReminderVal, setNewReminderVal] = useState(10);
  const [newReminderUnit, setNewReminderUnit] = useState<"min" | "hour" | "day">("min");

  // Scheduling Assistant States
  const [activeTab, setActiveTab] = useState<"details" | "findTime">("details");
  const [freeBusyData, setFreeBusyData] = useState<any>(null);
  const [queryingFreeBusy, setQueryingFreeBusy] = useState(false);

  useEffect(() => {
    if (isOpen && accountId) {
      // Fetch calendars
      api.listAccountCalendars(accountId)
        .then((list) => {
          setCalendars(list);
          if (editEvent) {
            setSelectedCalendarId(editEvent.google_calendar_id || "");
          } else {
            const primary = list.find(c => c.is_primary);
            setSelectedCalendarId(primary ? primary.calendar_id : (list[0]?.calendar_id || ""));
          }
        })
        .catch(console.error);

      // Fetch categories
      api.listCalendarCategories(accountId)
        .then(setCategories)
        .catch(console.error);
    }
  }, [isOpen, accountId, editEvent]);

  useEffect(() => {
    if (editEvent) {
      setSummary(editEvent.summary);
      setDescription(editEvent.description ?? "");
      setDtstart(editEvent.all_day ? toDateLocal(editEvent.dtstart) : toDateTimeLocal(editEvent.dtstart));
      setDtend(editEvent.dtend ? (editEvent.all_day ? toDateLocal(editEvent.dtend) : toDateTimeLocal(editEvent.dtend)) : "");
      setLocation(editEvent.location ?? "");
      setAllDay(editEvent.all_day);
      setAddMeet(false);
      
      if (editEvent.attendees_json) {
        try {
          const parsed = JSON.parse(editEvent.attendees_json) as any[];
          setAttendees(parsed.map(p => p.email).filter(Boolean).join(", "));
        } catch {
          setAttendees("");
        }
      } else {
        setAttendees("");
      }
      setColor(editEvent.color ?? "");
      setSelectedCategory(editEvent.category ?? "");
      setTravelTime(editEvent.travel_time_minutes ?? 0);
      setEventType("default");
      setTimeZone(editEvent.start_tz || Intl.DateTimeFormat().resolvedOptions().timeZone);

      if (editEvent.reminders_json) {
        try {
          const parsed = JSON.parse(editEvent.reminders_json);
          if (parsed && Array.isArray(parsed.overrides)) {
            setRemindersList(parsed.overrides);
          } else {
            setRemindersList([]);
          }
        } catch {
          setRemindersList([]);
        }
      } else {
        setRemindersList([]);
      }
    } else {
      setSummary(initialSummary || "");
      setDescription("");
      setDtstart("");
      setDtend("");
      setLocation("");
      setAllDay(false);
      setAddMeet(false);
      setAttendees("");
      setColor("");
      setSelectedCategory("");
      setTravelTime(0);
      setEventType("default");
      setTimeZone(Intl.DateTimeFormat().resolvedOptions().timeZone);

      // Load default reminder minutes from settings
      api.getAppSetting("calendar.default_reminder_minutes")
        .then((val) => {
          if (!editEvent && isOpen) {
            const mins = val ? Number(val) : 10;
            setRemindersList([{ method: "popup", minutes: mins }]);
          }
        })
        .catch(() => {
          if (!editEvent && isOpen) {
            setRemindersList([{ method: "popup", minutes: 10 }]);
          }
        });
    }
    setActiveTab("details");
    setFreeBusyData(null);
    setConflicts([]);
  }, [editEvent, isOpen, initialSummary]);

  // Query conflicts in background
  useEffect(() => {
    if (!isOpen || !dtstart || !dtend || allDay) {
      setConflicts([]);
      return;
    }

    const delayDebounce = setTimeout(() => {
      try {
        const startIso = fromZonedTime(dtstart, timeZone).toISOString();
        const endIso = fromZonedTime(dtend, timeZone).toISOString();
        api.checkEventConflict(accountId, startIso, endIso, editEvent?.id || undefined)
          .then(setConflicts)
          .catch(console.error);
      } catch (e) {
        // invalid date parse ignores
      }
    }, 400);

    return () => clearTimeout(delayDebounce);
  }, [dtstart, dtend, allDay, accountId, editEvent, isOpen, timeZone]);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!summary.trim() || !dtstart) return;

    try {
      setLoading(true);
      const startIso = allDay ? new Date(dtstart).toISOString() : fromZonedTime(dtstart, timeZone).toISOString();
      const endIso = dtend ? (allDay ? new Date(dtend).toISOString() : fromZonedTime(dtend, timeZone).toISOString()) : undefined;
      const parseAttendeesList = (str: string) => {
        return str.split(",").map(e => {
          const trimmed = e.trim();
          const match = trimmed.match(/<(.+?)>/);
          return match ? match[1].trim() : trimmed;
        }).filter(Boolean);
      };
      const attendeesList = parseAttendeesList(attendees);
      const remindersPayload = {
        useDefault: false,
        overrides: remindersList,
      };

      let saved: CalendarEvent;
      if (editEvent) {
        const scope = (editEvent as CalendarEvent & { _edit_scope?: "single" | "all" })._edit_scope;
        const request: UpdateCalendarEventRequest = {
          event_id: editEvent.id,
          summary: summary.trim(),
          description: description.trim() || undefined,
          dtstart: startIso,
          dtend: endIso,
          location: location.trim() || undefined,
          all_day: allDay,
          attendees: attendeesList,
          color: color || undefined,
          time_zone: allDay ? undefined : timeZone,
          edit_scope: scope,
          target_calendar_id: selectedCalendarId || undefined,
          category: selectedCategory || undefined,
          travel_time_minutes: travelTime || undefined,
          event_type: eventType !== "default" ? eventType : undefined,
          reminders: remindersPayload,
        };
        saved = await api.updateCalendarEvent(request);
      } else {
        const request: CreateCalendarEventRequest = {
          account_id: accountId,
          summary: summary.trim(),
          description: description.trim() || undefined,
          dtstart: startIso,
          dtend: endIso,
          location: location.trim() || undefined,
          all_day: allDay,
          add_meet: addMeet,
          attendees: attendeesList,
          color: color || undefined,
          time_zone: allDay ? undefined : timeZone,
          calendar_id: selectedCalendarId || undefined,
          category: selectedCategory || undefined,
          travel_time_minutes: travelTime || undefined,
          event_type: eventType !== "default" ? eventType : undefined,
          reminders: remindersPayload,
        };
        saved = await api.createCalendarEvent(request);
      }
      toast("success", t(editEvent ? "calendar.eventUpdated" : "calendar.eventCreated"));
      
      // Dispatch event to force refresh on main calendar grid
      window.dispatchEvent(new CustomEvent("calendar-events-changed", { detail: accountId }));
      
      onEventSaved(saved);
      onClose();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("common.error"));
    } finally {
      setLoading(false);
    }
  };

  const handleFetchFreeBusy = async () => {
    if (!accountId || !dtstart) {
      toast("warning", t("calendar.selectStartToFindTime") || "Please enter a start date first");
      return;
    }
    const parseAttendeesList = (str: string) => {
      return str.split(",").map(e => {
        const trimmed = e.trim();
        const match = trimmed.match(/<(.+?)>/);
        return match ? match[1].trim() : trimmed;
      }).filter(Boolean);
    };
    const emailsList = parseAttendeesList(attendees);
    if (emailsList.length === 0) {
      toast("warning", t("calendar.enterAttendeesToFindTime") || "Enter attendees to check availability");
      return;
    }

    setQueryingFreeBusy(true);
    try {
      const dateOnly = dtstart.split("T")[0] || new Date().toISOString().split("T")[0];
      const timeMin = `${dateOnly}T00:00:00Z`;
      const timeMax = `${dateOnly}T23:59:59Z`;
      const data = await api.googleCalendarFreeBusy(accountId, emailsList, timeMin, timeMax);
      setFreeBusyData(data);
      setActiveTab("findTime");
    } catch (err) {
      toast("error", err instanceof Error ? err.message : String(err));
    } finally {
      setQueryingFreeBusy(false);
    }
  };

  const handleSelectTimeSlot = (hour: number) => {
    const dateOnly = dtstart.split("T")[0] || new Date().toISOString().split("T")[0];
    const pad = (n: number) => n.toString().padStart(2, "0");
    setDtstart(`${dateOnly}T${pad(hour)}:00`);
    setDtend(`${dateOnly}T${pad(hour + 1)}:00`);
    setActiveTab("details");
  };

  const inputType = allDay ? "date" : "datetime-local";

  const busySlotsMap = useMemo(() => {
    if (!freeBusyData || !freeBusyData.calendars) return {};
    const map: Record<string, Array<{ start: Date; end: Date }>> = {};
    for (const [email, calData] of Object.entries(freeBusyData.calendars)) {
      const busyList = (calData as any).busy || [];
      map[email] = busyList.map((b: any) => ({
        start: new Date(b.start),
        end: new Date(b.end),
      }));
    }
    return map;
  }, [freeBusyData]);

  const workingHours = [9, 10, 11, 12, 13, 14, 15, 16, 17, 18];

  const checkSlotBusy = (email: string, hour: number) => {
    const list = busySlotsMap[email] || [];
    const dateOnly = dtstart.split("T")[0] || new Date().toISOString().split("T")[0];
    const slotStart = new Date(`${dateOnly}T${String(hour).padStart(2, "0")}:00:00`);
    const slotEnd = new Date(`${dateOnly}T${String(hour + 1).padStart(2, "0")}:00:00`);

    return list.some(b => b.start < slotEnd && b.end > slotStart);
  };

  return (
    <Dialog open={isOpen} onOpenChange={(open) => { if (!open) onClose(); }}>
      <DialogContent className="max-w-xl">
        <DialogHeader>
          <DialogTitle>{t(editEvent ? "calendar.editEvent" : "calendar.createEvent")}</DialogTitle>
          <DialogDescription>{t(editEvent ? "calendar.editEventDescription" : "calendar.createEventDescription")}</DialogDescription>
        </DialogHeader>

        {/* Tab selector */}
        {!editEvent && (
          <div className="flex border-b border-border/60 pb-1 mb-2">
            <button
              type="button"
              onClick={() => setActiveTab("details")}
              className={`px-3 py-1 text-xs font-semibold border-b-2 transition-colors ${
                activeTab === "details" ? "border-primary text-foreground" : "border-transparent text-muted-foreground hover:text-foreground"
              }`}
            >
              {t("calendar.details") || "Details"}
            </button>
            <button
              type="button"
              onClick={handleFetchFreeBusy}
              disabled={queryingFreeBusy}
              className={`px-3 py-1 text-xs font-semibold border-b-2 transition-colors flex items-center gap-1 ${
                activeTab === "findTime" ? "border-primary text-foreground" : "border-transparent text-muted-foreground hover:text-foreground"
              }`}
            >
              {queryingFreeBusy ? (
                <RefreshCw size={12} className="animate-spin" />
              ) : null}
              {t("calendar.findTime") || "Find a Time"}
            </button>
          </div>
        )}

        {activeTab === "details" ? (
          <form onSubmit={handleSubmit} className="space-y-4">
            {conflicts.length > 0 && (
              <div className="rounded-lg bg-amber-500/10 border border-amber-500/25 p-3 text-xs text-amber-600 dark:text-amber-400 space-y-1">
                <p className="font-semibold flex items-center gap-1"><TriangleAlert size={13} aria-hidden="true" /> Scheduling Conflict Warning</p>
                <p>This event overlaps with {conflicts.length} existing event(s):</p>
                <ul className="list-disc list-inside">
                  {conflicts.map(c => (
                    <li key={c.id} className="truncate">
                      <strong>{c.summary}</strong> ({c.dtstart.substring(11, 16)} - {c.dtend?.substring(11, 16)})
                    </li>
                  ))}
                </ul>
              </div>
            )}

            <Input
              label={t("calendar.fields.summary")}
              value={summary}
              onChange={setSummary}
              placeholder={t("calendar.fields.summaryPlaceholder")}
              required
            />

            <Textarea
              label={t("calendar.fields.description")}
              value={description}
              onChange={setDescription}
              placeholder={t("calendar.fields.descriptionPlaceholder")}
              rows={2}
            />

            {calendars.length > 0 && (
              <div className="flex flex-col gap-1.5">
                <label htmlFor="event-calendar" className="text-sm font-medium text-foreground">Calendar</label>
                <select
                  id="event-calendar"
                  value={selectedCalendarId}
                  onChange={(e) => setSelectedCalendarId(e.target.value)}
                  className="flex h-9 w-full rounded-md border border-input bg-transparent px-3 py-1 text-sm shadow-sm focus:outline-none focus:ring-1 focus:ring-ring"
                >
                  {calendars.map(cal => (
                    <option key={cal.id} value={cal.calendar_id} className="bg-popover text-popover-foreground">
                      {cal.summary} {cal.access_role && `(${cal.access_role})`}
                    </option>
                  ))}
                </select>
              </div>
            )}

            <div className="grid grid-cols-2 gap-3">
              <div className="flex flex-col gap-1.5">
                <label className="text-sm font-medium text-foreground">{t("calendar.fields.startDate")}</label>
                <input
                  type={inputType}
                  value={dtstart}
                  onChange={(e) => setDtstart(e.target.value)}
                  required
                  className="flex h-9 w-full rounded-md border border-input bg-transparent px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                />
              </div>
              <div className="flex flex-col gap-1.5">
                <label className="text-sm font-medium text-foreground">{t("calendar.fields.endDate")}</label>
                <input
                  type={inputType}
                  value={dtend}
                  onChange={(e) => setDtend(e.target.value)}
                  className="flex h-9 w-full rounded-md border border-input bg-transparent px-3 py-1 text-sm shadow-sm transition-colors placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                />
              </div>
            </div>

            <div className="grid grid-cols-2 gap-3">
              <div className="flex flex-col gap-1.5">
                <label htmlFor="event-category" className="text-sm font-medium text-foreground">Category</label>
                <select
                  id="event-category"
                  value={selectedCategory}
                  onChange={(e) => setSelectedCategory(e.target.value)}
                  className="flex h-9 w-full rounded-md border border-input bg-transparent px-3 py-1 text-sm shadow-sm focus:outline-none focus:ring-1 focus:ring-ring"
                >
                  <option value="" className="bg-popover text-popover-foreground">None</option>
                  {categories.map(cat => (
                    <option key={cat.id} value={cat.name} className="bg-popover text-popover-foreground">
                      {cat.name}
                    </option>
                  ))}
                </select>
              </div>
              <div className="flex flex-col gap-1.5">
                <label htmlFor="event-travel-time" className="text-sm font-medium text-foreground">Travel Time (minutes)</label>
                <select
                  id="event-travel-time"
                  value={travelTime}
                  onChange={(e) => setTravelTime(Number(e.target.value))}
                  className="flex h-9 w-full rounded-md border border-input bg-transparent px-3 py-1 text-sm shadow-sm focus:outline-none focus:ring-1 focus:ring-ring"
                >
                  <option value={0} className="bg-popover text-popover-foreground">None</option>
                  <option value={15} className="bg-popover text-popover-foreground">15 mins</option>
                  <option value={30} className="bg-popover text-popover-foreground">30 mins</option>
                  <option value={45} className="bg-popover text-popover-foreground">45 mins</option>
                  <option value={60} className="bg-popover text-popover-foreground">60 mins</option>
                </select>
              </div>
            </div>

            <div className="flex flex-col gap-1.5">
              <label htmlFor="event-type" className="text-sm font-medium text-foreground">Event Type</label>
              <select
                id="event-type"
                value={eventType}
                onChange={(e) => setEventType(e.target.value)}
                className="flex h-9 w-full rounded-md border border-input bg-transparent px-3 py-1 text-sm shadow-sm focus:outline-none focus:ring-1 focus:ring-ring"
              >
                <option value="default" className="bg-popover text-popover-foreground">Default Event</option>
                <option value="outOfOffice" className="bg-popover text-popover-foreground">Out of Office (Auto-Decline)</option>
                <option value="focusTime" className="bg-popover text-popover-foreground">Focus Time</option>
              </select>
            </div>

            {!allDay && (
              <div className="flex flex-col gap-1.5">
                <label htmlFor="event-timezone" className="text-sm font-medium text-foreground">Event Time Zone</label>
                <select
                  id="event-timezone"
                  value={timeZone}
                  onChange={(e) => setTimeZone(e.target.value)}
                  className="flex h-9 w-full rounded-md border border-input bg-transparent px-3 py-1 text-sm shadow-sm focus:outline-none focus:ring-1 focus:ring-ring"
                >
                  <option value={Intl.DateTimeFormat().resolvedOptions().timeZone} className="bg-popover text-popover-foreground">
                    Local ({Intl.DateTimeFormat().resolvedOptions().timeZone})
                  </option>
                  <option value="UTC" className="bg-popover text-popover-foreground">UTC</option>
                  <option value="America/New_York" className="bg-popover text-popover-foreground">New York (EST/EDT)</option>
                  <option value="Europe/London" className="bg-popover text-popover-foreground">London (GMT/BST)</option>
                  <option value="Asia/Tokyo" className="bg-popover text-popover-foreground">Tokyo (JST)</option>
                  <option value="Asia/Seoul" className="bg-popover text-popover-foreground">Seoul (KST)</option>
                </select>
              </div>
            )}

            {!allDay && dtstart && (
              <TimezoneConverter dtstart={dtstart} dtend={dtend} sourceTimeZone={timeZone} />
            )}

            <Input
              label={t("calendar.fields.location")}
              value={location}
              onChange={setLocation}
              placeholder={t("calendar.fields.locationPlaceholder")}
            />

            <div className="space-y-1">
              <label className="text-sm font-medium text-foreground">
                {t("calendar.fields.attendees") || "Attendees"}
              </label>
              <ContactAutocomplete
                accountId={accountId}
                value={attendees}
                onChange={setAttendees}
                placeholder="e.g. boss@company.com, client@example.com"
              />
            </div>

            {!allDay && dtstart && attendees.trim() && (
              <SchedulingAssistant
                accountId={accountId}
                attendees={attendees.split(",").map(e => {
                  const trimmed = e.trim();
                  const match = trimmed.match(/<(.+?)>/);
                  return match ? match[1].trim() : trimmed;
                }).filter(Boolean)}
                durationMinutes={
                  dtend
                    ? Math.max(15, Math.round((new Date(dtend).getTime() - new Date(dtstart).getTime()) / 60000))
                    : 60
                }
                onPickSlot={(startIso, endIso) => {
                  setDtstart(toDateTimeLocal(startIso));
                  setDtend(toDateTimeLocal(endIso));
                }}
              />
            )}

            <div className="space-y-2">
              <label className="text-sm font-medium text-foreground">Reminders</label>
              <div className="flex flex-wrap gap-2">
                {remindersList.map((rem, idx) => {
                  let label = `${rem.minutes} min`;
                  if (rem.minutes % 1440 === 0) {
                    label = `${rem.minutes / 1440} day`;
                  } else if (rem.minutes % 60 === 0) {
                    label = `${rem.minutes / 60} hr`;
                  }
                  return (
                    <div key={idx} className="flex items-center gap-1 bg-secondary text-secondary-foreground rounded-full px-2.5 py-0.5 text-xs border border-border">
                      <span>{label}</span>
                      <button
                        type="button"
                        onClick={() => setRemindersList(prev => prev.filter((_, i) => i !== idx))}
                        className="hover:text-destructive text-muted-foreground transition-colors"
                      >
                        <X size={10} />
                      </button>
                    </div>
                  );
                })}
              </div>
              <div className="flex items-center gap-2 mt-1">
                <input
                  type="number"
                  min="1"
                  value={newReminderVal}
                  onChange={(e) => setNewReminderVal(Number(e.target.value))}
                  className="w-16 h-8 rounded border border-input bg-transparent px-2 text-xs focus:outline-none"
                />
                <select
                  value={newReminderUnit}
                  onChange={(e) => setNewReminderUnit(e.target.value as "min" | "hour" | "day")}
                  className="h-8 rounded border border-input bg-transparent px-2 text-xs focus:outline-none focus:ring-1 focus:ring-ring"
                >
                  <option value="min" className="bg-popover text-popover-foreground">Minutes</option>
                  <option value="hour" className="bg-popover text-popover-foreground">Hours</option>
                  <option value="day" className="bg-popover text-popover-foreground">Days</option>
                </select>
                <Button
                  type="button"
                  size="sm"
                  variant="outline"
                  className="h-8 text-xs"
                  onClick={() => {
                    let mins = newReminderVal;
                    if (newReminderUnit === "hour") mins *= 60;
                    else if (newReminderUnit === "day") mins *= 1440;
                    setRemindersList(prev => [...prev, { method: "popup", minutes: mins }]);
                  }}
                >
                  + Add reminder
                </Button>
              </div>
            </div>

            <div className="flex items-center justify-between">
              <label className="text-sm font-medium text-foreground">{t("calendar.fields.allDay")}</label>
              <Switch checked={allDay} onCheckedChange={setAllDay} />
            </div>

            {!editEvent && (
              <div className="flex items-center justify-between">
                <label className="text-sm font-medium text-foreground">{t("calendar.fields.addMeet") || "Add Google Meet"}</label>
                <Switch checked={addMeet} onCheckedChange={setAddMeet} />
              </div>
            )}

            <div className="space-y-1.5">
              <label className="text-sm font-medium text-foreground">{t("calendar.fields.color") || "Event Color"}</label>
              <div className="flex items-center gap-2">
                {["", "#ef4444", "#f59e0b", "#10b981", "#3b82f6", "#8b5cf6"].map((c) => (
                  <button
                    key={c}
                    type="button"
                    onClick={() => setColor(c)}
                    className={`mail-pressable h-7 w-7 rounded-full border ${
                      color === c ? "ring-2 ring-primary ring-offset-2 ring-offset-background border-transparent" : "border-border hover:scale-110"
                    }`}
                    style={{ backgroundColor: c || "var(--primary)" }}
                    title={c || "Default"}
                  />
                ))}
              </div>
            </div>

            <DialogFooter>
              <Button type="button" variant="outline" onClick={onClose}>
                {t("common.cancel")}
              </Button>
              <Button type="submit" loading={loading} disabled={!summary.trim() || !dtstart}>
                {t(editEvent ? "common.save" : "calendar.createEvent")}
              </Button>
            </DialogFooter>
          </form>
        ) : !attendees.trim() ? (
          <div className="py-8 text-center space-y-3">
            <p className="text-xs text-muted-foreground">
              Please enter one or more attendee email addresses (separated by commas) in the Details tab first to find a suitable time.
            </p>
            <Button type="button" variant="outline" size="sm" onClick={() => setActiveTab("details")}>
              Go to Details
            </Button>
          </div>
        ) : (
          <div className="space-y-4 py-2">
            <p className="text-xs text-muted-foreground mb-2">
              {t("calendar.schedulingHelp") || "Working hours (09:00 - 18:00) availability. Click an hour to select it."}
            </p>
            <div className="border border-border rounded-lg overflow-hidden max-h-72 overflow-y-auto">
              <table className="w-full text-xs text-left">
                <thead>
                  <tr className="bg-muted border-b border-border">
                    <th className="p-2 font-medium">Time</th>
                    {Object.keys(busySlotsMap).map(email => (
                      <th key={email} className="p-2 font-medium truncate max-w-[120px]" title={email}>
                        {email.split("@")[0]}
                      </th>
                    ))}
                  </tr>
                </thead>
                <tbody>
                  {workingHours.map(hour => (
                    <tr
                      key={hour}
                      onClick={() => handleSelectTimeSlot(hour)}
                      className="border-b border-border/40 hover:bg-surface-hover cursor-pointer transition-colors"
                    >
                      <td className="p-2 font-medium whitespace-nowrap">
                        {String(hour).padStart(2, "0")}:00 - {String(hour + 1).padStart(2, "0")}:00
                      </td>
                      {Object.keys(busySlotsMap).map(email => {
                        const isBusy = checkSlotBusy(email, hour);
                        return (
                          <td key={email} className="p-2">
                            <div className={`w-full py-1 text-center font-bold rounded ${
                              isBusy ? "bg-rose-500/15 text-rose-500" : "bg-emerald-500/15 text-emerald-500"
                            }`}>
                              {isBusy ? "Busy" : "Free"}
                            </div>
                          </td>
                        );
                      })}
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            <DialogFooter>
              <Button type="button" variant="outline" onClick={() => setActiveTab("details")}>
                {t("common.back")}
              </Button>
            </DialogFooter>
          </div>
        )}
      </DialogContent>
    </Dialog>
  );
}
