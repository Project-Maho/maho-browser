import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { Input } from "../Input";

describe("Input", () => {
  it("renders label when provided", () => {
    render(<Input label="Email" value="" onChange={vi.fn()} />);
    expect(screen.getByText("Email")).toBeInTheDocument();
  });

  it("renders input with placeholder", () => {
    render(
      <Input
        placeholder="Enter your email"
        value=""
        onChange={vi.fn()}
      />
    );
    expect(screen.getByPlaceholderText("Enter your email")).toBeInTheDocument();
  });

  it("calls onChange with new value", () => {
    const onChange = vi.fn();
    render(
      <Input
        value=""
        onChange={onChange}
        placeholder="Type here"
      />
    );
    const input = screen.getByPlaceholderText("Type here");
    fireEvent.change(input, { target: { value: "test" } });
    expect(onChange).toHaveBeenCalledWith("test");
  });

  it("shows error message when error prop is set", () => {
    render(
      <Input
        value=""
        onChange={vi.fn()}
        error="This field is required"
      />
    );
    expect(screen.getByText("This field is required")).toBeInTheDocument();
  });

  it("disables input when disabled prop is true", () => {
    render(
      <Input
        value=""
        onChange={vi.fn()}
        disabled={true}
      />
    );
    const input = screen.getByRole("textbox");
    expect(input).toBeDisabled();
  });
});
