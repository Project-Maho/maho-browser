import * as React from "react";
import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { Textarea } from "../Textarea";

describe("Textarea", () => {
  it("renders label and associates it with textarea effectiveId", () => {
    render(<Textarea label="Bio" value="" onChange={vi.fn()} />);
    const label = screen.getByText("Bio");
    const textarea = screen.getByRole("textbox");
    expect(label).toBeInTheDocument();
    expect(textarea).toBeInTheDocument();
    expect(label).toHaveAttribute("for", textarea.id);
  });

  it("renders textarea with placeholder", () => {
    render(
      <Textarea
        placeholder="Enter your comments"
        value=""
        onChange={vi.fn()}
      />,
    );
    expect(
      screen.getByPlaceholderText("Enter your comments"),
    ).toBeInTheDocument();
  });

  it("calls onChange with string value when text changes", () => {
    const onChange = vi.fn();
    render(
      <Textarea
        placeholder="Type here"
        value=""
        onChange={onChange}
      />,
    );
    const textarea = screen.getByPlaceholderText("Type here");
    fireEvent.change(textarea, { target: { value: "Hello world" } });
    expect(onChange).toHaveBeenCalledWith("Hello world");
  });

  it("disables textarea when disabled prop is true", () => {
    render(<Textarea value="" onChange={vi.fn()} disabled />);
    const textarea = screen.getByRole("textbox");
    expect(textarea).toBeDisabled();
  });

  it("does not set aria-describedby and marks aria-invalid false when error is absent", () => {
    render(<Textarea value="" onChange={vi.fn()} />);
    const textarea = screen.getByRole("textbox");
    expect(textarea).toHaveAttribute("aria-invalid", "false");
    expect(textarea).not.toHaveAttribute("aria-describedby");
    expect(screen.queryByText(/error/i)).toBeNull();
  });

  it("links textarea aria-describedby to error element id when error is present", () => {
    render(
      <Textarea
        value=""
        onChange={vi.fn()}
        error="This field is required"
      />,
    );
    const textarea = screen.getByRole("textbox");
    const errorElement = screen.getByText("This field is required");

    expect(errorElement).toBeInTheDocument();
    expect(errorElement).toHaveAttribute("id");
    const errorId = errorElement.getAttribute("id");
    expect(errorId).toBeTruthy();

    expect(textarea).toHaveAttribute("aria-invalid", "true");
    expect(textarea).toHaveAttribute("aria-describedby", errorId);
  });

  it("derives stable error id from provided id and links aria-describedby", () => {
    render(
      <Textarea
        id="user-notes"
        value=""
        onChange={vi.fn()}
        error="Notes cannot exceed 500 characters"
      />,
    );
    const textarea = screen.getByRole("textbox");
    const errorElement = screen.getByText("Notes cannot exceed 500 characters");

    expect(textarea).toHaveAttribute("id", "user-notes");
    expect(errorElement).toHaveAttribute("id", "user-notes-error");
    expect(textarea).toHaveAttribute("aria-describedby", "user-notes-error");
    expect(textarea).toHaveAttribute("aria-invalid", "true");
  });

  it("combines existing aria-describedby with error id when both are present", () => {
    render(
      <Textarea
        id="description-input"
        aria-describedby="char-counter"
        value=""
        onChange={vi.fn()}
        error="Invalid characters found"
      />,
    );
    const textarea = screen.getByRole("textbox");
    expect(textarea).toHaveAttribute(
      "aria-describedby",
      "char-counter description-input-error",
    );
  });

  it("forwards ref to the underlying HTMLTextAreaElement", () => {
    const ref = React.createRef<HTMLTextAreaElement>();
    render(<Textarea ref={ref} value="" onChange={vi.fn()} />);
    expect(ref.current).toBeInstanceOf(HTMLTextAreaElement);
  });
});
