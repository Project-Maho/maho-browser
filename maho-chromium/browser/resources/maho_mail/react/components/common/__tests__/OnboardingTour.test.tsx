import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { OnboardingTour } from "../OnboardingTour";
import { TestProviders } from "../../../test/mocks";

describe("OnboardingTour", () => {
  beforeEach(() => {
    localStorage.clear();
  });

  it("renders the first step when forced open", () => {
    render(
      <TestProviders>
        <OnboardingTour forceShow={true} onComplete={vi.fn()} />
      </TestProviders>,
    );

    expect(screen.getByRole("heading", { name: "Welcome to Maho Mail", level: 3 })).toBeInTheDocument();
    expect(screen.getByText(/Onboarding tour step 1 of 6/)).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Next" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Skip tour" })).toBeInTheDocument();
  });

  it("advances through the tour and completes on the final step", () => {
    const onComplete = vi.fn();

    render(
      <TestProviders>
        <OnboardingTour forceShow={true} onComplete={onComplete} />
      </TestProviders>,
    );

    for (let i = 0; i < 5; i += 1) {
      fireEvent.click(screen.getByRole("button", { name: /Next|Get Started/ }));
    }

    expect(screen.getByRole("heading", { name: "Customize Everything", level: 3 })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Get Started" })).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Get Started" }));

    expect(onComplete).toHaveBeenCalledTimes(1);
    expect(screen.queryByRole("heading", { name: "Customize Everything", level: 3 })).not.toBeInTheDocument();
  });
});
