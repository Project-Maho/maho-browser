import { act, renderHook } from "@testing-library/react";
import { expect, it, vi } from "vitest";
import { useQuickReply } from "../useQuickReply";
import * as api from "../../api";
import type { Email } from "../../types";
vi.mock("../../api", () => ({ sendEmail: vi.fn().mockResolvedValue(undefined), getReplyContext: vi.fn().mockResolvedValue({ original_message_id: "<m>", original_references: "<parent>" }) }));
it("C13 reply-all excludes the sending account", async () => {
 const email = { id: "m", account_id: "a", from_address: "sender@example.com", to_addresses: '"Self" <self@example.com>, other@example.com', subject: "topic", message_id: "<m>" } as Email;
 const options = { email, emailDetail: null, accountEmail: "self@example.com", toast: vi.fn(), t: (k: string) => k };
 const { result } = renderHook(() => useQuickReply(options));
 act(() => result.current.setQuickReplyText("answer"));
 await act(async () => result.current.handleQuickReply(true));
 expect(api.sendEmail).toHaveBeenLastCalledWith(expect.objectContaining({ to: ["sender@example.com", "other@example.com"] }));
});
it("C13 self-reply retains original recipients when isSelf filter empties all", async () => {
 const email = { id: "m", account_id: "a", from_address: "self@example.com", to_addresses: "self@example.com", subject: "note to self", message_id: "<m>" } as Email;
 const options = { email, emailDetail: null, accountEmail: "self@example.com", toast: vi.fn(), t: (k: string) => k };
 const { result } = renderHook(() => useQuickReply(options));
 act(() => result.current.setQuickReplyText("noting"));
 await act(async () => result.current.handleQuickReply(true));
 const call = vi.mocked(api.sendEmail).mock.calls.at(-1)?.[0];
 expect(call.to).toEqual(expect.any(Array));
 expect(call.to.length).toBeGreaterThan(0);
});
it("C13 quick reply carries thread metadata", async () => {
 const email = { id: "m", account_id: "a", from_address: "sender@example.com", to_addresses: "self@example.com", subject: "topic", message_id: "<m>" } as Email;
 const { result } = renderHook(() => useQuickReply({ email, emailDetail: null, toast: vi.fn(), t: k => k }));
 act(() => result.current.setQuickReplyText("answer"));
 await act(async () => result.current.handleQuickReply(false));
 expect(api.sendEmail).toHaveBeenCalledWith(expect.objectContaining({ in_reply_to: "<m>", references: "<parent> <m>" }));
});
it("C02/C13 quick reply provides composition options to onSendRequested for recovery", async () => {
 const email = { id: "m", account_id: "a", from_address: "sender@example.com", to_addresses: "self@example.com", subject: "topic", message_id: "<m>" } as Email;
 const onSendRequested = vi.fn();
 const { result } = renderHook(() => useQuickReply({ email, emailDetail: null, onSendRequested, toast: vi.fn(), t: k => k }));
 act(() => result.current.setQuickReplyText("quick answer"));
 await act(async () => result.current.handleQuickReply(false));
 expect(onSendRequested).toHaveBeenCalledWith(
   expect.objectContaining({ body_text: "quick answer" }),
   expect.objectContaining({
     composition: expect.objectContaining({
       accountId: "a",
       body: "quick answer",
       to: "sender@example.com",
     }),
   }),
 );
});
