import { render, screen } from "@testing-library/react";
import { describe, it, expect } from "vitest";
import { EmptyState } from "../EmptyState";
import { Inbox } from "lucide-react";

describe("EmptyState", () => {
  it("renders title and description", () => {
    render(
      <EmptyState
        title="No emails"
        description="You have no emails in this folder"
      />
    );
    expect(screen.getByText("No emails")).toBeInTheDocument();
    expect(screen.getByText("You have no emails in this folder")).toBeInTheDocument();
  });

  it("shows default icon when no icon prop", () => {
    const { container } = render(
      <EmptyState
        title="No emails"
        description="You have no emails in this folder"
      />
    );
    const iconContainer = container.querySelector(".flex.h-16.w-16");
    expect(iconContainer).toBeInTheDocument();
    const svg = iconContainer?.querySelector("svg");
    expect(svg).toBeInTheDocument();
  });

  it("shows custom icon when provided", () => {
    const { container } = render(
      <EmptyState
        title="No emails"
        description="You have no emails in this folder"
        icon={<Inbox size={28} />}
      />
    );
    const svg = container.querySelector("svg");
    expect(svg).toBeInTheDocument();
  });
});
