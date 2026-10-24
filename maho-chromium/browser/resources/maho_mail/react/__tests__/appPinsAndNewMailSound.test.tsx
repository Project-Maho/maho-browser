import { render, act, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { App } from "../app";
import { TestProviders, createMockAccount, createMockEmail, createMockFolder } from "../test/mocks";
import type { EmailSummary } from "../types";
import * as api from "../api";
import type { PaletteCommand } from "../hooks/useCommandRegistry";

let capturedPaletteCommands: PaletteCommand[] = [];
const captured = vi.hoisted(() => ({
  pinnedIds: undefined as ReadonlySet<string> | undefined,
  onNewEmails: undefined as ((accountId: string, ids: string[]) => Promise<void>) | undefined,
  playSound: vi.fn(),
}));

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
  listPinnedEmails: vi.fn().mockResolvedValue([]),
  filterMutedMessageIds: vi.fn(),
  listThreadEmails: vi.fn(),
}));

vi.mock("../hooks/useAuthStatus", () => ({
  useAuthStatus: () => ({ authErrors: [], clearAuthError: vi.fn() }),
}));
vi.mock("../hooks/useReauthRetryQueue", () => ({
  useReauthRetryQueue: () => ({ enqueueRetry: vi.fn() }),
}));
vi.mock("../hooks/useAutoSync", () => ({
  useAutoSync: (opts: { onNewEmails?: (accountId: string, ids: string[]) => Promise<void> }) => {
    captured.onNewEmails = opts.onNewEmails;
  },
}));
vi.mock("../components/layout/EmailList", () => ({
  EmailList: ({ pinnedIds }: { pinnedIds?: ReadonlySet<string> }) => {
    captured.pinnedIds = pinnedIds;
    return null;
  },
}));
vi.mock("../hooks/useTrayBadge", () => ({ useTrayBadge: () => {} }));
vi.mock("../hooks/useAppLifecycle", () => ({ useAppLifecycle: () => {} }));
vi.mock("../hooks/useDeepLink", () => ({ useDeepLink: () => {} }));
vi.mock("../hooks/useTheme", () => ({ useTheme: () => {} }));
vi.mock("../hooks/useNetworkStatus", () => ({
  useNetworkStatus: () => ({ isOnline: true, pendingMutationCount: 0 }),
}));
vi.mock("../hooks/useNotificationSound", () => ({
  useNotificationSound: () => ({ playSound: captured.playSound }),
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

function summary(id: string, messageId: string): EmailSummary {
  return { ...createMockEmail({ id }), message_id: messageId } as unknown as EmailSummary;
}

describe("app pins and new-mail sound", () => {
  const folders = [createMockFolder({ folder_type: "Inbox" })];

  beforeEach(() => {
    vi.clearAllMocks();
    capturedPaletteCommands = [];
    captured.pinnedIds = undefined;
    captured.onNewEmails = undefined;
    localStorage.clear();
    vi.mocked(api.listAccounts).mockResolvedValue([createMockAccount({ id: "acc-1" })]);
    vi.mocked(api.listFolders).mockResolvedValue(folders);
    vi.mocked(api.syncFolders).mockResolvedValue(folders);
    vi.mocked(api.listEmails).mockResolvedValue([]);
    vi.mocked(api.listSavedSearches).mockResolvedValue([]);
    vi.mocked(api.listThreadEmails).mockResolvedValue([]);
  });

  it("populates pinnedIds from the server on load", async () => {
    vi.mocked(api.searchEmails).mockResolvedValue({ emails: [], total_count: 0, query: "" });
    vi.mocked(api.listPinnedEmails).mockResolvedValue([summary("pinned-1", "<p1@x>")] as never);

    await act(async () => {
      render(<App />, { wrapper: TestProviders });
    });

    await waitFor(() => expect(captured.pinnedIds?.has("pinned-1")).toBe(true));
  });

  async function renderApp() {
    vi.mocked(api.listPinnedEmails).mockResolvedValue([] as never);
    vi.mocked(api.searchEmails).mockResolvedValue({ emails: [], total_count: 0, query: "" });
    await act(async () => {
      render(<App />, { wrapper: TestProviders });
    });
    await waitFor(() => expect(captured.onNewEmails).toBeDefined());
    vi.mocked(api.searchEmails).mockClear();
  }

  it("does not play the sound when the arrival is in a muted thread", async () => {
    await renderApp();
    vi.mocked(api.filterMutedMessageIds).mockImplementation(async (_acc, ids) => ids.filter((id) => id !== "<muted@x>"));

    await act(async () => {
      await captured.onNewEmails?.("acc-1", ["<muted@x>"]);
    });

    expect(api.filterMutedMessageIds).toHaveBeenCalledWith("acc-1", ["<muted@x>"]);
    expect(captured.playSound).not.toHaveBeenCalled();
  });

  it("decides on the arrival id only, ignoring other unread mail", async () => {
    await renderApp();
    // An old unmuted unread email is visible in the unread window; it must
    // not make a muted-only arrival ring.
    vi.mocked(api.searchEmails).mockResolvedValue({ emails: [summary("old", "<old@x>")] as never, total_count: 1, query: "" });
    vi.mocked(api.filterMutedMessageIds).mockImplementation(async (_acc, ids) => ids.filter((id) => id !== "<muted@x>"));

    await act(async () => {
      await captured.onNewEmails?.("acc-1", ["<muted@x>"]);
    });

    expect(api.searchEmails).not.toHaveBeenCalled();
    expect(api.filterMutedMessageIds).toHaveBeenCalledTimes(1);
    expect(captured.playSound).not.toHaveBeenCalled();
  });

  it("treats a reply as muted when another message in its thread was muted", async () => {
    await renderApp();
    vi.mocked(api.listThreadEmails).mockResolvedValue([summary("root", "<root@x>"), summary("reply", "<reply@x>")] as never);
    vi.mocked(api.filterMutedMessageIds).mockImplementation(async (_acc, ids) => ids.filter((id) => id !== "<root@x>"));

    await act(async () => {
      await captured.onNewEmails?.("acc-1", ["<reply@x>"]);
    });

    expect(api.listThreadEmails).toHaveBeenCalledWith("acc-1", "<reply@x>");
    expect(captured.playSound).not.toHaveBeenCalled();
  });

  it("plays the sound for a new unmuted arrival", async () => {
    await renderApp();
    vi.mocked(api.filterMutedMessageIds).mockImplementation(async (_acc, ids) => ids);

    await act(async () => {
      await captured.onNewEmails?.("acc-1", ["<new@x>"]);
    });

    expect(captured.playSound).toHaveBeenCalledTimes(1);
  });

  it("fails open for an arrival without a Message-ID", async () => {
    await renderApp();

    await act(async () => {
      await captured.onNewEmails?.("acc-1", []);
    });

    expect(api.filterMutedMessageIds).not.toHaveBeenCalled();
    expect(captured.playSound).toHaveBeenCalledTimes(1);
  });
});
