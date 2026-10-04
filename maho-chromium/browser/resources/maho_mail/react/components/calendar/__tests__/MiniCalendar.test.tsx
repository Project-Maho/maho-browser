import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { MiniCalendar } from "../MiniCalendar";

vi.mock("react-i18next", () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { language: "en" },
  }),
}));

describe("MiniCalendar", () => {
  it("syncs currentMonth when parent value changes", () => {
    const onChange = vi.fn();
    const { rerender } = render(
      <MiniCalendar value={new Date("2026-03-15")} onChange={onChange} events={[]} />,
    );
    expect(screen.getByText(/March 2026/i)).toBeInTheDocument();

    rerender(<MiniCalendar value={new Date("2026-08-15")} onChange={onChange} events={[]} />);
    expect(screen.getByText(/August 2026/i)).toBeInTheDocument();
  });

  it("renders i18n weekday labels", () => {
    render(<MiniCalendar value={new Date("2026-03-15")} onChange={vi.fn()} events={[]} />);
    expect(screen.getAllByText("calendar.weekday.sun").length).toBeGreaterThan(0);
    expect(screen.getAllByText("calendar.weekday.sat").length).toBeGreaterThan(0);
  });

  it("emits onChange when a day is clicked", () => {
    const onChange = vi.fn();
    render(<MiniCalendar value={new Date("2026-03-15")} onChange={onChange} events={[]} />);
    fireEvent.click(screen.getByText("20"));
    expect(onChange).toHaveBeenCalledOnce();
    const arg = onChange.mock.calls[0][0] as Date;
    expect(arg.getDate()).toBe(20);
    expect(arg.getMonth()).toBe(2);
  });

  it("shows event dot for days with events", () => {
    const events = [
      {
        id: "e1",
        account_id: "a",
        email_id: null,
        uid: "u",
        summary: "Test",
        description: null,
        dtstart: "2026-03-20T10:00:00Z",
        dtend: null,
        location: null,
        organizer: null,
        status: "confirmed",
        rsvp_status: null,
        recurrence_rule: null,
        all_day: false,
        created_at: "",
        updated_at: "",
      },
    ];
    const { container } = render(
      <MiniCalendar value={new Date("2026-03-15")} onChange={vi.fn()} events={events} />,
    );
    const dots = container.querySelectorAll(".bg-accent");
    expect(dots.length).toBeGreaterThan(0);
  });
});
