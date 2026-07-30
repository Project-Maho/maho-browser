import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { YearView } from "../YearView";

vi.mock("react-i18next", () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { language: "en" },
  }),
}));

describe("YearView", () => {
  it("renders 12 months for the given year", () => {
    render(
      <YearView
        date={new Date("2026-06-15")}
        onNavigate={vi.fn()}
        onViewChange={vi.fn()}
        events={[]}
      />,
    );

    expect(screen.getByText("January")).toBeInTheDocument();
    expect(screen.getByText("December")).toBeInTheDocument();
    expect(screen.getByText("2026")).toBeInTheDocument();
  });

  it("syncs currentYear when parent date prop changes", () => {
    const { rerender } = render(
      <YearView
        date={new Date("2026-06-15")}
        onNavigate={vi.fn()}
        onViewChange={vi.fn()}
        events={[]}
      />,
    );
    expect(screen.getByText("2026")).toBeInTheDocument();

    rerender(
      <YearView
        date={new Date("2028-03-10")}
        onNavigate={vi.fn()}
        onViewChange={vi.fn()}
        events={[]}
      />,
    );
    expect(screen.getByText("2028")).toBeInTheDocument();
  });

  it("uses i18n weekday keys", () => {
    render(
      <YearView
        date={new Date("2026-06-15")}
        onNavigate={vi.fn()}
        onViewChange={vi.fn()}
        events={[]}
      />,
    );
    expect(screen.getAllByText("calendar.weekday.mon").length).toBeGreaterThan(0);
  });

  it("invokes onNavigate and onViewChange when a day is clicked", () => {
    const onNavigate = vi.fn();
    const onViewChange = vi.fn();
    render(
      <YearView
        date={new Date("2026-06-15")}
        onNavigate={onNavigate}
        onViewChange={onViewChange}
        events={[]}
      />,
    );

    const days = screen.getAllByText("15");
    fireEvent.click(days[0]);
    expect(onNavigate).toHaveBeenCalled();
    expect(onViewChange).toHaveBeenCalledWith("month");
  });
});
