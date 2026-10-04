import { act, cleanup, fireEvent, render, renderHook, screen } from "@testing-library/react";
import { afterEach, beforeEach, expect, it, vi } from "vitest";
import { CalendarGrid } from "../CalendarGrid";
import { useCalendarClient } from "../../../hooks/useCalendarClient";
import * as api from "../../../api";
import type { CalendarEvent, GoogleCalendarEntry } from "../../../types";

vi.mock("../../../events.js", () => ({ listen: vi.fn().mockResolvedValue(() => {}) }));
const t = (key: string) => key;
const toast = vi.fn();
vi.mock("react-i18next", () => ({ useTranslation: () => ({ t, i18n: { language: "en" } }) }));
vi.mock("../../ui/Toast", () => ({ useToast: () => ({ toast }) }));
vi.mock("sonner", () => ({ toast: { success: vi.fn() } }));
vi.mock("../../../api", () => ({
  listCalendarEvents: vi.fn(), listAccountCalendars: vi.fn(),
  listCalendarCategories: vi.fn().mockResolvedValue([]),
  getAppSetting: vi.fn().mockResolvedValue(null),
  setCalendarVisibility: vi.fn(), deleteCalendarEvent: vi.fn().mockResolvedValue(undefined),
  duplicateCalendarEvent: vi.fn().mockResolvedValue(undefined),
  createCalendarEvent: vi.fn().mockResolvedValue(undefined),
  updateCalendarEvent: vi.fn().mockResolvedValue(undefined),
  syncGoogleCalendar: vi.fn().mockResolvedValue({ events_upserted: 1, events_deleted: 0 }),
}));
vi.mock("react-big-calendar", () => ({
  dateFnsLocalizer: vi.fn(),
  Calendar: ({ events, onSelectEvent, view }: {
    events: { resource: CalendarEvent }[];
    onSelectEvent: (event: { resource: CalendarEvent }, e: React.SyntheticEvent) => void;
    view: string;
  }) => <div data-testid="calendar" data-view={view}>{events.map(event =>
    <button key={event.resource.id} onClick={e => onSelectEvent(event, e)}>{event.resource.id}</button>)}</div>,
}));
vi.mock("react-big-calendar/lib/addons/dragAndDrop", () => ({ default: (component: unknown) => component }));
vi.mock("../MiniCalendar", () => ({ MiniCalendar: () => null }));
vi.mock("../TodayPanel", () => ({ TodayPanel: () => null }));

const calendar: GoogleCalendarEntry = {
  id: "cal", account_id: "acc", calendar_id: "remote", summary: "Calendar",
  background_color: null, foreground_color: null, is_primary: true, access_role: "owner", visible: true,
};
const events: CalendarEvent[] = ["one", "two"].map(id => ({
  id, account_id: "acc", email_id: null, uid: id, summary: id, description: null,
  dtstart: "2026-09-05T09:00:00Z", dtend: "2026-09-05T10:00:00Z", location: null,
  organizer: null, status: "confirmed", rsvp_status: null, recurrence_rule: null,
  all_day: false, created_at: "", updated_at: "", google_calendar_id: "remote",
}));
beforeEach(() => {
  vi.clearAllMocks();
  vi.mocked(api.listCalendarEvents).mockResolvedValue(events);
  vi.mocked(api.listAccountCalendars).mockResolvedValue([calendar]);
  vi.mocked(api.setCalendarVisibility).mockImplementation(async () => {
    vi.mocked(api.listCalendarEvents).mockResolvedValue([]);
    vi.mocked(api.listAccountCalendars).mockResolvedValue([{ ...calendar, visible: false }]);
  });
});
afterEach(cleanup);

it("R-7 invalidates cached events after visibility changes", async () => {
  const { result } = renderHook(() => useCalendarClient({ accountId: "acc" }));
  await act(async () => { await result.current.fetchEvents(); });
  expect(result.current.events).toEqual(events);
  vi.mocked(api.listCalendarEvents).mockClear();
  await act(async () => { await result.current.toggleCalendar(calendar); });
  expect(api.listCalendarEvents).toHaveBeenCalledTimes(1);
  expect(result.current.events).toEqual([]);
  expect(result.current.calendars[0].visible).toBe(false);
});

