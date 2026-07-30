import { describe, it, expect } from "vitest";
import { buildReplySubject, buildForwardSubject } from "../replySubject";

describe("buildReplySubject", () => {
  it("prepends Re: to a plain subject", () => {
    expect(buildReplySubject("hello")).toBe("Re: hello");
  });

  it("keeps Re: prefix as-is (idempotent)", () => {
    expect(buildReplySubject("Re: hello")).toBe("Re: hello");
  });

  it("normalises RE: (uppercase) to Re:", () => {
    expect(buildReplySubject("RE: hello")).toBe("Re: hello");
  });

  it("normalises re: (lowercase) to Re:", () => {
    expect(buildReplySubject("re: hello")).toBe("Re: hello");
  });

  it("strips Re[2]: numbered reply prefix", () => {
    expect(buildReplySubject("Re[2]: thread")).toBe("Re: thread");
  });

  it("collapses nested Re: RE: double prefix", () => {
    expect(buildReplySubject("Re: RE: double")).toBe("Re: double");
  });

  it("normalises Fwd: subject to Re: when replying", () => {
    expect(buildReplySubject("Fwd: x")).toBe("Re: x");
  });

  it("handles Korean 회신: prefix", () => {
    expect(buildReplySubject("회신: hello")).toBe("Re: hello");
  });

  it("handles Japanese 返信: prefix", () => {
    expect(buildReplySubject("返信: hello")).toBe("Re: hello");
  });

  it("collapses deeply nested Re: Re: Re: deep", () => {
    expect(buildReplySubject("Re: Re: Re: deep")).toBe("Re: deep");
  });
});

describe("buildForwardSubject", () => {
  it("prepends Fwd: to a plain subject", () => {
    expect(buildForwardSubject("hello")).toBe("Fwd: hello");
  });

  it("keeps Fwd: prefix as-is (idempotent)", () => {
    expect(buildForwardSubject("Fwd: hello")).toBe("Fwd: hello");
  });

  it("normalises FWD: (uppercase) to Fwd:", () => {
    expect(buildForwardSubject("FWD: hello")).toBe("Fwd: hello");
  });

  it("normalises FW: to Fwd:", () => {
    expect(buildForwardSubject("FW: hello")).toBe("Fwd: hello");
  });

  it("normalises Re: subject to Fwd: when forwarding", () => {
    expect(buildForwardSubject("Re: hello")).toBe("Fwd: hello");
  });
});
