import { render, screen, act } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { Avatar } from "../Avatar";
import * as api from "../../../api";

describe("Avatar", () => {
  it("shows initials from name", () => {
    render(<Avatar name="John Doe" />);
    expect(screen.getByText("JD")).toBeInTheDocument();
  });

  it("shows initials from email", () => {
    render(<Avatar name="john@example.com" />);
    expect(screen.getByText("JE")).toBeInTheDocument();
  });

  it("does not produce gravatar.com URLs or build unproxied gravatar images", async () => {
    vi.spyOn(api, "md5Hash").mockResolvedValue("0bc83cb571cd1c50ba6f3e8a78ef1346");
    const { container } = render(<Avatar name="John Doe" email="john@example.com" />);

    await act(async () => {
      await Promise.resolve();
    });

    const gravatarImg = container.querySelector('img[src*="gravatar.com"]');
    expect(gravatarImg).toBeNull();
    expect(container.querySelector("img")).toBeNull();
    expect(container.innerHTML).not.toContain("gravatar.com");
  });

  it("does not call external hash APIs when email is provided", () => {
    const md5Spy = vi.spyOn(api, "md5Hash").mockResolvedValue("0bc83cb571cd1c50ba6f3e8a78ef1346");
    render(<Avatar name="John Doe" email="john@example.com" />);
    expect(md5Spy).not.toHaveBeenCalled();
  });

  it("computes deterministic color class from address hash", () => {
    const { container: c1 } = render(<Avatar name="Alice Smith" email="alice@example.com" />);
    const { container: c2 } = render(<Avatar name="Different Name" email="alice@example.com" />);
    const { container: c3 } = render(<Avatar name="Alice Smith" email="ALICE@EXAMPLE.COM" />);

    const colorClass1 = Array.from(c1.firstElementChild!.classList).find((c) => c.startsWith("bg-"));
    const colorClass2 = Array.from(c2.firstElementChild!.classList).find((c) => c.startsWith("bg-"));
    const colorClass3 = Array.from(c3.firstElementChild!.classList).find((c) => c.startsWith("bg-"));

    expect(colorClass1).toBeDefined();
    expect(colorClass1).toBe(colorClass2);
    expect(colorClass1).toBe(colorClass3);
  });

  it("falls back to name for color hash when email is omitted or empty", () => {
    const { container: c1 } = render(<Avatar name="Alice Smith" />);
    const { container: c2 } = render(<Avatar name="Alice Smith" email="" />);
    const { container: c3 } = render(<Avatar name="Alice Smith" email="   " />);

    const colorClass1 = Array.from(c1.firstElementChild!.classList).find((c) => c.startsWith("bg-"));
    const colorClass2 = Array.from(c2.firstElementChild!.classList).find((c) => c.startsWith("bg-"));
    const colorClass3 = Array.from(c3.firstElementChild!.classList).find((c) => c.startsWith("bg-"));

    expect(colorClass1).toBeDefined();
    expect(colorClass1).toBe(colorClass2);
    expect(colorClass1).toBe(colorClass3);
  });

  it("renders VIP badge when isVip is true", () => {
    render(<Avatar name="Alice" isVip={true} />);
    expect(screen.getByText("★")).toBeInTheDocument();
  });

  it("applies sizing classes and custom className", () => {
    const { container: cSm } = render(<Avatar name="Alice" size="sm" className="custom-class" />);
    expect(cSm.firstElementChild?.className).toContain("h-6 w-6");
    expect(cSm.firstElementChild?.className).toContain("custom-class");

    const { container: cXl } = render(<Avatar name="Alice" size="xl" />);
    expect(cXl.firstElementChild?.className).toContain("h-16 w-16");
  });
});
