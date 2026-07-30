import { render, screen } from "@testing-library/react";
import { describe, it, expect } from "vitest";
import { Avatar } from "../Avatar";

describe("Avatar", () => {
  it("shows initials from name", () => {
    render(<Avatar name="John Doe" />);
    expect(screen.getByText("JD")).toBeInTheDocument();
  });

  it("shows initials from email", () => {
    render(<Avatar name="john@example.com" />);
    expect(screen.getByText("JE")).toBeInTheDocument();
  });
});
