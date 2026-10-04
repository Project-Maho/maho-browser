import { useState } from "react";
import { Search, Loader2, TriangleAlert } from "lucide-react";
import * as api from "../../api";
import { useToast } from "../ui/Toast";

interface SchedulingAssistantProps {
  accountId: string;
  attendees: string[];
  onPickSlot: (startIso: string, endIso: string) => void;
  durationMinutes: number;
}

type FreeBusyItem = { start: string; end: string };
type FreeBusyResponse = {
  calendars?: Record<string, { busy: FreeBusyItem[]; errors?: Array<{ domain: string; reason: string }> }>;
};

const freeBusyCache: Record<string, { timestamp: number; data: FreeBusyResponse }> = {};

function ensureUtcMs(iso: string): number {
  const d = new Date(iso);
  return isNaN(d.getTime()) ? 0 : d.getTime();
}

function overlapsBusy(startMs: number, endMs: number, busy: FreeBusyItem[]): boolean {
  for (const b of busy) {
    const bs = ensureUtcMs(b.start);
    const be = ensureUtcMs(b.end);
    if (startMs < be && endMs > bs) return true;
  }
  return false;
}

function formatSlotLabel(ms: number): string {
  return new Intl.DateTimeFormat(undefined, {
    weekday: "short",
    month: "short",
    day: "numeric",
    hour: "numeric",
    minute: "2-digit",
    hour12: false,
  }).format(new Date(ms));
}

function toIso(ms: number): string {
  return new Date(ms).toISOString();
}

