import { useMemo, useState, useEffect, useRef } from "react";
import { X, Plus, Clock } from "lucide-react";
import * as api from "../../api";
import { Command, CommandInput, CommandList, CommandEmpty, CommandGroup, CommandItem } from "../ui/command";

interface TimezoneConverterProps {
  dtstart: string;
  dtend?: string;
  sourceTimeZone: string;
}

const COMMON_ZONES = [
  "UTC",
  "America/New_York",
  "America/Los_Angeles",
  "Europe/London",
  "Europe/Paris",
  "Europe/Berlin",
  "Asia/Tokyo",
  "Asia/Seoul",
  "Asia/Shanghai",
  "Asia/Kolkata",
  "Asia/Singapore",
  "Australia/Sydney",
];

function toDateInSourceTz(local: string, sourceTz: string): Date {
  if (!local) return new Date();
  const [datePart, timePart] = local.split("T");
  if (!datePart || !timePart) return new Date(local);
  const isoUtc = `${datePart}T${timePart}:00Z`;
  const utcMs = new Date(isoUtc).getTime();
  const offsetMs = getZoneOffsetMs(utcMs, sourceTz);
  return new Date(utcMs - offsetMs);
}

function getZoneOffsetMs(utcMs: number, timeZone: string): number {
  const date = new Date(utcMs);
  const formatter = new Intl.DateTimeFormat("en-US", {
    timeZone,
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false,
  });
  const parts = formatter.formatToParts(date);
  const partMap: Record<string, string> = {};
  for (const p of parts) if (p.type !== "literal") partMap[p.type] = p.value;
  const hour = partMap.hour === "24" ? "00" : partMap.hour;
  const asUtc = Date.UTC(
    Number(partMap.year),
    Number(partMap.month) - 1,
    Number(partMap.day),
    Number(hour),
    Number(partMap.minute),
    Number(partMap.second),
  );
  return asUtc - utcMs;
}

function formatInZone(date: Date, timeZone: string): string {
  return new Intl.DateTimeFormat(undefined, {
    timeZone,
    weekday: "short",
    month: "short",
    day: "numeric",
    hour: "numeric",
    minute: "2-digit",
    hour12: false,
  }).format(date);
}

export function TimezoneConverter({ dtstart, dtend, sourceTimeZone }: TimezoneConverterProps) {
  const [expanded, setExpanded] = useState(false);
  const [zones, setZones] = useState<string[]>([]);
  const [picking, setPicking] = useState(false);
  const [searchTerm, setSearchTerm] = useState("");
  const searchInputRef = useRef<HTMLInputElement>(null);

  const allZones = useMemo<string[]>(() => {
    try {
      if (typeof Intl !== "undefined" && "supportedValuesOf" in Intl) {
        return (Intl as any).supportedValuesOf("timeZone") as string[];
      }
      return COMMON_ZONES;
    } catch {
      return COMMON_ZONES;
    }
  }, []);

  const startDate = useMemo(() => toDateInSourceTz(dtstart, sourceTimeZone), [dtstart, sourceTimeZone]);
  const endDate = useMemo(() => {
    if (dtend) return toDateInSourceTz(dtend, sourceTimeZone);
    const start = toDateInSourceTz(dtstart, sourceTimeZone);
    return new Date(start.getTime() + 60 * 60 * 1000);
  }, [dtstart, dtend, sourceTimeZone]);

  // Load preview zones setting
  useEffect(() => {
    api.getAppSetting("calendar.preview_zones")
      .then((val) => {
        if (val) {
          try {
            const parsed = JSON.parse(val);
            if (Array.isArray(parsed)) {
              setZones(parsed);
            }
          } catch (e) {
            console.error("Failed to parse calendar.preview_zones setting", e);
          }
        }
      })
      .catch((err) => console.error("Failed to load timezone converter settings", err));
  }, []);

  // Save preview zones setting
  const updateZones = (newZones: string[]) => {
    setZones(newZones);
    void api.setAppSetting("calendar.preview_zones", JSON.stringify(newZones));
  };

  useEffect(() => {
    if (picking) {
      searchInputRef.current?.focus();
    }
  }, [picking]);

  const addZone = (z: string) => {
    if (zones.includes(z) || zones.length >= 3) return;
    updateZones([...zones, z]);
    setPicking(false);
    setSearchTerm("");
  };

  const removeZone = (z: string) => {
    updateZones(zones.filter((existing: string) => existing !== z));
  };

  const filteredZones = useMemo(() => {
    const q = searchTerm.toLowerCase();
    return allZones
      .filter((z: string) => z !== sourceTimeZone && !zones.includes(z))
      .filter((z: string) => z.toLowerCase().includes(q))
      .slice(0, 10);
  }, [allZones, sourceTimeZone, zones, searchTerm]);

  if (!expanded) {
    return (
      <button
        type="button"
        onClick={() => setExpanded(true)}
        className="flex items-center gap-1.5 self-start text-xs text-muted-foreground hover:text-foreground transition-colors"
      >
        <Clock size={12} />
        See in other zones
      </button>
    );
  }

  return (
    <div className="rounded-md border border-border/60 bg-muted/30 p-3 space-y-2">
      <div className="flex items-center justify-between">
        <span className="text-xs font-medium text-foreground">Preview in other zones</span>
        <button
          type="button"
          onClick={() => setExpanded(false)}
          className="text-xs text-muted-foreground hover:text-foreground"
        >
          Hide
        </button>
      </div>

      {zones.length === 0 && (
        <p className="text-xs text-muted-foreground">
          Add up to 3 additional time zones to preview the event time.
        </p>
      )}

      <div className="space-y-1.5">
        {zones.map((z) => (
          <div key={z} className="flex items-center justify-between gap-2 rounded bg-background/60 px-2 py-1.5">
            <div className="flex-1 min-w-0">
              <p className="text-xs font-medium text-foreground truncate">{z}</p>
              <p className="text-[11px] text-muted-foreground">
                {formatInZone(startDate, z)}
                {` – ${formatInZone(endDate, z)}`}
                {!dtend && <span className="opacity-75 italic ml-1">(1h default)</span>}
              </p>
            </div>
            <button
              type="button"
              onClick={() => removeZone(z)}
              className="text-muted-foreground hover:text-destructive"
              aria-label={`Remove ${z}`}
            >
              <X size={12} />
            </button>
          </div>
        ))}
      </div>

      {zones.length < 3 && (
        <div className="relative">
          {picking ? (
            <div className="border border-border rounded bg-background shadow-sm z-50">
              <Command className="p-0">
                <CommandInput
                  placeholder="Search time zones..."
                  value={searchTerm}
                  onValueChange={setSearchTerm}
                  className="h-8 py-1.5 text-xs focus:outline-none focus:ring-0 border-0"
                />
                <CommandList className="max-h-32">
                  <CommandEmpty className="text-[10px] py-2 text-muted-foreground text-center">No matching time zones</CommandEmpty>
                  <CommandGroup>
                    {filteredZones.map((z: string) => (
                      <CommandItem
                        key={z}
                        value={z}
                        onSelect={() => addZone(z)}
                        className="text-xs py-1 px-2 cursor-pointer hover:bg-surface-hover text-muted-foreground hover:text-foreground"
                      >
                        {z}
                      </CommandItem>
                    ))}
                  </CommandGroup>
                </CommandList>
              </Command>
            </div>
          ) : (
            <button
              type="button"
              onClick={() => setPicking(true)}
              className="flex items-center gap-1 text-xs text-primary hover:underline"
            >
              <Plus size={12} /> Add zone
            </button>
          )}
        </div>
      )}
    </div>
  );
}
