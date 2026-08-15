import { describe, expect, test } from "bun:test";

import {
  createLiveObservablesAdapter,
  parseLifecycleEventDto,
  parseNavigationEventDto,
  parseProfileEventDto,
  type LiveObservableReaders,
} from "./task12_live_observables.js";

const PROFILE_ID = "chromium-profile-7";
const MAHO_PROFILE_ID = "maho-profile-9";

function readers(
  overrides: Partial<LiveObservableReaders> = {},
): LiveObservableReaders {
  return {
    readActiveChromiumProfileId: () => PROFILE_ID,
    readActiveMahoProfileId: () => MAHO_PROFILE_ID,
    readProfileSnapshot: () => ({ id: MAHO_PROFILE_ID, revision: 41, name: "Work" }),
    readGlobalSnapshot: () => ({ revision: 12, theme: "dark" }),
    readSpaceSnapshot: () => ({ id: "space-3", revision: 8, name: "Research" }),
    readLifecycle: () => ({
      revision: 17,
      ready: [MAHO_PROFILE_ID],
      deleting: ["maho-profile-10"],
      deleted: ["maho-profile-11"],
    }),
    readBrowserContext: () => ({
      targetId: "target-2",
      windowId: 22,
      focused: true,
      url: "https://example.test/path",
      title: "Example",
      sanitizedDom: '<main id="content">safe</main>',
    }),
    persistObservation: () => undefined,
    ...overrides,
  };
}

describe("live observables adapter", () => {
  test("independently reads active Chromium and Maho profile IDs", () => {
    const calls: string[] = [];
    const adapter = createLiveObservablesAdapter(readers({
      readActiveChromiumProfileId: () => { calls.push("chromium"); return "chromium-live"; },
      readActiveMahoProfileId: () => { calls.push("maho"); return "maho-live"; },
      readProfileSnapshot: () => ({ id: "maho-live", revision: 41 }),
    }));

    expect(adapter.readAvailability()).toEqual({
      status: "available",
      chromiumProfileId: "chromium-live",
      mahoProfileId: "maho-live",
    });
    expect(calls).toEqual(["chromium", "maho"]);
  });

  test("never substitutes an expected-host literal or fallback ID", () => {
    const adapter = createLiveObservablesAdapter(readers({
      readActiveChromiumProfileId: () => null,
      readActiveMahoProfileId: () => null,
    }));

    expect(adapter.readAvailability()).toEqual({
      status: "unavailable",
      reason: "active-profile-unavailable",
    });
    expect(JSON.stringify(adapter.readAvailability())).not.toContain("expected-host");
    expect(JSON.stringify(adapter.readAvailability())).not.toContain(PROFILE_ID);
    expect(JSON.stringify(adapter.readAvailability())).not.toContain(MAHO_PROFILE_ID);
  });

  test("null IDs are unavailable and a snapshot/profile mismatch is blocked", () => {
    expect(createLiveObservablesAdapter(readers({
      readActiveChromiumProfileId: () => null,
    })).readAvailability()).toEqual({
      status: "unavailable",
      reason: "active-profile-unavailable",
    });

    expect(createLiveObservablesAdapter(readers({
      readProfileSnapshot: () => ({ id: "different-profile", revision: 41 }),
    })).readAvailability()).toEqual({
      status: "blocked",
      reason: "active-profile-mismatch",
      chromiumProfileId: PROFILE_ID,
      mahoProfileId: MAHO_PROFILE_ID,
      snapshotProfileId: "different-profile",
    });
  });

  test("uses the real lifecycle DTO, including revisions and nonempty states", () => {
    const adapter = createLiveObservablesAdapter(readers());
    expect(adapter.readLifecycle()).toEqual({
      revision: 17,
      ready: [MAHO_PROFILE_ID],
      deleting: ["maho-profile-10"],
      deleted: ["maho-profile-11"],
    });
  });

  test("persists browser and settings observables together in one before/after object", () => {
    const persisted: unknown[] = [];
    const adapter = createLiveObservablesAdapter(readers({
      persistObservation: value => { persisted.push(value); },
    }));

    const before = adapter.capture();
    const after = adapter.capture();
    const observation = adapter.persistBeforeAfter(before, after);

    expect(persisted).toEqual([observation]);
    expect(observation).toEqual({ before, after });
    expect(before).toMatchObject({
      chromiumProfileId: PROFILE_ID,
      mahoProfileId: MAHO_PROFILE_ID,
      profile: { id: MAHO_PROFILE_ID, revision: 41 },
      global: { revision: 12 },
      space: { id: "space-3", revision: 8 },
      lifecycle: { revision: 17 },
      browser: {
        targetId: "target-2",
        windowId: 22,
        focused: true,
        url: "https://example.test/path",
        title: "Example",
        sanitizedDom: '<main id="content">safe</main>',
      },
    });
  });
});

describe("exact event DTO parsing", () => {
  test("navigation DTO carries real target/window/context and navigation token", () => {
    const dto = {
      type: "navigation" as const,
      targetId: "target-2",
      windowId: 22,
      contextId: "context-4",
      navigationToken: "nav-token-6",
      url: "https://example.test/next",
      title: "Next",
    };
    expect(parseNavigationEventDto(dto)).toEqual(dto);
  });

  test("profile DTO carries both real profile IDs and profile revision", () => {
    const dto = {
      type: "profile" as const,
      chromiumProfileId: PROFILE_ID,
      mahoProfileId: MAHO_PROFILE_ID,
      profileRevision: 42,
      contextId: "context-4",
    };
    expect(parseProfileEventDto(dto)).toEqual(dto);
  });

  test("lifecycle DTO carries real profile ID, token, state, and revision", () => {
    const dto = {
      type: "lifecycle" as const,
      mahoProfileId: MAHO_PROFILE_ID,
      lifecycleToken: "lifecycle-token-3",
      state: "deleting" as const,
      lifecycleRevision: 18,
      profileRevision: 42,
      contextId: "context-4",
    };
    expect(parseLifecycleEventDto(dto)).toEqual(dto);
  });

  test("malformed event DTOs fail closed", () => {
    expect(parseNavigationEventDto({ type: "navigation", targetId: "target-2" })).toBeNull();
    expect(parseNavigationEventDto({
      type: "navigation", targetId: "target-2", windowId: -1, contextId: "context-4",
      navigationToken: "nav-token", url: "javascript:alert(1)", title: "bad",
    })).toBeNull();
    expect(parseProfileEventDto({
      type: "profile", chromiumProfileId: PROFILE_ID, mahoProfileId: "",
      profileRevision: 42, contextId: "context-4",
    })).toBeNull();
    expect(parseLifecycleEventDto({
      type: "lifecycle", mahoProfileId: MAHO_PROFILE_ID, lifecycleToken: "token",
      state: "unknown", lifecycleRevision: 18, profileRevision: 42, contextId: "context-4",
    })).toBeNull();
  });
});
