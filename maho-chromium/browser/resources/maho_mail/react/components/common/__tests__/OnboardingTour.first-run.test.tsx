import { render, screen } from "@testing-library/react";
import { describe, it, expect, beforeEach } from "vitest";
import { OnboardingTour } from "../OnboardingTour";
import { TestProviders } from "../../../test/mocks";

describe("OnboardingTour first-run behavior", () => {
  beforeEach(() => {
    localStorage.clear();
  });

  it("shows onboarding tour automatically on first run when completed flag is absent", () => {
    expect(localStorage.getItem("maho-onboarding-completed")).toBeNull();

    render(
      <TestProviders>
        <OnboardingTour />
      </TestProviders>,
    );

    // On first run, the tour must be displayed to the user
    expect(
      screen.getByRole("heading", { name: "Welcome to Maho Mail", level: 3 }),
    ).toBeInTheDocument();
    expect(screen.getByText(/Onboarding tour step 1 of 6/)).toBeInTheDocument();
  });

  it("does not prematurely mark tour completed in localStorage on initial mount", () => {
    expect(localStorage.getItem("maho-onboarding-completed")).toBeNull();

    render(
      <TestProviders>
        <OnboardingTour />
      </TestProviders>,
    );

    // Mounting the tour must not mark it completed before user finishes/skips it
    expect(localStorage.getItem("maho-onboarding-completed")).toBeNull();
  });

  it("does not show onboarding tour when completed flag is already present", () => {
    localStorage.setItem("maho-onboarding-completed", "true");

    render(
      <TestProviders>
        <OnboardingTour />
      </TestProviders>,
    );

    expect(
      screen.queryByRole("heading", { name: "Welcome to Maho Mail", level: 3 }),
    ).not.toBeInTheDocument();
  });
});
