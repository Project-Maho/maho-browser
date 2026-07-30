import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { SuccessStep } from '../SuccessStep';

describe("SuccessStep", () => {
  it("renders success message", () => {
    render(<SuccessStep onDone={vi.fn()} />);
    expect(screen.getByText("Account added!")).toBeInTheDocument();
  });

  it("renders description", () => {
    render(<SuccessStep onDone={vi.fn()} />);
    expect(screen.getByText("Your email account has been set up successfully.")).toBeInTheDocument();
  });

  it("renders Get Started button", () => {
    render(<SuccessStep onDone={vi.fn()} />);
    expect(screen.getByText("Get Started")).toBeInTheDocument();
  });

  it("calls onDone when Get Started clicked", () => {
    const onDone = vi.fn();
    render(<SuccessStep onDone={onDone} />);
    fireEvent.click(screen.getByText("Get Started"));
    expect(onDone).toHaveBeenCalled();
  });
});