export function SchedulingAssistant({ accountId, attendees, onPickSlot, durationMinutes }: SchedulingAssistantProps) {
  const { toast } = useToast();
  const [expanded, setExpanded] = useState(false);
  const [querying, setQuerying] = useState(false);
  const [suggestions, setSuggestions] = useState<Array<{ startMs: number; endMs: number }>>([]);
  const [warnings, setWarnings] = useState<string[]>([]);
  const [lastClickTime, setLastClickTime] = useState(0);

  const fetchWithRetry = async (
    fn: () => Promise<any>,
    retries = 3,
    delay = 1000
  ): Promise<any> => {
    try {
      return await fn();
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      const isRateLimit = msg.includes("429") || msg.includes("403") || msg.toLowerCase().includes("rate limit");
      if (isRateLimit && retries > 0) {
        toast("warning", `Rate limit hit, retrying in ${(delay / 1000).toFixed(1)}s...`);
        await new Promise((resolve) => setTimeout(resolve, delay));
        return fetchWithRetry(fn, retries - 1, delay * 2);
      }
      throw err;
    }
  };

  const runQuery = async () => {
    if (attendees.length === 0) {
      toast("warning", "Add at least one attendee to find a time");
      return;
    }

    const nowTime = Date.now();
    if (nowTime - lastClickTime < 500) {
      return;
    }
    setLastClickTime(nowTime);

    try {
      setQuerying(true);
      setWarnings([]);

      let workStart = 9;
      let workEnd = 18;
      let hideWeekends = false;

      try {
        const startStr = await api.getAppSetting("calendar.working_hours_start");
        if (startStr) workStart = Number(startStr.split(":")[0]);
        const endStr = await api.getAppSetting("calendar.working_hours_end");
        if (endStr) workEnd = Number(endStr.split(":")[0]);
        const hwStr = await api.getAppSetting("calendar.hide_weekends");
        hideWeekends = hwStr === "true";
      } catch (err) {
        console.error("Failed to load settings in SchedulingAssistant", err);
      }

      const now = new Date();
      let timeMin = new Date(now.getTime() + 30 * 60 * 1000);
      const isWeekend = (day: number) => day === 0 || day === 6;

      const currentHour = timeMin.getHours();
      const currentDay = timeMin.getDay();

      if (currentHour < workStart) {
        timeMin.setHours(workStart, 0, 0, 0);
      } else if (currentHour >= workEnd || (hideWeekends && isWeekend(currentDay))) {
        timeMin.setDate(timeMin.getDate() + 1);
        timeMin.setHours(workStart, 0, 0, 0);
      }

      while (hideWeekends && isWeekend(timeMin.getDay())) {
        timeMin.setDate(timeMin.getDate() + 1);
        timeMin.setHours(workStart, 0, 0, 0);
      }

      const timeMax = new Date(timeMin.getTime() + 7 * 24 * 60 * 60 * 1000);

      const sortedAttendees = [...attendees].sort().join(",");
      const cacheKey = `${accountId}|${sortedAttendees}|${timeMin.toISOString()}|${timeMax.toISOString()}`;
      
      let raw: FreeBusyResponse;
      const cached = freeBusyCache[cacheKey];
      if (cached && Date.now() - cached.timestamp < 5 * 60 * 1000) {
        raw = cached.data;
      } else {
        raw = await fetchWithRetry(() =>
          api.googleCalendarFreeBusy(
            accountId,
            attendees,
            timeMin.toISOString(),
            timeMax.toISOString(),
          )
        ) as FreeBusyResponse;
        freeBusyCache[cacheKey] = {
          timestamp: Date.now(),
          data: raw,
        };
      }

      const warningEmails: string[] = [];
      const allBusy: FreeBusyItem[] = [];
      for (const [email, cal] of Object.entries(raw.calendars || {})) {
        if (cal.errors && cal.errors.length > 0) {
          warningEmails.push(email);
        }
        if (cal.busy) {
          allBusy.push(...cal.busy);
        }
      }

      if (warningEmails.length > 0) {
        setWarnings(warningEmails);
      }

      const durationMs = durationMinutes * 60 * 1000;
      const stepMinutes = Math.min(15, durationMinutes);
      const stepMs = stepMinutes * 60 * 1000;
      const found: Array<{ startMs: number; endMs: number }> = [];
      let cursor = timeMin.getTime();
      const end = timeMax.getTime();

      while (cursor + durationMs <= end && found.length < 10) {
        const cursorDate = new Date(cursor);
        const localHour = cursorDate.getHours();
        const localDay = cursorDate.getDay();

        let valid = true;
        if (hideWeekends && isWeekend(localDay)) {
          valid = false;
        }
        if (localHour < workStart || localHour + Math.ceil(durationMinutes / 60) > workEnd) {
          valid = false;
        }

        if (valid) {
          if (!overlapsBusy(cursor, cursor + durationMs, allBusy)) {
            found.push({ startMs: cursor, endMs: cursor + durationMs });
          }
        }
        cursor += stepMs;
      }

      setSuggestions(found);
      if (found.length === 0) {
        toast("warning", "No free slot found in next 7 working days");
      }
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Free/busy query failed");
    } finally {
      setQuerying(false);
    }
  };

  if (!expanded) {
    return (
      <button
        type="button"
        onClick={() => setExpanded(true)}
        className="flex items-center gap-1.5 self-start text-xs text-muted-foreground hover:text-foreground transition-colors"
      >
        <Search size={12} />
        Find a time (Scheduling Assistant)
      </button>
    );
  }

  return (
    <div className="rounded-md border border-border/60 bg-muted/30 p-3 space-y-2">
      <div className="flex items-center justify-between">
        <span className="text-xs font-medium text-foreground">Find a time</span>
        <button
          type="button"
          onClick={() => setExpanded(false)}
          className="text-xs text-muted-foreground hover:text-foreground"
        >
          Hide
        </button>
      </div>

      {warnings.length > 0 && (
        <p className="flex items-start gap-1.5 text-[11px] text-amber-500 font-medium bg-amber-500/10 rounded-lg px-2.5 py-2 border border-amber-500/20">
          <TriangleAlert size={12} className="mt-px shrink-0" aria-hidden="true" /> Cannot check availability for: {warnings.join(", ")}
        </p>
      )}

      <div className="flex items-center justify-between gap-2">
        <p className="text-[11px] text-muted-foreground">
          Suggests available {durationMinutes}-min slots in the next 7 working days ({attendees.length} attendee{attendees.length !== 1 ? "s" : ""}).
        </p>
        <button
          type="button"
          onClick={() => void runQuery()}
          disabled={querying || attendees.length === 0}
          className="flex items-center gap-1 rounded bg-primary px-2 py-1 text-[11px] font-medium text-primary-foreground hover:bg-primary/90 disabled:opacity-50"
        >
          {querying ? <Loader2 size={10} className="animate-spin" /> : <Search size={10} />}
          Search
        </button>
      </div>

      {suggestions.length > 0 && (
        <div className="space-y-1 max-h-40 overflow-y-auto">
          {suggestions.map(({ startMs, endMs }) => (
            <button
              key={startMs}
              type="button"
              onClick={() => onPickSlot(toIso(startMs), toIso(endMs))}
              className="w-full text-left rounded bg-background/60 px-2 py-1.5 text-xs hover:bg-background/90 transition-colors"
            >
              {formatSlotLabel(startMs)}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}
