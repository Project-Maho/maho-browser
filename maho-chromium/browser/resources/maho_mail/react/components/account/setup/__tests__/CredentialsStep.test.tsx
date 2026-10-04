import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { TestProviders } from "../../../../test/mocks";
import { CredentialsStep } from '../CredentialsStep';

describe("CredentialsStep", () => {
  it("renders credentials form", () => {
    render(
      <CredentialsStep providerHint="yahoo" onSuccess={vi.fn()} onBack={vi.fn()} />,
      { wrapper: TestProviders }
    );
    expect(screen.getByText("Enter your credentials")).toBeInTheDocument();
    expect(screen.getByText("Email address")).toBeInTheDocument();
    expect(screen.getByText("Password")).toBeInTheDocument();
  });

  it("shows advanced settings expanded for other provider", () => {
    render(
      <CredentialsStep providerHint="other" onSuccess={vi.fn()} onBack={vi.fn()} />,
      { wrapper: TestProviders }
    );
    expect(screen.getByText("IMAP (Incoming)")).toBeInTheDocument();
  });

  it("hides advanced settings by default for known provider", () => {
    render(
      <CredentialsStep providerHint="yahoo" onSuccess={vi.fn()} onBack={vi.fn()} />,
      { wrapper: TestProviders }
    );
    expect(screen.queryByText("IMAP (Incoming)")).not.toBeInTheDocument();
  });

  it("toggles advanced settings", () => {
    render(
      <CredentialsStep providerHint="yahoo" onSuccess={vi.fn()} onBack={vi.fn()} />,
      { wrapper: TestProviders }
    );
    fireEvent.click(screen.getByText("Advanced server settings"));
    expect(screen.getByText("IMAP (Incoming)")).toBeInTheDocument();
  });

  it("renders back button", () => {
    render(
      <CredentialsStep providerHint="yahoo" onSuccess={vi.fn()} onBack={vi.fn()} />,
      { wrapper: TestProviders }
    );
    expect(screen.getByText("Back")).toBeInTheDocument();
  });

  it("calls onBack when back button clicked", () => {
    const onBack = vi.fn();
    render(
      <CredentialsStep providerHint="yahoo" onSuccess={vi.fn()} onBack={onBack} />,
      { wrapper: TestProviders }
    );
    fireEvent.click(screen.getByText("Back"));
    expect(onBack).toHaveBeenCalled();
  });
});
