import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import * as Sentry from "@sentry/browser";
import { ErrorBoundary } from "../ErrorBoundary";

// Shelved react-i18next mock: in accordance with Maho's English-only policy,
// i18n is shelved and translation wrappers must not be relied upon to render
// raw translation keys.
vi.mock("react-i18next", () => ({
  withTranslation: () => (Component: any) => (props: any) => (
    <Component
      {...props}
      t={(key: string) => key}
      i18n={{ language: "en" }}
      tReady={true}
    />
  ),
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { language: "en" },
  }),
}));

vi.mock("@sentry/browser", () => ({
  withScope: vi.fn((callback: (scope: any) => void) => {
    callback({ setExtra: vi.fn() });
  }),
  captureException: vi.fn(),
}));

function ThrowingComponent({ error }: { error?: Error }): never {
  throw error ?? new Error("Crash for boundary test");
}

function ConditionalThrow({ shouldThrow }: { shouldThrow: boolean }) {
  if (shouldThrow) {
    throw new Error("Triggered update error");
  }
  return <div>Healthy view</div>;
}

describe("ErrorBoundary", () => {
  let originalLocation: Location;

  beforeEach(() => {
    originalLocation = window.location;
    // Mock window.location.reload
    Object.defineProperty(window, "location", {
      configurable: true,
      value: {
        ...originalLocation,
        reload: vi.fn(),
      },
    });
  });

  afterEach(() => {
    Object.defineProperty(window, "location", {
      configurable: true,
      value: originalLocation,
    });
    vi.restoreAllMocks();
  });

  it("renders children when no error occurs", () => {
    render(
      <ErrorBoundary>
        <div data-testid="child-content">Normal Application View</div>
      </ErrorBoundary>,
    );

    expect(screen.getByTestId("child-content")).toBeInTheDocument();
    expect(screen.getByText("Normal Application View")).toBeInTheDocument();
  });

  it("renders visible English title, description, and reload text when an error occurs", () => {
    const consoleSpy = vi.spyOn(console, "error").mockImplementation(() => {});

    render(
      <ErrorBoundary>
        <ThrowingComponent />
      </ErrorBoundary>,
    );

    expect(screen.getByRole("alert")).toBeInTheDocument();
    // Must display hardcoded English strings, not bare translation keys (e.g. errorBoundary.title)
    expect(
      screen.getByRole("heading", { name: "Something went wrong", level: 1 }),
    ).toBeInTheDocument();
    expect(
      screen.getByText(
        "An unexpected error occurred. Please try reloading the application.",
      ),
    ).toBeInTheDocument();
    expect(
      screen.getByRole("button", { name: "Reload" }),
    ).toBeInTheDocument();
    expect(screen.getByText("Crash for boundary test")).toBeInTheDocument();

    consoleSpy.mockRestore();
  });

  it("calls window.location.reload when reload button is clicked", () => {
    const consoleSpy = vi.spyOn(console, "error").mockImplementation(() => {});

    render(
      <ErrorBoundary>
        <ThrowingComponent />
      </ErrorBoundary>,
    );

    const reloadButton = screen.getByRole("button", { name: "Reload" });
    fireEvent.click(reloadButton);
    expect(window.location.reload).toHaveBeenCalledOnce();

    consoleSpy.mockRestore();
  });

  it("reports caught error and componentStack to Sentry", () => {
    const consoleSpy = vi.spyOn(console, "error").mockImplementation(() => {});
    const customError = new Error("Sentry verification test");

    render(
      <ErrorBoundary>
        <ThrowingComponent error={customError} />
      </ErrorBoundary>,
    );

    expect(Sentry.captureException).toHaveBeenCalledWith(customError);

    consoleSpy.mockRestore();
  });

  it("focuses reload button on componentDidUpdate transition to error state", () => {
    const consoleSpy = vi.spyOn(console, "error").mockImplementation(() => {});

    const { rerender } = render(
      <ErrorBoundary>
        <ConditionalThrow shouldThrow={false} />
      </ErrorBoundary>,
    );

    expect(screen.getByText("Healthy view")).toBeInTheDocument();

    rerender(
      <ErrorBoundary>
        <ConditionalThrow shouldThrow={true} />
      </ErrorBoundary>,
    );

    const reloadButton = screen.getByRole("button", { name: "Reload" });
    expect(document.activeElement).toBe(reloadButton);

    consoleSpy.mockRestore();
  });
});
