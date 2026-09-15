import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { CalendarEventCard } from "../CalendarEventCard";
import { TestProviders } from "../../../test/mocks";

vi.mock("react-i18next", () => ({
  useTranslation: () => ({ t: (key: string) => key }),
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
  rsvp_status: null,
  ics_data: null,
  recurrence_rule: null,
  all_day: false,
  created_at: "2026-01-01T00:00:00Z",
  updated_at: "2026-01-01T00:00:00Z",
  uid: "uid-1",
};

describe("CalendarEventCard", () => {
  it("renders the event summary, time, location, and organizer", () => {
    render(
      <CalendarEventCard event={mockEvent as never} accountEmail="user@example.com" onRsvp={vi.fn()} />,
      { wrapper: TestProviders },
    );

    expect(screen.getByText("Meeting")).toBeInTheDocument();
    expect(screen.getByText(/\d{1,2}:\d{2}/)).toBeInTheDocument();
    expect(screen.getByText("Room 1")).toBeInTheDocument();
    expect(screen.getByText("boss@example.com")).toBeInTheDocument();
  });

  it("shows the all-day badge when the event is all day", () => {
    render(
      <CalendarEventCard
        event={{ ...mockEvent, all_day: true } as never}
        accountEmail="user@example.com"
        onRsvp={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    expect(screen.getByText("calendar.allDay")).toBeInTheDocument();
  });

  it("calls onRsvp with the selected status", () => {
    const onRsvp = vi.fn();

    render(
      <CalendarEventCard event={mockEvent as never} accountEmail="user@example.com" onRsvp={onRsvp} />,
      { wrapper: TestProviders },
    );

    fireEvent.click(screen.getByRole("button", { name: "calendar.rsvp.accepted" }));

    expect(onRsvp).toHaveBeenCalledWith("evt-1", "accepted");
  });
});
