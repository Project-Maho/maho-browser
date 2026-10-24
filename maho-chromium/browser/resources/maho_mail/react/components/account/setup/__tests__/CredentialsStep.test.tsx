import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { TestProviders } from "../../../../test/mocks";
import * as api from "../../../../api";
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

  it("reports the port range instead of testing with an invalid port", () => {
    const testConnection = vi.spyOn(api, "testConnection").mockResolvedValue(true);
    const { container } = render(
      <CredentialsStep providerHint="other" onSuccess={vi.fn()} onBack={vi.fn()} />,
      { wrapper: TestProviders }
    );
    const textInputs = container.querySelectorAll<HTMLInputElement>('input:not([type="number"])');
    fireEvent.change(textInputs[0], { target: { value: "me@example.com" } });
    fireEvent.change(container.querySelector('input[type="password"]')!, { target: { value: "secret" } });
    const hosts = screen.getAllByPlaceholderText(/imap|smtp/i);
    fireEvent.change(hosts[0], { target: { value: "imap.example.com" } });
    fireEvent.change(hosts[1], { target: { value: "smtp.example.com" } });
    fireEvent.change(container.querySelectorAll('input[type="number"]')[0], { target: { value: "70000" } });

    fireEvent.click(screen.getByText("Test Connection"));

    expect(screen.getByText("Port numbers must be between 1 and 65535")).toBeInTheDocument();
    expect(testConnection).not.toHaveBeenCalled();
    testConnection.mockRestore();
  });
});
