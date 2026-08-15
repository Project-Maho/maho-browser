import {render, screen} from "@testing-library/react";
import {describe, expect, it, vi} from "vitest";

import {
  BottomTabBar,
  formatBadgeCount,
  resolveMailBadgePresentation,
} from "./BottomTabBar";

describe("BottomTabBar unread badge", () => {
  it.each([
    [1, "1"],
    [9, "9"],
    [99, "99"],
    [100, "99+"],
  ])("formats %i unread messages as %s", (count, expected) => {
    expect(formatBadgeCount(count)).toBe(expected);
  });

  it.each([
    [1, {text: "1", accessibleName: "Mail, 1 unread message"}],
    [99, {text: "99", accessibleName: "Mail, 99 unread messages"}],
    [100, {text: "99+", accessibleName: "Mail, 99+ unread messages"}],
  ])("matches the native badge presentation for %i unread", (count, expected) => {
    expect(resolveMailBadgePresentation(count)).toEqual(expected);
  });

  it("hides the badge at zero", () => {
    render(
      <BottomTabBar
        activeTab="inbox"
        onTabChange={vi.fn()}
        unreadCount={0}
      />,
    );

    expect(screen.getByRole("button", {name: "Inbox"})).toBeInTheDocument();
    expect(screen.queryByText("0")).not.toBeInTheDocument();
  });

  it("includes the capped count in the accessible name", () => {
    render(
      <BottomTabBar
        activeTab="inbox"
        onTabChange={vi.fn()}
        unreadCount={100}
      />,
    );

    expect(
      screen.getByRole("button", {name: "Mail, 99+ unread messages"}),
    ).toBeInTheDocument();
    expect(screen.getByText("99+")).toBeInTheDocument();
  });

  it("announces one unread message in the singular", () => {
    render(
      <BottomTabBar
        activeTab="inbox"
        onTabChange={vi.fn()}
        unreadCount={1}
      />,
    );

    expect(
      screen.getByRole("button", {name: "Mail, 1 unread message"}),
    ).toBeInTheDocument();
  });
});
