import { fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { SchedulePicker } from "../../common/SchedulePicker";
import { TestProviders } from "../../../test/mocks";
afterEach(() => vi.useRealTimers());
it.each([1, 23])("C10 tomorrow morning uses local next-day 09:00 at hour %i", hour => {
 vi.useFakeTimers();
 vi.setSystemTime(new Date(2026, 8, 5, hour));
 const onSchedule = vi.fn();
 render(<SchedulePicker isOpen onClose={vi.fn()} onSchedule={onSchedule} />, { wrapper: TestProviders });
 fireEvent.click(screen.getByRole("button", { name: "Tomorrow morning" }));
 expect(onSchedule).toHaveBeenCalledWith(new Date(2026, 8, 6, 9).toISOString());
});

describe.each([
  ["ordinary day", 2026, 8, 5],
  ["spring DST transition", 2026, 2, 7],
  ["autumn DST transition", 2026, 9, 31],
] as const)("C10 next-day presets on %s", (_label, year, month, day) => {
  it.each([1, 23])("uses local calendar times at hour %i", hour => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date(year, month, day, hour, 37, 29, 456));
    const onSchedule = vi.fn();
    render(<SchedulePicker isOpen onClose={vi.fn()} onSchedule={onSchedule} />, { wrapper: TestProviders });
    fireEvent.click(screen.getByRole("button", { name: "Tomorrow morning" }));
    expect(onSchedule).toHaveBeenLastCalledWith(new Date(year, month, day + 1, 9).toISOString());
    fireEvent.click(screen.getByRole("button", { name: "Tomorrow afternoon" }));
    expect(onSchedule).toHaveBeenLastCalledWith(new Date(year, month, day + 1, 14).toISOString());
  });
});
