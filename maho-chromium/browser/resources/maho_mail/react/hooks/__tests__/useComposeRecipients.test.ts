import { describe, it, expect, vi, beforeEach } from "vitest";
import { renderHook, act } from "@testing-library/react";
import { useComposeRecipients } from "../useComposeRecipients";
import type { MutableRefObject } from "react";

function makePrevIsOpenRef(value = false): MutableRefObject<boolean> {
  return { current: value };
}

function defaultProps() {
  return {
    initialTo: undefined as string | undefined,
    initialCc: undefined as string | undefined,
    initialBcc: undefined as string | undefined,
    replyTo: undefined as Parameters<typeof useComposeRecipients>[0]["replyTo"],
    forwardFrom: undefined as Parameters<typeof useComposeRecipients>[0]["forwardFrom"],
    effectiveAccountEmail: undefined as string | undefined,
    isOpen: false,
    prevIsOpenRef: makePrevIsOpenRef(false),
  };
}

describe("useComposeRecipients", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("initial state empty when no props", () => {
    const { result } = renderHook(() =>
      useComposeRecipients({ ...defaultProps(), isOpen: true }),
    );

    expect(result.current.to).toBe("");
    expect(result.current.cc).toBe("");
    expect(result.current.bcc).toBe("");
    expect(result.current.showCcBcc).toBe(false);
  });

  it("initial state from initialTo/initialCc/initialBcc props on first open", () => {
    const prevRef = makePrevIsOpenRef(false);
    const { result } = renderHook(() =>
      useComposeRecipients({
        ...defaultProps(),
        isOpen: true,
        prevIsOpenRef: prevRef,
        initialTo: "alice@example.com",
        initialCc: "bob@example.com",
        initialBcc: "charlie@example.com",
      }),
    );

    expect(result.current.to).toBe("alice@example.com");
    expect(result.current.cc).toBe("bob@example.com");
    expect(result.current.bcc).toBe("charlie@example.com");
  });

  it("initial state from replyTo (uses original_from + original_to_addresses for reply-all)", () => {
    const prevRef = makePrevIsOpenRef(false);
    const { result } = renderHook(() =>
      useComposeRecipients({
        ...defaultProps(),
        isOpen: true,
        prevIsOpenRef: prevRef,
        effectiveAccountEmail: "me@example.com",
        replyTo: {
          reply_all: true,
          original_from: "sender@example.com",
          original_to_addresses: "me@example.com, other@example.com",
          original_cc_addresses: "cc-person@example.com",
          original_subject: "Test",
          original_date: "2024-01-01",
          original_body_text: "body",
          original_body_html: null,
          original_message_id: "msg-1",
          original_references: "",
        },
      }),
    );

    // Reply-all: to should include sender + other (excluding self)
    expect(result.current.to).toContain("sender@example.com");
    expect(result.current.to).toContain("other@example.com");
    expect(result.current.to).not.toContain("me@example.com");
    // CC should include cc-person (excluding self)
    expect(result.current.cc).toContain("cc-person@example.com");
  });

  it("initial state from forwardFrom (empty to/cc)", () => {
    const prevRef = makePrevIsOpenRef(false);
    const { result } = renderHook(() =>
      useComposeRecipients({
        ...defaultProps(),
        isOpen: true,
        prevIsOpenRef: prevRef,
        forwardFrom: {
          reply_all: false,
          original_from: "sender@example.com",
          original_to_addresses: "recipient@example.com",
          original_cc_addresses: "",
          original_subject: "Fwd: Test",
          original_date: "2024-01-01",
          original_body_text: "body",
          original_body_html: null,
          original_message_id: "msg-2",
          original_references: "",
        },
      }),
    );

    expect(result.current.to).toBe("");
    expect(result.current.cc).toBe("");
    expect(result.current.bcc).toBe("");
  });

  it("showCcBcc=true when initialCc or initialBcc non-empty", () => {
    const prevRef = makePrevIsOpenRef(false);
    const { result } = renderHook(() =>
      useComposeRecipients({
        ...defaultProps(),
        isOpen: true,
        prevIsOpenRef: prevRef,
        initialCc: "someone@example.com",
      }),
    );

    expect(result.current.showCcBcc).toBe(true);
  });

  it("showCcBcc=true when reply-all has CC addresses", () => {
    const prevRef = makePrevIsOpenRef(false);
    const { result } = renderHook(() =>
      useComposeRecipients({
        ...defaultProps(),
        isOpen: true,
        prevIsOpenRef: prevRef,
        effectiveAccountEmail: "me@example.com",
        replyTo: {
          reply_all: true,
          original_from: "sender@example.com",
          original_to_addresses: "me@example.com",
          original_cc_addresses: "cc@example.com",
          original_subject: "Test",
          original_date: "2024-01-01",
          original_body_text: "body",
          original_body_html: null,
          original_message_id: "msg-1",
          original_references: "",
        },
      }),
    );

    expect(result.current.showCcBcc).toBe(true);
  });

  describe("parseRecipients", () => {
    it("splits on comma, trims, filters empty", () => {
      const { result } = renderHook(() =>
        useComposeRecipients({ ...defaultProps(), isOpen: true }),
      );

      expect(result.current.parseRecipients("a@b.com, c@d.com")).toEqual([
        "a@b.com",
        "c@d.com",
      ]);
      expect(result.current.parseRecipients("a@b.com,, ,c@d.com")).toEqual([
        "a@b.com",
        "c@d.com",
      ]);
    });

    it("splits on semicolon", () => {
      const { result } = renderHook(() =>
        useComposeRecipients({ ...defaultProps(), isOpen: true }),
      );

      expect(result.current.parseRecipients("a@b.com; c@d.com")).toEqual([
        "a@b.com",
        "c@d.com",
      ]);
    });

    it("handles 'Name <email>' format properly", () => {
      const { result } = renderHook(() =>
        useComposeRecipients({ ...defaultProps(), isOpen: true }),
      );

      expect(
        result.current.parseRecipients("Alice <a@b.com>, Bob <c@d.com>"),
      ).toEqual(["Alice <a@b.com>", "Bob <c@d.com>"]);
    });
  });

  describe("validateRecipients", () => {
    it("returns invalid list for malformed emails", () => {
      const prevRef = makePrevIsOpenRef(false);
      const { result } = renderHook(() =>
        useComposeRecipients({
          ...defaultProps(),
          isOpen: true,
          prevIsOpenRef: prevRef,
          initialTo: "valid@example.com, not-an-email, also@bad",
        }),
      );

      const validation = result.current.validateRecipients();
      expect(validation.invalid).toContain("not-an-email");
      expect(validation.invalid).toContain("also@bad");
      expect(validation.invalid).not.toContain("valid@example.com");
    });

    it("returns empty invalid array when all emails are valid", () => {
      const prevRef = makePrevIsOpenRef(false);
      const { result } = renderHook(() =>
        useComposeRecipients({
          ...defaultProps(),
          isOpen: true,
          prevIsOpenRef: prevRef,
          initialTo: "alice@example.com, Bob <bob@example.com>",
        }),
      );

      const validation = result.current.validateRecipients();
      expect(validation.invalid).toEqual([]);
    });
  });

  describe("hasInvalidEmails", () => {
    it("true when any address fails isValidEmail", () => {
      const prevRef = makePrevIsOpenRef(false);
      const { result } = renderHook(() =>
        useComposeRecipients({
          ...defaultProps(),
          isOpen: true,
          prevIsOpenRef: prevRef,
          initialTo: "valid@example.com, badaddr",
        }),
      );

      expect(result.current.hasInvalidEmails).toBe(true);
    });

    it("false when all addresses pass isValidEmail", () => {
      const prevRef = makePrevIsOpenRef(false);
      const { result } = renderHook(() =>
        useComposeRecipients({
          ...defaultProps(),
          isOpen: true,
          prevIsOpenRef: prevRef,
          initialTo: "alice@example.com",
        }),
      );

      expect(result.current.hasInvalidEmails).toBe(false);
    });
  });

  describe("first-open edge transition", () => {
    it("seeds recipients only on edge transition (prevIsOpenRef false→true)", () => {
      const prevRef = makePrevIsOpenRef(false);

      const { result, rerender } = renderHook(
        (props) => useComposeRecipients(props),
        {
          initialProps: {
            ...defaultProps(),
            isOpen: true,
            prevIsOpenRef: prevRef,
            initialTo: "first@example.com",
          },
        },
      );

      expect(result.current.to).toBe("first@example.com");

      // Simulate ComposeModal's main effect setting prevIsOpenRef after first-open
      act(() => {
        prevRef.current = true;
      });

      // Simulate updating the to field
      act(() => {
        result.current.setTo("edited@example.com");
      });

      expect(result.current.to).toBe("edited@example.com");

      // Re-render with same isOpen=true - should NOT re-seed (prevRef is true now)
      rerender({
        ...defaultProps(),
        isOpen: true,
        prevIsOpenRef: prevRef,
        initialTo: "should-not-appear@example.com",
      });

      expect(result.current.to).toBe("edited@example.com");
    });

    it("does not seed when prevIsOpenRef is already true (no edge)", () => {
      const prevRef = makePrevIsOpenRef(true);

      const { result } = renderHook(() =>
        useComposeRecipients({
          ...defaultProps(),
          isOpen: true,
          prevIsOpenRef: prevRef,
          initialTo: "should-not-seed@example.com",
        }),
      );

      // No edge transition: should stay empty
      expect(result.current.to).toBe("");
    });
  });
});
