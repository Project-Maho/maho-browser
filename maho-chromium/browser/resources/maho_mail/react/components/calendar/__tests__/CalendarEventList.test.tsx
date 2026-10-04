import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { CalendarEventList } from "../CalendarEventList";
import { TestProviders } from "../../../test/mocks";
import * as api from "../../../api";

vi.mock("react-i18next", () => ({
  useTranslation: () => ({ t: (key: string) => key }),
}));

vi.mock("../../../api", () => ({
  listCalendarEvents: vi.fn(),
}));

const mockEvent = {
  id: "evt-1",
  account_id: "acc-1",
  email_id: "email-1",
  title: "Meeting",
  summary: "Meeting",
  description: "Team standup",
  start_time: "2026-01-15T10:00:00Z",
  end_time: "2026-01-15T11:00:00Z",
  dtstart: "2026-01-15T10:00:00Z",
  dtend: "2026-01-15T11:00:00Z",
  location: "Room 1",
  organizer: "boss@example.com",
  attendees: "alice@example.com,bob@example.com",
  status: "confirmed",
  rsvp_status: "accepted",
  ics_data: null,
  recurrence_rule: null,
  all_day: false,
  created_at: "2026-01-01T00:00:00Z",
  updated_at: "2026-01-01T00:00:00Z",
  uid: "uid-1",
};

describe("CalendarEventList", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.mocked(api.listCalendarEvents).mockResolvedValue([]);
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it("renders the loading skeleton while fetching events", async () => {
    vi.mocked(api.listCalendarEvents).mockImplementation(() => new Promise(() => {}));

    render(
      <CalendarEventList accountId="acc-1" onSelectEvent={vi.fn()} onCreateEvent={vi.fn()} />,
      { wrapper: TestProviders },
    );

    expect(screen.getByText("calendar.title")).toBeInTheDocument();
    expect(document.querySelectorAll(".animate-pulse").length).toBeGreaterThan(0);
  });

  it("shows the empty state when no events are returned", async () => {
    render(
      <CalendarEventList accountId="acc-1" onSelectEvent={vi.fn()} onCreateEvent={vi.fn()} />,
      { wrapper: TestProviders },
    );

    await waitFor(() => expect(screen.getByText("calendar.emptyTitle")).toBeInTheDocument());
    expect(screen.getAllByRole("button", { name: "calendar.createEvent" })).toHaveLength(2);
  });

  it("groups events by date and calls onSelectEvent when clicked", async () => {
    const onSelectEvent = vi.fn();
    const onCreateEvent = vi.fn();
    vi.mocked(api.listCalendarEvents).mockResolvedValue([
      {
        ...mockEvent,
        id: "evt-2",
        summary: "Tomorrow meeting",
        dtstart: new Date(Date.now() + 24 * 60 * 60 * 1000).toISOString(),
        dtend: new Date(Date.now() + 25 * 60 * 60 * 1000).toISOString(),
      } as never,
      { 
        ...mockEvent,
        dtstart: new Date(Date.now() - 24 * 60 * 60 * 1000).toISOString(),
        dtend: new Date(Date.now() - 23 * 60 * 60 * 1000).toISOString(),
      } as never,
    ]);

    render(
      <CalendarEventList accountId="acc-1" onSelectEvent={onSelectEvent} onCreateEvent={onCreateEvent} />,
      { wrapper: TestProviders },
    );

    await waitFor(() => expect(screen.getByText("Meeting")).toBeInTheDocument());
    expect(screen.getByText("calendar.group.upcoming")).toBeInTheDocument();
    expect(screen.getByText("calendar.group.past")).toBeInTheDocument();

    fireEvent.click(screen.getByText("Meeting"));
    expect(onSelectEvent).toHaveBeenCalledWith(expect.objectContaining({ id: "evt-1" }));

    fireEvent.click(screen.getByRole("button", { name: "calendar.createEvent" }));
    expect(onCreateEvent).toHaveBeenCalled();
  });
});
