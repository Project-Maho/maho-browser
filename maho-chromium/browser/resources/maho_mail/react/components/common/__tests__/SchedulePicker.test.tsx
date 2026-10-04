import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { SchedulePicker } from "../SchedulePicker";

const { toastMock } = vi.hoisted(() => ({ toastMock: vi.fn() }));

vi.mock("../../ui/Toast", () => ({
  useToast: () => ({ toast: toastMock }),
}));

describe("SchedulePicker", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date(2026, 0, 1));
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it("renders the quick schedule options when open", () => {
    render(<SchedulePicker isOpen={true} onClose={vi.fn()} onSchedule={vi.fn()} />);

    expect(screen.getByText("Schedule send")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "In 1 hour" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "In 2 hours" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "In 4 hours" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Tomorrow morning" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Tomorrow afternoon" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Monday morning" })).toBeInTheDocument();
    expect(screen.getByText("Custom date & time")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Schedule" })).toBeInTheDocument();
    const inputs = document.querySelectorAll('input[type="date"], input[type="time"]');
    expect(inputs).toHaveLength(2);
  });

  it("schedules a quick option about one hour from now", () => {
    const onSchedule = vi.fn();
    render(<SchedulePicker isOpen={true} onClose={vi.fn()} onSchedule={onSchedule} />);

    fireEvent.click(screen.getByRole("button", { name: "In 1 hour" }));

    expect(onSchedule).toHaveBeenCalledTimes(1);
    const scheduledAt = onSchedule.mock.calls[0][0] as string;
    expect(new Date(scheduledAt).getTime() - Date.now()).toBe(60 * 60 * 1000);
  });

  it("schedules a custom date and time when provided", () => {
    const onSchedule = vi.fn();
    render(<SchedulePicker isOpen={true} onClose={vi.fn()} onSchedule={onSchedule} />);

    const [dateInput, timeInput] = Array.from(document.querySelectorAll('input[type="date"], input[type="time"]')) as HTMLInputElement[];
    fireEvent.change(dateInput, { target: { value: "2026-01-02" } });
    fireEvent.change(timeInput, { target: { value: "14:30" } });
    fireEvent.click(screen.getByRole("button", { name: "Schedule" }));

    const scheduledAt = new Date(onSchedule.mock.calls[0][0] as string);
    expect(scheduledAt.getFullYear()).toBe(2026);
    expect(scheduledAt.getMonth()).toBe(0);
    expect(scheduledAt.getDate()).toBe(2);
    expect(scheduledAt.getHours()).toBe(14);
    expect(scheduledAt.getMinutes()).toBe(30);
  });

  it("rejects a past custom date and does not call onSchedule", () => {
    const onSchedule = vi.fn();
    render(<SchedulePicker isOpen={true} onClose={vi.fn()} onSchedule={onSchedule} />);

    const [dateInput, timeInput] = Array.from(document.querySelectorAll('input[type="date"], input[type="time"]')) as HTMLInputElement[];
    fireEvent.change(dateInput, { target: { value: "2025-12-31" } });
    fireEvent.change(timeInput, { target: { value: "23:59" } });
    fireEvent.click(screen.getByRole("button", { name: "Schedule" }));

    expect(onSchedule).not.toHaveBeenCalled();
    expect(toastMock).toHaveBeenCalledWith("error", expect.any(String));
  });

  it("rejects scheduling at exactly now (not strictly future)", () => {
    const onSchedule = vi.fn();
    render(<SchedulePicker isOpen={true} onClose={vi.fn()} onSchedule={onSchedule} />);

    const [dateInput, timeInput] = Array.from(document.querySelectorAll('input[type="date"], input[type="time"]')) as HTMLInputElement[];
    fireEvent.change(dateInput, { target: { value: "2026-01-01" } });
    fireEvent.change(timeInput, { target: { value: "00:00" } });
    fireEvent.click(screen.getByRole("button", { name: "Schedule" }));

    expect(onSchedule).not.toHaveBeenCalled();
    expect(toastMock).toHaveBeenCalledWith("error", expect.any(String));
  });
});
