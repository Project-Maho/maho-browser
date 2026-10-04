import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { RecurringScopeDialog } from "../RecurringScopeDialog";

vi.mock("react-i18next", () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { language: "en" },
  }),
}));

describe("RecurringScopeDialog", () => {
  it("shows edit prompt when mode=edit", () => {
    render(<RecurringScopeDialog open={true} mode="edit" onCancel={vi.fn()} onSelect={vi.fn()} />);
    expect(screen.getByText("calendar.recurring.promptTitle")).toBeInTheDocument();
    expect(screen.getByText("calendar.recurring.promptMessage")).toBeInTheDocument();
  });

  it("shows delete prompt when mode=delete", () => {
    render(<RecurringScopeDialog open={true} mode="delete" onCancel={vi.fn()} onSelect={vi.fn()} />);
    expect(screen.getByText("calendar.recurring.deletePromptTitle")).toBeInTheDocument();
    expect(screen.getByText("calendar.recurring.deletePromptMessage")).toBeInTheDocument();
  });

  it("calls onSelect('single') when 'this occurrence' clicked", () => {
    const onSelect = vi.fn();
    render(<RecurringScopeDialog open={true} mode="edit" onCancel={vi.fn()} onSelect={onSelect} />);
    fireEvent.click(screen.getByText("calendar.recurring.thisOccurrence"));
    expect(onSelect).toHaveBeenCalledWith("single");
  });

  it("calls onSelect('all') when 'all events' clicked", () => {
    const onSelect = vi.fn();
    render(<RecurringScopeDialog open={true} mode="edit" onCancel={vi.fn()} onSelect={onSelect} />);
    fireEvent.click(screen.getByText("calendar.recurring.allEvents"));
    expect(onSelect).toHaveBeenCalledWith("all");
  });

  it("calls onCancel when cancel button clicked", () => {
    const onCancel = vi.fn();
    render(<RecurringScopeDialog open={true} mode="edit" onCancel={onCancel} onSelect={vi.fn()} />);
    fireEvent.click(screen.getByText("calendar.recurring.cancel"));
    expect(onCancel).toHaveBeenCalled();
  });
});
