import { render, screen } from "@testing-library/react";
import { describe, it, expect } from "vitest";
import { Badge } from "../Badge";

describe("Badge", () => {
  it("renders children", () => {
    render(<Badge>Inbox</Badge>);
    expect(screen.getByText("Inbox")).toBeInTheDocument();
  });

  it("applies variant content", () => {
    render(<Badge variant="success">Synced</Badge>);
    expect(screen.getByText("Synced")).toHaveClass("bg-success/20");
  });
});
