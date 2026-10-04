import { useTranslation } from "react-i18next";
import { Calendar, Clock, MapPin, User, FileText, Pencil, Trash2, BellOff, Copy, Download, Tag, Car, Ban, Target, Video } from "lucide-react";
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogDescription,
  DialogFooter,
} from "../ui/dialog";
import { Button } from "../ui/Button";
import { Badge } from "../ui/Badge";
import { useConfirm } from "../ui/ConfirmDialog";
import { useToast } from "../ui/Toast";
import * as api from "../../api";
import type { CalendarEvent } from "../../types";
import { openExternalUrl } from "../../utils/openExternal.js";

interface CalendarEventDetailProps {
  event: CalendarEvent;
  accountEmail: string;
  onClose: () => void;
  onRsvp: (eventId: string, status: string) => void;
  onDelete: (eventId: string) => void;
  onEdit: (event: CalendarEvent) => void;
}

const RSVP_OPTIONS = ["accepted", "declined", "tentative"] as const;

const RSVP_LABEL_KEY: Record<(typeof RSVP_OPTIONS)[number], string> = {
  accepted: "calendar.rsvpAccept",
  declined: "calendar.rsvpDecline",
  tentative: "calendar.rsvpTentative",
};

function isInternalGoogleOrganizer(value: string): boolean {
  return /@(import|group)\.calendar\.google\.com$/i.test(value);
}

function formatDateTime(iso: string, allDay: boolean): string {
  if (allDay) {
    const [y, m, d] = iso.split("-").map(Number);
    const date = new Date(y, m - 1, d);
    return date.toLocaleDateString(undefined, { weekday: "long", year: "numeric", month: "long", day: "numeric" });
  }
  const d = new Date(iso);
  return d.toLocaleDateString(undefined, { weekday: "long", year: "numeric", month: "long", day: "numeric" }) +
    " " + d.toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit" });
}

