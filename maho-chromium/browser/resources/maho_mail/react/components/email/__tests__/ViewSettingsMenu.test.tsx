import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { ViewSettingsMenu } from "../ViewSettingsMenu";

describe("ViewSettingsMenu", () => {
  const baseProps = {
    darkMode: true,
    onToggleDarkMode: vi.fn(),
    imagesBlocked: true,
    blockedImageCount: 0,
    onToggleImagesBlocked: vi.fn(),
    trackerCount: 0,
    trackerDomains: [] as string[],
    hasActiveTranslation: false,
    showTranslation: false,
    onToggleShowTranslation: vi.fn(),
  };

  beforeEach(() => {
    vi.clearAllMocks();
  });

  describe("trigger button", () => {
    it("renders a single 'View settings' trigger button", () => {
      render(<ViewSettingsMenu {...baseProps} />);
      expect(screen.getByRole("button", { name: /view settings/i })).toBeInTheDocument();
    });

    it("does NOT show a 'blocking active' indicator when nothing is blocked", () => {
      render(<ViewSettingsMenu {...baseProps} blockedImageCount={0} trackerCount={0} />);
      const trigger = screen.getByRole("button", { name: /view settings/i });
      expect(trigger.querySelector('[data-testid="view-settings-blocking-dot"]')).toBeNull();
    });

    it("shows a 'blocking active' dot when any images are blocked", () => {
      render(
        <ViewSettingsMenu
          {...baseProps}
          imagesBlocked
          blockedImageCount={3}
        />,
      );
      const trigger = screen.getByRole("button", { name: /view settings/i });
      expect(trigger.querySelector('[data-testid="view-settings-blocking-dot"]')).not.toBeNull();
    });

    it("shows a 'blocking active' dot when any trackers are blocked", () => {
      render(<ViewSettingsMenu {...baseProps} trackerCount={2} trackerDomains={["a.com", "b.com"]} />);
      const trigger = screen.getByRole("button", { name: /view settings/i });
      expect(trigger.querySelector('[data-testid="view-settings-blocking-dot"]')).not.toBeNull();
    });
  });

  describe("popover content", () => {
    it("reveals Rendering section with Dark mode and Show images switches when opened", async () => {
      render(<ViewSettingsMenu {...baseProps} />);
      fireEvent.click(screen.getByRole("button", { name: /view settings/i }));

      await waitFor(() => {
        expect(screen.getByRole("switch", { name: /dark mode/i })).toBeInTheDocument();
      });
      expect(screen.getByRole("switch", { name: /show remote images/i })).toBeInTheDocument();
    });

    it("shows blocked image count next to the Show images switch", async () => {
      render(
        <ViewSettingsMenu
          {...baseProps}
          imagesBlocked
          blockedImageCount={5}
        />,
      );
      fireEvent.click(screen.getByRole("button", { name: /view settings/i }));

      await waitFor(() => {
        expect(screen.getByText(/5 blocked/i)).toBeInTheDocument();
      });
    });

    it("invokes onToggleDarkMode when the Dark mode switch is clicked", async () => {
      render(<ViewSettingsMenu {...baseProps} />);
      fireEvent.click(screen.getByRole("button", { name: /view settings/i }));

      const sw = await screen.findByRole("switch", { name: /dark mode/i });
      fireEvent.click(sw);
      expect(baseProps.onToggleDarkMode).toHaveBeenCalledTimes(1);
    });

    it("invokes onToggleImagesBlocked when the Show images switch is clicked", async () => {
      render(<ViewSettingsMenu {...baseProps} />);
      fireEvent.click(screen.getByRole("button", { name: /view settings/i }));

      const sw = await screen.findByRole("switch", { name: /show remote images/i });
      fireEvent.click(sw);
      expect(baseProps.onToggleImagesBlocked).toHaveBeenCalledTimes(1);
    });

    it("renders Privacy tracker info read-only when trackerCount > 0", async () => {
      render(
        <ViewSettingsMenu
          {...baseProps}
          trackerCount={3}
          trackerDomains={["mailchimp.com", "sendgrid.net", "doubleclick.net"]}
        />,
      );
      fireEvent.click(screen.getByRole("button", { name: /view settings/i }));

      await waitFor(() => {
        expect(screen.getByText(/3 trackers blocked/i)).toBeInTheDocument();
      });
      expect(screen.getByText(/mailchimp\.com/)).toBeInTheDocument();
    });

    it("does NOT render Privacy section when trackerCount is 0", async () => {
      render(<ViewSettingsMenu {...baseProps} trackerCount={0} />);
      fireEvent.click(screen.getByRole("button", { name: /view settings/i }));

      await waitFor(() => {
        expect(screen.getByRole("switch", { name: /dark mode/i })).toBeInTheDocument();
      });
      expect(screen.queryByText(/trackers blocked/i)).toBeNull();
    });

    it("does NOT render Translation section when hasActiveTranslation is false", async () => {
      render(<ViewSettingsMenu {...baseProps} hasActiveTranslation={false} />);
      fireEvent.click(screen.getByRole("button", { name: /view settings/i }));

      await waitFor(() => {
        expect(screen.getByRole("switch", { name: /dark mode/i })).toBeInTheDocument();
      });
      expect(screen.queryByRole("switch", { name: /show translation/i })).toBeNull();
    });

    it("renders the Show translation switch when hasActiveTranslation is true", async () => {
      render(
        <ViewSettingsMenu
          {...baseProps}
          hasActiveTranslation
          showTranslation
        />,
      );
      fireEvent.click(screen.getByRole("button", { name: /view settings/i }));

      await waitFor(() => {
        expect(screen.getByRole("switch", { name: /show translation/i })).toBeInTheDocument();
      });
    });

    it("invokes onToggleShowTranslation when the Show translation switch is clicked", async () => {
      render(
        <ViewSettingsMenu
          {...baseProps}
          hasActiveTranslation
          showTranslation
        />,
      );
      fireEvent.click(screen.getByRole("button", { name: /view settings/i }));

      const sw = await screen.findByRole("switch", { name: /show translation/i });
      fireEvent.click(sw);
      expect(baseProps.onToggleShowTranslation).toHaveBeenCalledTimes(1);
    });
  });
});
