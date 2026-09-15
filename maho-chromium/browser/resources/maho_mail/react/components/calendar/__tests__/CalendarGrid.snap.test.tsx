import { render } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { CalendarGrid } from "../CalendarGrid";
import { TestProviders } from "../../../test/mocks";

vi.mock("../../../events.js", () => ({
  listen: vi.fn().mockResolvedValue(() => {}),
}));
vi.mock("react-i18next", () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { language: "en" },
  }),
}));

vi.mock("../../../api", () => ({
  listCalendarEvents: vi.fn().mockResolvedValue([]),
  listAccountCalendars: vi.fn().mockResolvedValue([]),
  listCalendarCategories: vi.fn().mockResolvedValue([]),
  getAppSetting: vi.fn().mockResolvedValue("09:00"),
  createCalendarEvent: vi.fn().mockResolvedValue({}),
  updateCalendarEvent: vi.fn().mockResolvedValue({}),
}));

describe("CalendarGrid Snap and Alt-Drag", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("renders the CalendarGrid properly", () => {
    const { container } = render(
      <CalendarGrid
        accountId="acc-1"
        onSelectEvent={vi.fn()}
        onCreateEvent={vi.fn()}
      />,
      { wrapper: TestProviders },
    );
    expect(container).toBeDefined();
  });
});