export function CalendarEventDetail({
  event,
  accountEmail: _accountEmail,
  onClose,
  onRsvp,
  onDelete,
  onEdit,
}: CalendarEventDetailProps) {
  const { t } = useTranslation();
  const confirm = useConfirm();
  const { toast } = useToast();

  const handleSnooze = async (minutes: number) => {
    try {
      await api.snoozeCalendarEvent(event.id, minutes);
      toast("success", t("calendar.snoozed", { minutes }) || `Snoozed for ${minutes} minutes`);
      onClose();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : String(err));
    }
  };

  const handleDelete = async () => {
    const confirmed = await confirm({
      title: t("calendar.deleteTitle"),
      message: t("calendar.deleteMessage"),
      confirmLabel: t("calendar.deleteConfirm"),
      danger: true,
    });
    if (confirmed) {
      onDelete(event.id);
    }
  };

  const handleDuplicate = async () => {
    try {
      await api.duplicateCalendarEvent(event.id);
      toast("success", "Event duplicated (shifted +1 hour)");
      window.dispatchEvent(new CustomEvent("calendar-events-changed", { detail: event.account_id }));
      onClose();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : String(err));
    }
  };

  const handleExportIcs = async () => {
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

  const lowerSummary = (event.summary || "").toLowerCase();
  const isOoo = event.event_type === "outOfOffice" || lowerSummary.includes("out of office") || lowerSummary.startsWith("ooo");
  const isFocus = event.event_type === "focusTime" || lowerSummary.includes("focus time") || lowerSummary.startsWith("focus");

  return (
    <Dialog open onOpenChange={(open) => { if (!open) onClose(); }}>
      <DialogContent className="max-w-md">
        <DialogHeader>
          <DialogTitle className="flex items-center flex-wrap gap-1.5 pr-6">
            <span>{event.summary}</span>
            {event.category && (
              <Badge variant="outline" className="border-primary/45 text-primary text-[10px] font-semibold h-5">
                <Tag size={10} aria-hidden="true" /> {event.category}
              </Badge>
            )}
            {event.travel_time_minutes ? (
              <Badge variant="outline" className="border-indigo-500/40 text-indigo-600 dark:text-indigo-400 text-[10px] font-semibold h-5">
                <Car size={10} aria-hidden="true" /> Travel: {event.travel_time_minutes}m
              </Badge>
            ) : null}
            {isOoo && (
              <Badge variant="outline" className="border-rose-500/40 text-rose-600 dark:text-rose-400 text-[10px] font-semibold h-5">
                <Ban size={10} aria-hidden="true" /> Out of Office
              </Badge>
            )}
            {isFocus && (
              <Badge variant="outline" className="border-purple-500/40 text-purple-600 dark:text-purple-400 text-[10px] font-semibold h-5">
                <Target size={10} aria-hidden="true" /> Focus Time
              </Badge>
            )}
          </DialogTitle>
          <DialogDescription asChild className="flex items-center justify-between w-full mt-1.5">
            <div className="flex items-center justify-between w-full mt-1.5">
              <div className="flex gap-2">
                {event.status && event.status !== "confirmed" && (
                  <Badge variant="secondary">{event.status}</Badge>
                )}
              </div>
              <Button
                variant="ghost"
                size="sm"
                onClick={handleExportIcs}
                className="text-muted-foreground hover:text-foreground h-6 px-1.5 gap-1 text-[10px]"
              >
                <Download size={11} /> Export .ics
              </Button>
            </div>
          </DialogDescription>
        </DialogHeader>

        <div className="space-y-4">
          <div className="flex items-start gap-3">
            <Calendar size={16} className="mt-0.5 shrink-0 text-muted-foreground" />
            <div className="space-y-0.5">
              <p className="text-xs font-medium text-muted-foreground">{t("calendar.startDate")}</p>
              <p className="text-sm text-foreground">{formatDateTime(event.dtstart, event.all_day)}</p>
            </div>
          </div>

          {event.dtend && (
            <div className="flex items-start gap-3">
              <Clock size={16} className="mt-0.5 shrink-0 text-muted-foreground" />
              <div className="space-y-0.5">
                <p className="text-xs font-medium text-muted-foreground">{t("calendar.endDate")}</p>
                <p className="text-sm text-foreground">{formatDateTime(event.dtend, event.all_day)}</p>
              </div>
            </div>
          )}

          {event.location && (
            <div className="flex items-start gap-3">
              <MapPin size={16} className="mt-0.5 shrink-0 text-muted-foreground" />
              <div className="space-y-0.5">
                <p className="text-xs font-medium text-muted-foreground">{t("calendar.location")}</p>
                <p className="text-sm text-foreground">{event.location}</p>
              </div>
            </div>
          )}

          {event.hangout_link && (
            <div className="flex items-start gap-3">
              <Video size={16} className="mt-0.5 shrink-0 text-muted-foreground" aria-hidden="true" />
              <div className="space-y-1 w-full animate-fade-in">
                <p className="text-xs font-medium text-muted-foreground">{t("calendar.videoCall") || "Video Call"}</p>
                <button
                  type="button"
                  onClick={() => openExternalUrl(event.hangout_link!)}
                  className="inline-flex items-center justify-center rounded-xl bg-primary px-3 py-1.5 text-xs font-semibold text-primary-foreground shadow transition-colors hover:bg-primary/90 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                >
                  {t("calendar.joinMeet") || "Join Google Meet"}
                </button>
              </div>
            </div>
          )}

          {event.organizer && !isInternalGoogleOrganizer(event.organizer) && (
            <div className="flex items-start gap-3">
              <User size={16} className="mt-0.5 shrink-0 text-muted-foreground" />
              <div className="space-y-0.5">
                <p className="text-xs font-medium text-muted-foreground">{t("calendar.organizer")}</p>
                <p className="text-sm text-foreground">{event.organizer}</p>
              </div>
            </div>
          )}

          {event.description && (
            <div className="flex items-start gap-3">
              <FileText size={16} className="mt-0.5 shrink-0 text-muted-foreground" />
              <div className="space-y-0.5">
                <p className="text-xs font-medium text-muted-foreground">{t("calendar.description")}</p>
                <p className="whitespace-pre-wrap text-sm text-foreground">{event.description}</p>
              </div>
            </div>
          )}

          {event.all_day && (
            <Badge variant="secondary">{t("calendar.allDay")}</Badge>
          )}

          {(() => {
            const attendees = (() => {
              if (!event.attendees_json) return [];
              try {
                return JSON.parse(event.attendees_json) as Array<{
                  email?: string;
                  displayName?: string;
                  responseStatus?: string;
                  organizer?: boolean;
                }>;
              } catch {
                return [];
              }
            })();

            if (attendees.length === 0) return null;

            return (
              <div className="space-y-2 pt-2 border-t border-border/40">
                <div className="flex items-center justify-between">
                  <p className="text-xs font-semibold text-muted-foreground uppercase tracking-wider">
                    {t("calendar.attendees") || "Attendees"}
                  </p>
                  <Badge variant="secondary" className="text-[10px] px-1.5 py-0.5">
                    {attendees.length}
                  </Badge>
                </div>
                <div className="max-h-36 overflow-y-auto space-y-1.5 pr-1">
                  {attendees.map((att, idx) => {
                    let rsvpIcon = "–";
                    let rsvpColor = "text-muted-foreground";
                    if (att.responseStatus === "accepted") {
                      rsvpIcon = "✓";
                      rsvpColor = "text-emerald-500 font-bold";
                    } else if (att.responseStatus === "declined") {
                      rsvpIcon = "✕";
                      rsvpColor = "text-destructive font-bold";
                    } else if (att.responseStatus === "tentative") {
                      rsvpIcon = "?";
                      rsvpColor = "text-amber-500 font-bold";
                    }

                    const name = att.displayName || att.email || t("calendar.unknownAttendee") || "Unknown";

                    return (
                      <div key={idx} className="flex items-center justify-between text-xs py-0.5">
                        <div className="flex items-center gap-2 truncate">
                          <div className="w-5 h-5 rounded-full bg-primary/10 text-primary flex items-center justify-center font-semibold text-[10px]">
                            {name.substring(0, 1).toUpperCase()}
                          </div>
                          <span className="truncate text-foreground font-medium">{name}</span>
                          {att.organizer && (
                            <span className="text-[9px] px-1 py-0.2 bg-primary/10 text-primary rounded-md font-semibold shrink-0">
                              {t("calendar.organizerBadge") || "Organizer"}
                            </span>
                          )}
                        </div>
                        <span className={`text-[11px] shrink-0 w-4 text-center ${rsvpColor}`} title={att.responseStatus}>
                          {rsvpIcon}
                        </span>
                      </div>
                    );
                  })}
                </div>
              </div>
            );
          })()}

          <div className="space-y-2">
            <p className="text-xs font-medium text-muted-foreground">{t("calendar.rsvpLabel")}</p>
            <div className="flex items-center gap-2">
              {RSVP_OPTIONS.map((status) => (
                <Button
                  key={status}
                  variant={event.rsvp_status === status ? "default" : "outline"}
                  size="sm"
                  onClick={() => onRsvp(event.id, status)}
                >
                  {t(RSVP_LABEL_KEY[status])}
                </Button>
              ))}
            </div>
          </div>
        </div>

        <DialogFooter className="flex items-center justify-between sm:justify-between w-full">
          <div className="flex items-center gap-1">
            <Button variant="outline" size="sm" onClick={() => void handleSnooze(5)} title="Snooze 5 min">
              <BellOff size={13} className="mr-1" /> 5m
            </Button>
            <Button variant="outline" size="sm" onClick={() => void handleSnooze(60)} title="Snooze 1 hour">
              <BellOff size={13} className="mr-1" /> 1h
            </Button>
          </div>
          <div className="flex items-center gap-1">
            <Button variant="outline" size="sm" onClick={handleDuplicate} title="Duplicate event">
              <Copy size={13} className="mr-1" />
              Duplicate
            </Button>
            <Button variant="outline" size="sm" onClick={() => onEdit(event)}>
              <Pencil size={14} className="mr-1" />
              {t("calendar.edit")}
            </Button>
            <Button variant="destructive" size="sm" onClick={handleDelete}>
              <Trash2 size={14} className="mr-1" />
              {t("calendar.delete")}
            </Button>
          </div>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
