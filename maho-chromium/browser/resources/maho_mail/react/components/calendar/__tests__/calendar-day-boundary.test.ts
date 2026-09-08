import React from "react";
import { render, screen } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterAll } from "vitest";
import { MiniCalendar } from "../MiniCalendar";
import { YearView } from "../YearView";
import { MobileDayView } from "../MobileDayView";
import type { CalendarEvent } from "../../../types";

vi.mock("react-i18next", () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { language: "en" },
  }),
}));

const testEvent: CalendarEvent = {
  id: "evt-night",
  account_id: "acc-1",
  email_id: null,
  uid: "uid-night",
  summary: "Late Night Standup",
  description: "Standup across midnight",
  dtstart: "2026-09-08T02:00:00Z", // 2026-09-07 22:00:00 EDT in America/New_York
  dtend: "2026-09-08T03:00:00Z",   // 2026-09-07 23:00:00 EDT
  location: null,
  organizer: null,
  status: "confirmed",
  rsvp_status: null,
  recurrence_rule: null,
  all_day: false,
  created_at: "2026-09-01T00:00:00Z",
  updated_at: "2026-09-01T00:00:00Z",
};

describe("Calendar Day Boundary Across Timezones (DEFECT-10)", () => {
  const originalTz = process.env.TZ;

  beforeEach(() => {
    process.env.TZ = "America/New_York";
  });

  afterAll(() => {
    process.env.TZ = originalTz;
  });

  it("DEFECT-10 (MiniCalendar): marks local date (Sept 7) with event dot instead of UTC date (Sept 8)", () => {
    // In America/New_York (EDT, UTC-4), 2026-09-08T02:00:00Z occurs on September 7 at 22:00.
    // MiniCalendar for September 2026 (selected date: Sept 1):
    const { container } = render(
      React.createElement(MiniCalendar, {
        value: new Date("2026-09-01T12:00:00"),
        onChange: vi.fn(),
        events: [testEvent],
      }),
    );

    // Find all day buttons in MiniCalendar
    const buttons = Array.from(container.querySelectorAll("button"));
    const day7Button = buttons.find((btn) => btn.querySelector("span")?.textContent?.trim() === "7");
    const day8Button = buttons.find((btn) => btn.querySelector("span")?.textContent?.trim() === "8");

    expect(day7Button).toBeDefined();
    expect(day8Button).toBeDefined();

    // With the bug (e.dtstart.substring(0, 10)), event was stored under "2026-09-08",
    // so Sept 7 has no dot and Sept 8 has the dot.
    // The correct behavior: Sept 7 has the dot (.bg-accent), Sept 8 does NOT.
    expect(day7Button!.querySelector(".bg-accent")).not.toBeNull();
    expect(day8Button!.querySelector(".bg-accent")).toBeNull();
  });

  it("DEFECT-10 (YearView): marks local date (Sept 7) with event dot instead of UTC date (Sept 8)", () => {
    const { container } = render(
      React.createElement(YearView, {
        date: new Date("2026-09-01T12:00:00"),
        onNavigate: vi.fn(),
        onViewChange: vi.fn(),
        events: [testEvent],
      }),
    );

    // Find September month card
    const monthCards = Array.from(container.querySelectorAll(".bg-card\\/30, div.rounded-xl"));
    const septCard = monthCards.find((card) => card.textContent?.includes("September"));
    expect(septCard).toBeDefined();

    const buttons = Array.from(septCard!.querySelectorAll("button"));
    const day7Button = buttons.find((btn) => btn.querySelector("span")?.textContent?.trim() === "7");
    const day8Button = buttons.find((btn) => btn.querySelector("span")?.textContent?.trim() === "8");

    expect(day7Button).toBeDefined();
    expect(day8Button).toBeDefined();

    // In YearView, the dot indicator is .bg-primary
    expect(day7Button!.querySelector(".bg-primary")).not.toBeNull();
    expect(day8Button!.querySelector(".bg-primary")).toBeNull();
  });

  it("DEFECT-10 (MobileDayView): displays event on local date (Sept 7) matching local midnight boundaries", () => {
    // Viewing Sept 7 local date:
    render(
      React.createElement(MobileDayView, {
        date: new Date("2026-09-07T12:00:00"),
        onNavigate: vi.fn(),
        events: [testEvent],
        onSelectEvent: vi.fn(),
      }),
    );

    // With the bug (filter e.dtstart.substring(0, 10) === "2026-09-07"),
    // "2026-09-08T02:00:00Z".substring(0, 10) is "2026-09-08", so event is dropped.
    // The correct behavior: event is displayed on Sept 7.
    expect(screen.getByText("Late Night Standup")).toBeInTheDocument();
  });
});
