import { render, screen, fireEvent, waitFor, within } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterAll } from "vitest";
import { CreateEventDialog } from "../CreateEventDialog";
import { TestProviders } from "../../../test/mocks";
import * as api from "../../../api";

vi.mock("react-i18next", () => ({
  useTranslation: () => ({
    t: (key: string) => {
      if (key === "calendar.createEvent") return "Create Event";
      if (key === "calendar.details") return "Details";
      if (key === "calendar.findTime") return "Find a Time";
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
  googleCalendarFreeBusy: vi.fn(),
  getAppSetting: vi.fn().mockResolvedValue("10"),
}));

describe("CreateEventDialog - Timezone handling (DEFECT-04 and DEFECT-05)", () => {
  const originalTz = process.env.TZ;

  beforeEach(() => {
    vi.clearAllMocks();
    process.env.TZ = "America/New_York";
  });

  afterAll(() => {
    process.env.TZ = originalTz;
  });

  it("DEFECT-04: checkSlotBusy evaluates attendee working hours in local time rather than appending literal Z", async () => {
    // In America/New_York (EDT, UTC-4), 09:00 EDT corresponds to 13:00 UTC.
    // Colleague is busy 09:00 - 10:00 local time on 2026-09-08.
    const busyStartIso = new Date("2026-09-08T09:00:00").toISOString(); // 2026-09-08T13:00:00.000Z in EDT
    const busyEndIso = new Date("2026-09-08T10:00:00").toISOString();   // 2026-09-08T14:00:00.000Z in EDT

    vi.mocked(api.googleCalendarFreeBusy).mockResolvedValue({
      calendars: {
        "colleague@example.com": {
          busy: [{ start: busyStartIso, end: busyEndIso }],
        },
      },
    } as any);

    render(
      <CreateEventDialog
        isOpen
        onClose={vi.fn()}
        accountId="acc-1"
        onEventSaved={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    // Set start date to 2026-09-08T09:00
    fireEvent.change(document.querySelectorAll('input[type="datetime-local"]')[0], {
      target: { value: "2026-09-08T09:00" },
    });

    // Add colleague attendee via ContactAutocomplete
    const attendeeInput = screen.getByPlaceholderText("e.g. boss@company.com, client@example.com");
    fireEvent.change(attendeeInput, { target: { value: "colleague@example.com" } });
    fireEvent.keyDown(attendeeInput, { key: "Enter" });

    // Click "Find a Time" tab button to trigger handleFetchFreeBusy
    fireEvent.click(screen.getByRole("button", { name: /^Find a Time$/i }));

    await waitFor(() => {
      expect(api.googleCalendarFreeBusy).toHaveBeenCalledWith(
        "acc-1",
        ["colleague@example.com"],
        "2026-09-08T00:00:00Z",
        "2026-09-08T23:59:59Z",
      );
    });

    // Wait for the free/busy table to appear
    const table = await screen.findByRole("table");
    const rows = within(table).getAllByRole("row");

    // Row for 09:00 - 10:00 local time
    const row9am = rows.find(r => r.textContent?.includes("09:00 - 10:00"));
    expect(row9am).toBeDefined();

    // With the bug (literal "Z" on local hours), slotStart was parsed as 09:00:00Z (05:00 EDT).
    // The busy block is 13:00:00Z - 14:00:00Z (09:00 - 10:00 EDT).
    // So 09:00 - 10:00 displays "Free" instead of "Busy".
    expect(within(row9am!).getByText("Busy")).toBeInTheDocument();

    // Colleague is free at 13:00 - 14:00 local time (1:00 PM EDT = 17:00:00Z), so that row must be Free
    const row1pm = rows.find(r => r.textContent?.includes("13:00 - 14:00"));
    expect(row1pm).toBeDefined();
    expect(within(row1pm!).getByText("Free")).toBeInTheDocument();
  });

  it("DEFECT-05: handleSubmit converts datetime-local using selected timeZone before toISOString", async () => {
    vi.mocked(api.createCalendarEvent).mockResolvedValue({
      id: "evt-new",
      account_id: "acc-1",
      summary: "London Sync",
      dtstart: "2026-09-08T14:00:00.000Z",
    } as any);

    render(
      <CreateEventDialog
        isOpen
        onClose={vi.fn()}
        accountId="acc-1"
        onEventSaved={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    fireEvent.change(screen.getByLabelText("calendar.fields.summary"), { target: { value: "London Sync" } });
    fireEvent.change(document.querySelectorAll('input[type="datetime-local"]')[0], { target: { value: "2026-09-08T15:00" } });
    fireEvent.change(document.querySelectorAll('input[type="datetime-local"]')[1], { target: { value: "2026-09-08T16:00" } });

    // Select Europe/London time zone
    const tzSelect = screen.getByLabelText(/Event Time Zone/i);
    fireEvent.change(tzSelect, { target: { value: "Europe/London" } });

    fireEvent.click(screen.getByRole("button", { name: "Create Event" }));

    await waitFor(() => {
      expect(api.createCalendarEvent).toHaveBeenCalledWith(
        expect.objectContaining({
          account_id: "acc-1",
          summary: "London Sync",
          // In Europe/London on Sept 8 (BST, UTC+1), 15:00 BST is 14:00 UTC.
          // Without fix, it serializes using local client timezone (e.g. 19:00:00.000Z in EDT).
          dtstart: "2026-09-08T14:00:00.000Z",
          dtend: "2026-09-08T15:00:00.000Z",
          time_zone: "Europe/London",
        }),
      );
    });
  });
});
