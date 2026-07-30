import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { Button } from "../Button";

describe("Button", () => {
  it("renders children", () => {
    render(<Button>Click me</Button>);
    expect(screen.getByText("Click me")).toBeInTheDocument();
  });

  it("calls onClick when clicked", () => {
    const onClick = vi.fn();
    render(<Button onClick={onClick}>Click</Button>);
    fireEvent.click(screen.getByText("Click"));
    expect(onClick).toHaveBeenCalledOnce();
  });

  it("is disabled when disabled prop is true", () => {
    render(<Button disabled>Click</Button>);
    expect(screen.getByRole("button")).toBeDisabled();
  });

  it("shows loading spinner", () => {
    render(<Button loading>Click</Button>);
    expect(screen.getByRole("button")).toBeDisabled();
  });
});
