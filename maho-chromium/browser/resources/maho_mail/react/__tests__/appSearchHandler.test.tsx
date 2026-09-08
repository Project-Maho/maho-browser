import React, { useState } from "react";
import { render, screen, act, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { App } from "../app";
import { TestProviders, createMockAccount, createMockEmail, createMockFolder } from "../test/mocks";
import * as api from "../api";
import type { PaletteCommand } from "../hooks/useCommandRegistry";

let capturedPaletteCommands: PaletteCommand[] = [];

vi.mock("../components/ui/CommandPalette", () => ({
  CommandPalette: ({ commands }: { commands: PaletteCommand[] }) => {
    capturedPaletteCommands = commands;
    return null;
  },
}));

vi.mock("../i18n", () => ({}));
vi.mock("../mojo_client", () => ({
  handler: {
    listAccounts: async () => ({
      ok: true,
      resultJson: '[{"id":"acc-1","email":"user@example.com","auth_type":"password"}]',
    }),
    getBrowserUiPrefs: async () => ({ prefsJson: "{}" }),
  },
  callbackRouter: {
    onAccountsChanged: { addListener: vi.fn() },
    onBrowserUiPrefsChanged: { addListener: vi.fn() },
    onLifecycleChanged: { addListener: vi.fn() },
    removeListener: vi.fn(),
  },
}));

vi.mock("../api", () => ({
  listAccounts: vi.fn(),
  listFolders: vi.fn(),
  syncFolders: vi.fn(),
  listEmails: vi.fn(),
  getEmail: vi.fn(),
  moveEmail: vi.fn(),
  deleteEmail: vi.fn(),
  reconnectAccount: vi.fn(),
  startOAuth2: vi.fn(),
  listSavedSearches: vi.fn(),
  searchEmails: vi.fn(),
  md5Hash: vi.fn().mockResolvedValue("hash"),
  getAppSetting: vi.fn().mockResolvedValue(null),
  startAutoSync: vi.fn(),
  stopAutoSync: vi.fn(),
  startScheduler: vi.fn(),
  stopScheduler: vi.fn(),
  listCalendarEvents: vi.fn().mockResolvedValue([]),
  listAccountCalendars: vi.fn().mockResolvedValue([]),
  listCalendarCategories: vi.fn().mockResolvedValue([]),
  toggleStar: vi.fn(),
  markRead: vi.fn(),
  getReplyContext: vi.fn(),
}));

vi.mock("../hooks/useAuthStatus", () => ({
  useAuthStatus: () => ({ authErrors: [], clearAuthError: vi.fn() }),
}));
vi.mock("../hooks/useReauthRetryQueue", () => ({
  useReauthRetryQueue: () => ({ enqueueRetry: vi.fn() }),
}));
vi.mock("../hooks/useAutoSync", () => ({ useAutoSync: () => {} }));
vi.mock("../hooks/useTrayBadge", () => ({ useTrayBadge: () => {} }));
vi.mock("../hooks/useAppLifecycle", () => ({ useAppLifecycle: () => {} }));
vi.mock("../hooks/useDeepLink", () => ({ useDeepLink: () => {} }));
vi.mock("../hooks/useTheme", () => ({ useTheme: () => {} }));
vi.mock("../hooks/useNetworkStatus", () => ({
  useNetworkStatus: () => ({ isOnline: true, pendingMutationCount: 0 }),
}));
vi.mock("../hooks/useNotificationSound", () => ({
  useNotificationSound: () => ({ playSound: vi.fn() }),
}));
vi.mock("../hooks/useNotifications", () => ({
  useNotifications: () => ({ notify: vi.fn() }),
}));
vi.mock("../hooks/useSettings", () => ({
  useSettings: () => ({ syncInterval: 0, sidebarWidth: 240, emailListWidth: 350 }),
}));
vi.mock("../components/compose/ComposeModal", () => ({
  ComposeModal: ({ isOpen }: { isOpen: boolean }) =>
    isOpen ? <div data-testid="mail-compose" /> : null,
}));
vi.mock("../components/account/AddAccountModal", () => ({
  AddAccountModal: () => null,
}));
vi.mock("../components/common/OnboardingTour", () => ({
  OnboardingTour: () => null,
}));
vi.mock("../components/calendar/CreateEventDialog", () => ({
  CreateEventDialog: () => null,
}));
vi.mock("../components/layout/EmailContent", () => ({
  EmailContent: () => <div data-testid="email-content" />,
}));

describe("DEFECT-07: app.tsx smart-search controlled React state", () => {
  const email = createMockEmail({ id: "fixture", subject: "Test subject", is_read: true });
  const folders = [
    createMockFolder({ folder_type: "Inbox" }),
    createMockFolder({ id: "archive-1", folder_type: "Archive" }),
  ];

  beforeEach(() => {
    vi.clearAllMocks();
    capturedPaletteCommands = [];
    localStorage.clear();
    Object.defineProperty(HTMLElement.prototype, "offsetHeight", {
      configurable: true,
      get: () => 600,
    });
    vi.mocked(api.listAccounts).mockResolvedValue([createMockAccount()]);
    vi.mocked(api.listFolders).mockResolvedValue(folders);
    vi.mocked(api.syncFolders).mockResolvedValue(folders);
    vi.mocked(api.listEmails).mockResolvedValue([email]);
    vi.mocked(api.listSavedSearches).mockResolvedValue([]);
    vi.mocked(api.searchEmails).mockResolvedValue({ emails: [email], total_count: 1, query: "" });
  });

  it("dispatches bubbling input event so React controlled state synchronizes on smart-search command", async () => {
    await act(async () => {
      render(<App />, { wrapper: TestProviders });
    });

    const searchInput = document.querySelector<HTMLInputElement>("[data-mail-search-input]");
    expect(searchInput).not.toBeNull();
    if (!searchInput) return;

    let observedValueOnInput = "";
    const inputEventSpy = vi.fn((e: Event) => {
      observedValueOnInput = (e.target as HTMLInputElement).value;
    });
    searchInput.addEventListener("input", inputEventSpy);

    // Find smart-search command from CommandPalette commands
    const smartSearchCmd = capturedPaletteCommands.find((c) => c.id === "smart-search");
    expect(smartSearchCmd).toBeDefined();
    if (!smartSearchCmd) return;

    // Execute the smart-search handler
    await act(async () => {
      smartSearchCmd.handler();
    });

    // The input event must be dispatched so React's onChange / setState pathway fires
    expect(inputEventSpy).toHaveBeenCalledTimes(1);
    expect(observedValueOnInput).toBe("? ");
    expect(searchInput.value).toBe("? ");
    expect(document.activeElement).toBe(searchInput);
    expect(searchInput.selectionStart).toBe(2);
    expect(searchInput.selectionEnd).toBe(2);
  });

  it("updates React controlled input state without being wiped on subsequent render", async () => {
    await act(async () => {
      render(<App />, { wrapper: TestProviders });
    });

    const searchInput = document.querySelector<HTMLInputElement>("[data-mail-search-input]");
    expect(searchInput).not.toBeNull();
    if (!searchInput) return;

    const smartSearchCmd = capturedPaletteCommands.find((c) => c.id === "smart-search");
    expect(smartSearchCmd).toBeDefined();
    if (!smartSearchCmd) return;

    await act(async () => {
      smartSearchCmd.handler();
    });

    // When React reconciles controlled state, input value must remain "? "
    expect(searchInput.value).toBe("? ");

    // Focus / blur cycle or typing additional text should preserve the "? " prefix
    fireEvent.change(searchInput, { target: { value: "? invoice" } });
    expect(searchInput.value).toBe("? invoice");
  });
});
