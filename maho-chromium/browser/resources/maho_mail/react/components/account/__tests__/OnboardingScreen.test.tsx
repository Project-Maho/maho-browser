import { render, screen } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { TestProviders } from "../../../test/mocks";
import { OnboardingScreen } from '../OnboardingScreen';

describe("OnboardingScreen", () => {
  it("renders welcome heading", () => {
    render(<OnboardingScreen onAccountAdded={vi.fn()} />, { wrapper: TestProviders });
    expect(screen.getByText("Welcome to Maho Mail")).toBeInTheDocument();
  });

  it("renders description", () => {
    render(<OnboardingScreen onAccountAdded={vi.fn()} />, { wrapper: TestProviders });
    expect(screen.getByText("Add your email account to get started")).toBeInTheDocument();
  });

  it("renders provider step", () => {
    render(<OnboardingScreen onAccountAdded={vi.fn()} />, { wrapper: TestProviders });
    expect(screen.getByText("Choose your email provider")).toBeInTheDocument();
  });
});
