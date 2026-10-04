import { beforeEach, describe, expect, it, vi } from "vitest";
import type { CreateAccountRequest } from "../mail_types";

const handler = vi.hoisted(() => ({ testConnection: vi.fn() }));
vi.mock("../mojo_client.js", () => ({ handler }));
import { testConnection } from "../api/index";

const request: CreateAccountRequest = {
  email: "test@example.test", display_name: "Test", auth_type: "password",
  imap_host: "imap.example.test", imap_port: 993, imap_encryption: "ssl",
  smtp_host: "smtp.example.test", smtp_port: 587, smtp_encryption: "starttls",
  username: "test@example.test", password: "fixture-only",
};

beforeEach(() => vi.clearAllMocks());

describe("Mail account wire contracts", () => {
  it.each([true, false])("unwraps probe status %s from its JSON response", async (ok) => {
    handler.testConnection.mockResolvedValue({ ok: true, resultJson: JSON.stringify({ ok }) });

    const result = await testConnection(request);

    expect(result).toBe(ok);
    expect(handler.testConnection).toHaveBeenCalledExactlyOnceWith(JSON.stringify(request));
  });

  it("rejects a failed probe instead of returning success", async () => {
    handler.testConnection.mockResolvedValue({ ok: false, resultJson: "connection refused" });

    await expect(testConnection(request)).rejects.toThrow("connection refused");
  });
});