it.each(["local refresh", "Gmail refresh", "duplicate", "quick add"])(
  "R-25 reloads cached events after %s", async operation => {
    const accounts = [{ id: "acc", email: "fixture@example.test", display_name: "Fixture",
      provider: operation === "Gmail refresh" ? "gmail" : "imap" }];
    const { result } = renderHook(() => useCalendarClient({ accountId: "acc", accounts }));
    await act(async () => { await result.current.fetchEvents(); });
    expect(result.current.events).toEqual(events);
    const updated = [...events, { ...events[0], id: "new" }];
    vi.mocked(api.listCalendarEvents).mockClear().mockResolvedValue(updated);
    if (operation === "quick add") {
      act(() => { result.current.setQuickAddText("Lunch September 10, 2026 at 1pm"); });
    }
    await act(async () => {
      if (operation === "duplicate") await result.current.handleDuplicateEvent("one");
      else if (operation === "quick add") {
        await result.current.handleQuickAdd({ preventDefault: vi.fn() } as unknown as React.FormEvent);
      } else await result.current.handleRefresh();
    });
    if (operation === "Gmail refresh") expect(api.syncGoogleCalendar).toHaveBeenCalledWith("acc");
    if (operation === "duplicate") expect(api.duplicateCalendarEvent).toHaveBeenCalledWith("one");
    if (operation === "quick add") expect(api.createCalendarEvent).toHaveBeenCalledWith(
      expect.objectContaining({ account_id: "acc", dtstart: expect.any(String) }));
    expect(api.listCalendarEvents).toHaveBeenCalledTimes(1);
    expect(result.current.events).toEqual(updated);
  },
);

it.each(["d", "c", "e"])("R-9 uses current Cmd-click selection for %s", async key => {
  const onSelectEvent = vi.fn();
  await act(async () => { render(<CalendarGrid accountId="acc" onSelectEvent={onSelectEvent} onCreateEvent={vi.fn()} />); });
  fireEvent.click(screen.getByRole("button", { name: "one", exact: true }), { metaKey: true });
  fireEvent.click(screen.getByRole("button", { name: "two", exact: true }), { metaKey: true });
  await act(async () => { fireEvent.keyDown(document.body, { key }); });
  if (key === "d") {
    expect(screen.queryByRole("button", { name: "two", exact: true })).toBeNull();
    expect(screen.getByTestId("calendar").getAttribute("data-view")).toBe("month");
  } else if (key === "c") {
    expect(api.duplicateCalendarEvent).toHaveBeenCalledWith("two");
  } else {
    expect(onSelectEvent).toHaveBeenCalledWith(events[1]);
  }
});

it("R-26 grid mutations invalidate the cache before reloading", async () => {
  // Given: the grid is mounted and its first load has populated the 5-minute
  // event cache in useCalendarClient.
  const onSelectEvent = vi.fn();
  await act(async () => {
    render(<CalendarGrid accountId="acc" onSelectEvent={onSelectEvent} onCreateEvent={vi.fn()} />);
  });
  await act(async () => { await Promise.resolve(); });

  // When: a mutation performed from the grid (moving an event to another
  // calendar through the context menu path) completes and the grid reloads.
  const updated = [...events, { ...events[0], id: "moved" }];
  vi.mocked(api.listCalendarEvents).mockClear().mockResolvedValue(updated);
  await act(async () => {
    window.dispatchEvent(new CustomEvent("calendar-events-changed", { detail: "acc" }));
    await Promise.resolve();
  });

  // Then: the reload must reach the API instead of being served the stale
  // cached list. A grid mutation that reloads through cached fetchEvents makes
  // a successful move/resize/duplicate snap back to its old state.
  expect(api.listCalendarEvents).toHaveBeenCalled();
});
