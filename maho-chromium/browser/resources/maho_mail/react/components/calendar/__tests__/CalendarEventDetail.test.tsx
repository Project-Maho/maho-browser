import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { CalendarEventDetail } from "../CalendarEventDetail";
import * as api from "../../../api";

vi.mock("../../ui/ConfirmDialog", () => ({
  useConfirm: () => vi.fn().mockResolvedValue(true),
}));

vi.mock("../../../api", () => ({
  duplicateCalendarEvent: vi.fn().mockResolvedValue({}),
  exportCalendarIcs: vi.fn().mockResolvedValue("BEGIN:VCALENDAR..."),
  snoozeCalendarEvent: vi.fn().mockResolvedValue({}),
}));

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
  rsvp_status: "accepted",
  ics_data: null,
  recurrence_rule: null,
  all_day: false,
  created_at: "2026-01-01T00:00:00Z",
  updated_at: "2026-01-01T00:00:00Z",
  uid: "uid-1",
};

describe("CalendarEventDetail", () => {
  it("renders event details and RSVP actions", () => {
    render(
      <CalendarEventDetail
        event={mockEvent as never}
        accountEmail="user@example.com"
        onClose={vi.fn()}
        onRsvp={vi.fn()}
        onDelete={vi.fn()}
        onEdit={vi.fn()}
      />,
    );

    expect(screen.getByText("Meeting")).toBeInTheDocument();
    expect(screen.getByText("Room 1")).toBeInTheDocument();
    expect(screen.getByText("Team standup")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "calendar.rsvpAccept" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "calendar.edit" })).toBeInTheDocument();
  });

  it("calls onRsvp and onEdit with the event", () => {
    const onRsvp = vi.fn();
    const onEdit = vi.fn();

    render(
      <CalendarEventDetail
        event={mockEvent as never}
        accountEmail="user@example.com"
        onClose={vi.fn()}
        onRsvp={onRsvp}
        onDelete={vi.fn()}
        onEdit={onEdit}
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "calendar.rsvpDecline" }));
    fireEvent.click(screen.getByRole("button", { name: "calendar.edit" }));

    expect(onRsvp).toHaveBeenCalledWith("evt-1", "declined");
    expect(onEdit).toHaveBeenCalledWith(expect.objectContaining({ id: "evt-1" }));
  });

  it("confirms deletion before calling onDelete", async () => {
    const onDelete = vi.fn();

    render(
      <CalendarEventDetail
        event={mockEvent as never}
        accountEmail="user@example.com"
        onClose={vi.fn()}
        onRsvp={vi.fn()}
        onDelete={onDelete}
        onEdit={vi.fn()}
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "calendar.delete" }));

    await waitFor(() => expect(onDelete).toHaveBeenCalledWith("evt-1"));
  });

  it("calls duplicate and export ICS APIs", async () => {
    // Mock global URL using vi.stubGlobal for download test
    vi.stubGlobal("URL", {
      createObjectURL: vi.fn().mockReturnValue("mock-url"),
      revokeObjectURL: vi.fn(),
    });

    render(
      <CalendarEventDetail
        event={mockEvent as never}
        accountEmail="user@example.com"
        onClose={vi.fn()}
        onRsvp={vi.fn()}
        onDelete={vi.fn()}
        onEdit={vi.fn()}
      />,
    );

    const duplicateButton = screen.getByRole("button", { name: "Duplicate" });
    fireEvent.click(duplicateButton);

    const exportButton = screen.getByRole("button", { name: "Export .ics" });
    fireEvent.click(exportButton);

    await waitFor(() => {
      expect(api.duplicateCalendarEvent).toHaveBeenCalledWith("evt-1");
      expect(api.exportCalendarIcs).toHaveBeenCalledWith("acc-1", undefined, "2026-01-15T10:00:00Z", "2026-01-15T10:00:00Z");
    });
  });
});
