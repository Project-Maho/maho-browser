import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { ProviderStep } from '../ProviderStep';

describe("ProviderStep", () => {
  it("renders major providers first", () => {
    render(<ProviderStep onSelect={vi.fn()} />);
    expect(screen.getByText("Gmail")).toBeInTheDocument();
    expect(screen.getByText("Outlook")).toBeInTheDocument();
    expect(screen.getByText("Yahoo Mail")).toBeInTheDocument();
    expect(screen.getByText("iCloud Mail")).toBeInTheDocument();
    expect(screen.getByText("Other")).toBeInTheDocument();
    expect(screen.queryByText("Fastmail")).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: /more providers/i })).toBeInTheDocument();
  });

  it("reveals additional providers when More providers is clicked", () => {
    render(<ProviderStep onSelect={vi.fn()} />);

    fireEvent.click(screen.getByRole("button", { name: /more providers/i }));

    expect(screen.getByText("Fastmail")).toBeInTheDocument();
    expect(screen.getByText("Zoho Mail")).toBeInTheDocument();
    expect(screen.getByText("Naver")).toBeInTheDocument();
    expect(screen.getByText("Enable IMAP and use an app password")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /show fewer/i })).toBeInTheDocument();
  });

  it("renders heading", () => {
    render(<ProviderStep onSelect={vi.fn()} />);
    expect(screen.getByText("Choose your email provider")).toBeInTheDocument();
  });

  it("calls onSelect with gmail when Gmail clicked", () => {
    const onSelect = vi.fn();
    render(<ProviderStep onSelect={onSelect} />);
    fireEvent.click(screen.getByText("Gmail"));
    expect(onSelect).toHaveBeenCalledWith("gmail");
  });

  it("calls onSelect with outlook when Outlook clicked", () => {
    const onSelect = vi.fn();
    render(<ProviderStep onSelect={onSelect} />);
    fireEvent.click(screen.getByText("Outlook"));
    expect(onSelect).toHaveBeenCalledWith("outlook");
  });

  it("calls onSelect with other when Other clicked", () => {
    const onSelect = vi.fn();
    render(<ProviderStep onSelect={onSelect} />);
    fireEvent.click(screen.getByText("Other"));
    expect(onSelect).toHaveBeenCalledWith("other");
  });
});
