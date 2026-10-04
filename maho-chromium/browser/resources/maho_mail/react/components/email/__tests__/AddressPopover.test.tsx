import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";

const mockToast = vi.fn();
vi.mock("../../ui/Toast", () => ({
  useToast: () => ({ toast: mockToast }),
  ToastProvider: ({ children }: { children: React.ReactNode }) => children,
}));

vi.mock("../../../api", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../../api")>();
  return {
    ...actual,
    md5Hash: vi.fn().mockResolvedValue(null),
  };
});

import { AddressPopover } from "../AddressPopover";

const mockWriteText = vi.fn().mockResolvedValue(undefined);
Object.defineProperty(globalThis.navigator, "clipboard", {
  value: { writeText: mockWriteText },
  configurable: true,
  writable: true,
});

function renderPopover(props: Partial<React.ComponentProps<typeof AddressPopover>> = {}) {
  return render(
    <AddressPopover address="foo@example.com" name="Foo Bar" {...props}>
      <button type="button" data-testid="trigger">open</button>
    </AddressPopover>,
  );
}

describe("AddressPopover", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    mockWriteText.mockClear();
  });

  it("renders the trigger child and opens popover content on click", async () => {
    renderPopover();
    fireEvent.click(screen.getByTestId("trigger"));
    await waitFor(() => {
      expect(screen.getByRole("button", { name: /compose an email/i })).toBeInTheDocument();
    });
    expect(screen.getAllByText("foo@example.com").length).toBeGreaterThan(0);
    expect(screen.getByText("Foo Bar")).toBeInTheDocument();
  });

  it("writes the email address to clipboard when Copy email address button is clicked", async () => {
    renderPopover({ address: "bar@example.com" });
    fireEvent.click(screen.getByTestId("trigger"));
    const copyBtn = await screen.findByRole("button", { name: /copy email address/i });
    fireEvent.click(copyBtn);
    await waitFor(() => {
      expect(mockWriteText).toHaveBeenCalledWith("bar@example.com");
    });
  });

  it("invokes onCompose with the email address when 'Compose an Email' is clicked", async () => {
    const onCompose = vi.fn();
    renderPopover({ address: "baz@example.com", onCompose });
    fireEvent.click(screen.getByTestId("trigger"));
    const composeBtn = await screen.findByRole("button", { name: /compose an email/i });
    fireEvent.click(composeBtn);
    expect(onCompose).toHaveBeenCalledWith("baz@example.com");
  });

  it("populates the global mail search input with 'from:<address>' when Search Emails from is clicked", async () => {
    const input = document.createElement("input");
    input.setAttribute("data-mail-search-input", "true");
    document.body.appendChild(input);

    let observedValue = "";
    input.addEventListener("input", (e) => {
      observedValue = (e.target as HTMLInputElement).value;
    });

    renderPopover({ address: "qux@example.com" });
    fireEvent.click(screen.getByTestId("trigger"));
    const searchBtn = await screen.findByRole("button", { name: /search emails from/i });
    fireEvent.click(searchBtn);

    expect(input.value).toBe("from:qux@example.com ");
    // Regression guard: the input event must fire with the NEW value so React's
    // onChange / setState pathway can sync. If the native value setter is not
    // used, _valueTracker is pre-synced and onChange is skipped, leaving
    // observedValue empty.
    expect(observedValue).toBe("from:qux@example.com ");
    document.body.removeChild(input);
  });

  it("falls back to an info toast when no mail search input exists", async () => {
    renderPopover({ address: "noinput@example.com" });
    fireEvent.click(screen.getByTestId("trigger"));
    const searchBtn = await screen.findByRole("button", { name: /search emails from/i });
    fireEvent.click(searchBtn);
    expect(mockToast).toHaveBeenCalledWith(
      "info",
      expect.stringMatching(/search unavailable/i),
      expect.any(String),
    );
  });

  it("shows 'Coming soon' info toast for the Block Sender stub", async () => {
    renderPopover({ address: "zap@example.com" });
    fireEvent.click(screen.getByTestId("trigger"));
    const blockBtn = await screen.findByRole("button", { name: /block sender/i });
    fireEvent.click(blockBtn);
    expect(mockToast).toHaveBeenCalledWith("info", "Coming soon", expect.any(String));
  });

  it("renders all six stub action rows (Group / Priority / Show in Primary / Notifications / Block / Category)", async () => {
    renderPopover();
    fireEvent.click(screen.getByTestId("trigger"));
    await screen.findByRole("button", { name: /compose an email/i });
    expect(screen.getByText("Group Emails")).toBeInTheDocument();
    expect(screen.getByText("Mark as Priority")).toBeInTheDocument();
    expect(screen.getByText("Show in Primary List")).toBeInTheDocument();
    expect(screen.getByText("Notifications")).toBeInTheDocument();
    expect(screen.getByText("Block Sender")).toBeInTheDocument();
    expect(screen.getByText("Uncategorized")).toBeInTheDocument();
  });
});
