import assert from "node:assert/strict";
import test from "node:test";

import {
  cleanupOwnedBrowser,
  type CleanupInventory,
  type CleanupPrimitives,
  type CleanupReceipt,
} from "./task12_final_cleanup.js";

// Standalone contract coverage for the final cleanup adapter.

const marker = "/tmp/maho-owned-profile";

function inventory(): CleanupInventory {
  return {
    baselineProcesses: [
      { pid: 10, startToken: "baseline-start", userDataDir: marker },
      { pid: 11, startToken: "other-baseline", userDataDir: "/tmp/unrelated" },
    ],
    baselineProfileIds: ["baseline-profile"],
    baselineProfileNames: ["Baseline"],
    baselineTargetIds: ["baseline-target"],
    ownedProcess: { pid: 20, startToken: "owned-start", userDataDir: marker },
    createdProfileIds: ["created-profile"],
    createdProfileNames: ["Created"],
    createdTargetIds: ["created-target"],
    tempRoot: marker,
  };
}

function harness(overrides: Partial<CleanupPrimitives> = {}) {
  const calls: string[] = [];
  const persisted: CleanupReceipt[] = [];
  let processes = [
    { pid: 10, parentPid: 1, startToken: "baseline-start", userDataDir: marker },
    { pid: 20, parentPid: 1, startToken: "owned-start", userDataDir: marker },
    { pid: 21, parentPid: 20, startToken: "child-start", userDataDir: marker },
    { pid: 22, parentPid: 21, startToken: "grandchild-start", userDataDir: marker },
    { pid: 20, parentPid: 1, startToken: "reused-pid", userDataDir: marker },
    { pid: 30, parentPid: 1, startToken: "foreign-start", userDataDir: marker },
    { pid: 31, parentPid: 1, startToken: "foreign-dir", userDataDir: "/tmp/foreign" },
  ];
  let targets = [
    { id: "baseline-target", profileId: "baseline-profile", profileName: "Baseline" },
    { id: "created-target", profileId: "created-profile", profileName: "Created" },
  ];
  let rootExists = true;

  const primitives: CleanupPrimitives = {
    closeBrowser: async () => { calls.push("closeBrowser"); },
    listProcesses: async () => { calls.push("listProcesses"); return processes; },
    signalProcess: async (process, signal) => {
      calls.push(`${signal}:${process.pid}:${process.startToken}`);
      processes = processes.filter((candidate) => candidate.pid !== process.pid || candidate.startToken !== process.startToken);
    },
    waitForExit: async (identity, signal) => {
      calls.push(`wait:${signal}:${identity.pid}:${identity.startToken}`);
      return !processes.some((process) => process.pid === identity.pid && process.startToken === identity.startToken);
    },
    listTargets: async () => { calls.push("listTargets"); return targets; },
    closeTarget: async (id) => { calls.push(`closeTarget:${id}`); targets = targets.filter((target) => target.id !== id); },
    removeTempRoot: async (path) => { calls.push(`removeTempRoot:${path}`); rootExists = false; },
    statPath: async (path) => { calls.push(`statPath:${path}`); return rootExists; },
    persistReceipt: async (receipt) => { calls.push("persistReceipt"); persisted.push(structuredClone(receipt)); },
    ...overrides,
  };

  return { calls, persisted, primitives };
}

test("cleans only owned identities and descendants and persists a final receipt", async () => {
  const { calls, persisted, primitives } = harness();
  const receipt = await cleanupOwnedBrowser(inventory(), primitives);

  assert.equal(receipt.passed, true);
  assert.deepEqual(receipt.cleaned.profileIds, ["created-profile"]);
  assert.deepEqual(receipt.cleaned.profileNames, ["Created"]);
  assert.deepEqual(receipt.cleaned.targetIds, ["created-target"]);
  assert.deepEqual(receipt.cleaned.processes.map(({ pid }) => pid).sort(), [20, 21, 22]);
  assert.equal(calls[0], "closeBrowser");
  assert.ok(calls.indexOf("closeBrowser") < calls.findIndex((call) => call.startsWith("TERM:")));
  assert.ok(calls.includes("TERM:20:owned-start"));
  assert.ok(calls.includes("TERM:21:child-start"));
  assert.ok(calls.includes("TERM:22:grandchild-start"));
  assert.ok(!calls.includes("TERM:10:baseline-start"), "baseline identity must never be signaled");
  assert.ok(!calls.includes("TERM:20:reused-pid"), "a reused PID must not match without its start token");
  assert.ok(!calls.includes("TERM:30:foreign-start"), "marker alone does not establish ownership");
  assert.ok(!calls.includes("TERM:31:foreign-dir"));
  assert.ok(calls.indexOf(`removeTempRoot:${marker}`) < calls.indexOf(`statPath:${marker}`));
  assert.deepEqual(receipt.remainingProcesses, []);
  assert.deepEqual(receipt.remainingTargetIds, []);
  assert.equal(receipt.tempRootAbsent, true);
  assert.equal(calls.at(-1), "persistReceipt");
  assert.deepEqual(persisted, [receipt]);
});

test("uses bounded TERM to KILL escalation through the injected wait primitive", async () => {
  const waits: string[] = [];
  const { calls, primitives } = harness({
    waitForExit: async (identity, signal) => {
      waits.push(`${signal}:${identity.pid}:${identity.startToken}`);
      return signal === "KILL";
    },
  });

  const receipt = await cleanupOwnedBrowser(inventory(), primitives);
  assert.equal(receipt.passed, true);
  assert.ok(calls.indexOf("closeBrowser") < calls.findIndex((call) => call.startsWith("TERM:")));
  assert.ok(calls.some((call) => call.startsWith("KILL:")));
  assert.ok(waits.some((call) => call.startsWith("TERM:")));
  assert.ok(waits.some((call) => call.startsWith("KILL:")));
});

test("fails the receipt for cleanup errors, remaining ownership, or temp-root absence failure", async () => {
  const cases: Array<Partial<CleanupPrimitives>> = [
    { closeBrowser: async () => { throw new Error("close failed"); } },
    { listProcesses: async () => [{ pid: 20, parentPid: 1, startToken: "owned-start", userDataDir: marker }] },
    { statPath: async () => true },
    { listTargets: async () => [{ id: "created-target", profileId: "created-profile", profileName: "Created" }] },
  ];

  for (const overrides of cases) {
    const { persisted, primitives } = harness(overrides);
    const receipt = await cleanupOwnedBrowser(inventory(), primitives);
    assert.equal(receipt.passed, false);
    assert.deepEqual(persisted, [receipt]);
  }
});

test("is idempotent and returns the same persisted receipt on a second call", async () => {
  const { persisted, primitives } = harness();
  const first = await cleanupOwnedBrowser(inventory(), primitives);
  const second = await cleanupOwnedBrowser(inventory(), primitives);

  assert.deepEqual(second, first);
  assert.equal(persisted.length, 1);
});
