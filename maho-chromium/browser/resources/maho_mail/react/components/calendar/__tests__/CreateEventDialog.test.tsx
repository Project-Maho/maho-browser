import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { CreateEventDialog } from "../CreateEventDialog";
import { TestProviders } from "../../../test/mocks";
import * as api from "../../../api";

vi.mock("react-i18next", () => ({
  useTranslation: () => ({
    t: (key: string) => {
      if (key === "common.save") return "Save";
      if (key === "common.cancel") return "Cancel";
      return key;
    },
  }),
}));

vi.mock("../../../api", () => ({
  createCalendarEvent: vi.fn(),
  updateCalendarEvent: vi.fn(),
  listAccountCalendars: vi.fn().mockResolvedValue([]),
  listCalendarCategories: vi.fn().mockResolvedValue([]),
  checkEventConflict: vi.fn().mockResolvedValue([]),
  getAppSetting: vi.fn().mockResolvedValue("10"),
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

describe("CreateEventDialog", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.mocked(api.createCalendarEvent).mockResolvedValue(mockEvent as never);
    vi.mocked(api.updateCalendarEvent).mockResolvedValue(mockEvent as never);
  });

  it("renders the create dialog and disabled submit state", () => {
    render(
      <CreateEventDialog
        isOpen
        onClose={vi.fn()}
        accountId="acc-1"
        onEventSaved={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    expect(screen.getByRole("heading", { name: "calendar.createEvent" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "calendar.createEvent" })).toBeDisabled();
    expect(document.querySelectorAll('input[type="datetime-local"]').length).toBe(2);
  });

  it("creates a new event when the form is completed", async () => {
    const onClose = vi.fn();
    const onEventSaved = vi.fn();
    render(
      <CreateEventDialog
        isOpen
        onClose={onClose}
        accountId="acc-1"
        onEventSaved={onEventSaved}
      />,
      { wrapper: TestProviders },
    );

    fireEvent.change(screen.getByLabelText("calendar.fields.summary"), { target: { value: "Planning" } });
    fireEvent.change(screen.getByLabelText("calendar.fields.description"), { target: { value: "Quarterly planning" } });
    fireEvent.change(document.querySelectorAll('input[type="datetime-local"]')[0], { target: { value: "2026-01-15T10:00" } });
    fireEvent.change(document.querySelectorAll('input[type="datetime-local"]')[1], { target: { value: "2026-01-15T11:00" } });
    fireEvent.change(screen.getByLabelText("calendar.fields.location"), { target: { value: "Room 1" } });

    fireEvent.click(screen.getByRole("button", { name: "calendar.createEvent" }));

    await waitFor(() => {
      expect(api.createCalendarEvent).toHaveBeenCalledWith(
        expect.objectContaining({
          account_id: "acc-1",
          summary: "Planning",
          description: "Quarterly planning",
          location: "Room 1",
          all_day: false,
        }),
      );
      expect(onEventSaved).toHaveBeenCalledWith(mockEvent);
      expect(onClose).toHaveBeenCalled();
    });
  });

  it("loads edit data and switches to date inputs for all-day events", () => {
    render(
      <CreateEventDialog
        isOpen
        onClose={vi.fn()}
        accountId="acc-1"
        editEvent={{ ...mockEvent, all_day: true } as never}
        onEventSaved={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    expect(screen.getByDisplayValue("Meeting")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Save" })).toBeEnabled();
    expect(document.querySelectorAll('input[type="date"]').length).toBe(2);
  });

  it("renders and updates category, travel time, and event type fields", async () => {
    vi.mocked(api.listCalendarCategories).mockResolvedValue([
      { id: "cat-1", account_id: "acc-1", name: "Work", color: "#ef4444" },
    ]);

    render(
      <CreateEventDialog
        isOpen
        onClose={vi.fn()}
        accountId="acc-1"
        onEventSaved={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByText("Work")).toBeInTheDocument();
    });

    const categorySelect = screen.getByLabelText("Category");
    const travelSelect = screen.getByLabelText("Travel Time (minutes)");
    const typeSelect = screen.getByLabelText("Event Type");

    expect(categorySelect).toBeInTheDocument();
    expect(travelSelect).toBeInTheDocument();
    expect(typeSelect).toBeInTheDocument();

    fireEvent.change(categorySelect, { target: { value: "Work" } });
    fireEvent.change(travelSelect, { target: { value: "30" } });
    fireEvent.change(typeSelect, { target: { value: "outOfOffice" } });

    expect(categorySelect).toHaveValue("Work");
    expect(travelSelect).toHaveValue("30");
    expect(typeSelect).toHaveValue("outOfOffice");
  });
});
