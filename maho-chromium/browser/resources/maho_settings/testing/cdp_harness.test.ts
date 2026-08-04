import {describe, expect, test} from "bun:test";

import {CDP} from "./cdp_harness";

type CDPResponse = {
  readonly result?: unknown;
};

class FakeWebSocket {
  onmessage: ((event: MessageEvent) => void) | null = null;
  readonly sent: Array<Record<string, unknown>> = [];

  constructor(private readonly response: CDPResponse) {}

  send(data: string): void {
    const request = JSON.parse(data) as Record<string, unknown>;
    this.sent.push(request);
    queueMicrotask(() => {
      this.onmessage?.({data: JSON.stringify({id: request.id, ...this.response})} as MessageEvent);
    });
  }
}

describe("CDP.eval", () => {
  test("returns Runtime.evaluate values by value and awaits promises", async () => {
    const expected = {profile: "ready"};
    const ws = new FakeWebSocket({result: {result: {value: expected}}});
    const cdp = new CDP(ws as unknown as WebSocket);

    await expect(cdp.eval("provisionProfile()", true)).resolves.toEqual(expected);
    expect(ws.sent).toEqual([
      {
        id: 1,
        method: "Runtime.evaluate",
        params: {
          expression: "provisionProfile()",
          returnByValue: true,
          awaitPromise: true,
        },
      },
    ]);
  });

  test("rejects Runtime.evaluate exception descriptions", async () => {
    const description = "Error: Profile provisioning failed: Mojo rejected";
    const ws = new FakeWebSocket({
      result: {
        exceptionDetails: {
          exception: {description},
        },
      },
    });
    const cdp = new CDP(ws as unknown as WebSocket);

    await expect(cdp.eval("provisionProfile()", true)).rejects.toThrow(description);
  });
});
