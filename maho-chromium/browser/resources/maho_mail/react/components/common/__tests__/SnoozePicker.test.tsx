import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { SnoozePicker } from "../SnoozePicker";

describe("SnoozePicker", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date("2026-01-01T00:00:00.000Z"));
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it("renders the snooze options when open", () => {
    render(<SnoozePicker isOpen={true} onClose={vi.fn()} onSnooze={vi.fn()} />);

    expect(screen.getByText("Snooze until")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Later today" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Next month" })).toBeInTheDocument();
  });

  it("snoozes for the requested quick duration", () => {
    const onSnooze = vi.fn();
    render(<SnoozePicker isOpen={true} onClose={vi.fn()} onSnooze={onSnooze} />);

    fireEvent.click(screen.getByRole("button", { name: "Later today" }));

    expect(onSnooze).toHaveBeenCalledTimes(1);
    const until = onSnooze.mock.calls[0][0] as string;
    expect(new Date(until).getTime() - Date.now()).toBe(3 * 60 * 60 * 1000);
  });

  it("uses the custom date and time picker when provided", () => {
    const onSnooze = vi.fn();
    render(<SnoozePicker isOpen={true} onClose={vi.fn()} onSnooze={onSnooze} />);

    const [dateInput, timeInput] = Array.from(document.querySelectorAll('input[type="date"], input[type="time"]')) as HTMLInputElement[];
    fireEvent.change(dateInput, { target: { value: "2026-01-05" } });
    fireEvent.change(timeInput, { target: { value: "08:15" } });
    fireEvent.click(screen.getByRole("button", { name: "Snooze" }));

    const snoozedUntil = new Date(onSnooze.mock.calls[0][0] as string);
    expect(snoozedUntil.getFullYear()).toBe(2026);
    expect(snoozedUntil.getMonth()).toBe(0);
    expect(snoozedUntil.getDate()).toBe(5);
    expect(snoozedUntil.getHours()).toBe(8);
    expect(snoozedUntil.getMinutes()).toBe(15);
  });
});
