import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";

const root = resolve(import.meta.dirname, "..");

function read(relativePath: string) {
  return readFileSync(resolve(root, relativePath), "utf8");
}

describe("Mail-specific Agent surface removal", () => {
  it("does not mount or import the legacy Agent panel", () => {
    const app = read("app.tsx");
    const emailList = read("components/layout/EmailList.tsx");

    expect(app).not.toContain("AgentPanel");
    expect(app).not.toContain("showAgent");
    expect(emailList).not.toContain("onOpenAgent");
  });

  it("does not expose Agent session APIs from the Mail frontend", () => {
    const api = read("api/index.ts");
    const types = read("types/index.ts");

    for (const symbol of [
      "listAgentSessions",
      "getAgentMessages",
      "createAgentSession",
      "deleteAgentSession",
      "sendAgentMessage",
      "AgentChatSession",
      "AgentChatMessage",
      "AgentStreamEvent",
    ]) {
      expect(api + types).not.toContain(symbol);
    }
  });
});
