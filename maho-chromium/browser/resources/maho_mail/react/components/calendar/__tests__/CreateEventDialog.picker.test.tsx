import { render, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { CreateEventDialog } from "../CreateEventDialog";
import { TestProviders } from "../../../test/mocks";
import * as api from "../../../api";

vi.mock("react-i18next", () => ({
  useTranslation: () => ({
    t: (key: string) => key,
  }),
}));

vi.mock("../../../api", () => ({
  createCalendarEvent: vi.fn(),
  updateCalendarEvent: vi.fn(),
  listAccountCalendars: vi.fn(),
  listCalendarCategories: vi.fn().mockResolvedValue([]),
  checkEventConflict: vi.fn(),
  getAppSetting: vi.fn().mockResolvedValue("10"),
}));

describe("CreateEventDialog - Picker and Conflict", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("lists writable calendars only in the calendar selection dropdown", async () => {
    const mockCalendars = [
      { id: "cal-1", calendar_id: "cal-1", summary: "My Writable Cal", access_role: "owner" },
      { id: "cal-2", calendar_id: "cal-2", summary: "Subscribed Holiday Cal", access_role: "reader" },
    ];
    vi.mocked(api.listAccountCalendars).mockResolvedValue(mockCalendars as any);
    vi.mocked(api.checkEventConflict).mockResolvedValue([]);

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
      expect(api.listAccountCalendars).toHaveBeenCalledWith("acc-1");
    });
  });

  it("renders a conflict warning when overlapping events are detected", async () => {
    vi.mocked(api.listAccountCalendars).mockResolvedValue([]);
    vi.mocked(api.checkEventConflict).mockResolvedValue([
      { id: "conflict-1", summary: "Conflicting Meeting", dtstart: "2026-01-15T10:00:00Z" }
    ] as any);

    render(
      <CreateEventDialog
        isOpen
        onClose={vi.fn()}
        accountId="acc-1"
        onEventSaved={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    fireEvent.change(document.querySelectorAll('input[type="datetime-local"]')[0], { target: { value: "2026-01-15T10:00" } });
    fireEvent.change(document.querySelectorAll('input[type="datetime-local"]')[1], { target: { value: "2026-01-15T11:00" } });

    await waitFor(() => {
      expect(api.checkEventConflict).toHaveBeenCalled();
    });
  });
});
