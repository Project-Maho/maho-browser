import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { TestProviders } from "../../../../test/mocks";
import { AccountSetupFlow } from '../AccountSetupFlow';

describe("AccountSetupFlow", () => {
  it("renders ProviderStep initially", () => {
    render(<AccountSetupFlow onComplete={vi.fn()} />, { wrapper: TestProviders });
    expect(screen.getByText("Choose your email provider")).toBeInTheDocument();
  });

  it("navigates to OAuth step when Gmail selected", () => {
    render(<AccountSetupFlow onComplete={vi.fn()} />, { wrapper: TestProviders });
    fireEvent.click(screen.getByText("Gmail"));
    expect(screen.getByRole("heading", { name: "Sign in with Google" })).toBeInTheDocument();
  });

  it("navigates to OAuth step when Outlook selected", () => {
    render(<AccountSetupFlow onComplete={vi.fn()} />, { wrapper: TestProviders });
    fireEvent.click(screen.getByText("Outlook"));
    expect(screen.getByRole("heading", { name: "Sign in with Microsoft" })).toBeInTheDocument();
  });

  it("navigates to Credentials step when Yahoo selected", () => {
    render(<AccountSetupFlow onComplete={vi.fn()} />, { wrapper: TestProviders });
    fireEvent.click(screen.getByText("Yahoo Mail"));
    expect(screen.getByText("Enter your credentials")).toBeInTheDocument();
  });

  it("reveals additional providers before selecting Naver", () => {
    render(<AccountSetupFlow onComplete={vi.fn()} />, { wrapper: TestProviders });

    expect(screen.queryByText("Naver")).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: /more providers/i }));
    fireEvent.click(screen.getByText("Naver"));

    expect(screen.getByText("Enter your credentials")).toBeInTheDocument();
    expect(screen.getByText("Naver Mail Setup")).toBeInTheDocument();
  });

  it("navigates to Credentials step when Other selected", () => {
    render(<AccountSetupFlow onComplete={vi.fn()} />, { wrapper: TestProviders });
    fireEvent.click(screen.getByText("Other"));
    expect(screen.getByText("Enter your credentials")).toBeInTheDocument();
  });

  it("back button returns to provider step from credentials", () => {
    render(<AccountSetupFlow onComplete={vi.fn()} />, { wrapper: TestProviders });
    fireEvent.click(screen.getByText("Yahoo Mail"));
    expect(screen.getByText("Enter your credentials")).toBeInTheDocument();
    fireEvent.click(screen.getByText("Back"));
    expect(screen.getByText("Choose your email provider")).toBeInTheDocument();
  });
});
