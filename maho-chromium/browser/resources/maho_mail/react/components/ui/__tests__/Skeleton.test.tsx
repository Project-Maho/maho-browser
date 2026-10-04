import { render } from "@testing-library/react";
import { describe, it, expect } from "vitest";
import { Skeleton, EmailListSkeleton, EmailDetailSkeleton } from "../Skeleton";

describe("Skeleton", () => {
  it("renders Skeleton with animate-pulse class", () => {
    const { container } = render(<Skeleton />);
    const div = container.querySelector("div");
    expect(div).toHaveClass("animate-pulse");
  });

  it("renders Skeleton with custom className", () => {
    const { container } = render(<Skeleton className="h-10 w-10" />);
    const div = container.querySelector("div");
    expect(div).toHaveClass("h-10");
    expect(div).toHaveClass("w-10");
  });
});

describe("EmailListSkeleton", () => {
  it("renders EmailListSkeleton with 6 skeleton items", () => {
    const { container } = render(<EmailListSkeleton />);
    const skeletons = container.querySelectorAll(".animate-pulse");
    expect(skeletons.length).toBeGreaterThanOrEqual(6);
  });
});

describe("EmailDetailSkeleton", () => {
  it("renders EmailDetailSkeleton", () => {
    const { container } = render(<EmailDetailSkeleton />);
    const skeletons = container.querySelectorAll(".animate-pulse");
    expect(skeletons.length).toBeGreaterThan(0);
  });
});
