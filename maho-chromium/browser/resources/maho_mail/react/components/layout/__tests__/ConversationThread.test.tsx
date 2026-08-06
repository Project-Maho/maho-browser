import { render, screen, waitFor, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, it, expect, vi } from "vitest";

import { ConversationThread } from "../ConversationThread";
import type { EmailDetail, EmailSummary } from "../../../types";

const { getEmail } = vi.hoisted(() => ({ getEmail: vi.fn() }));

vi.mock("../../ui/Toast", () => ({ useToast: () => ({ toast: vi.fn() }) }));

vi.mock("../EmailBodyView", () => ({
  EmailBodyView: ({ emailDetail }: { emailDetail: EmailDetail }) => (
    <div data-testid={`body-${emailDetail.email.id}`}>body {emailDetail.email.id}</div>
  ),
}));

vi.mock("../../../api", () => ({ getEmail: (id: string) => getEmail(id) }));

getEmail.mockImplementation((id: string) =>
  Promise.resolve({ email: { id, subject: `S ${id}` }, attachments: [] } as unknown as EmailDetail),
);

function summary(over: Partial<EmailSummary> & { id: string; date: string }): EmailSummary {
  return {
    account_id: "acct",
    folder_id: "f",
    uid: 1,
    message_id: `${over.id}@m`,
    subject: "Subject",
    from_address: `${over.id}@x`,
    from_name: over.id.toUpperCase(),
    snippet: `snippet-${over.id}`,
    is_read: true,
    is_starred: false,
    is_draft: false,
    has_attachments: false,
    ...over,
  };
}

const A = summary({ id: "a", date: "2026-08-05T13:45:00Z", from_name: "Alice", is_read: true });
const B = summary({ id: "b", date: "2026-08-05T13:57:00Z", from_name: "Bob", is_read: true });
const C = summary({ id: "c", date: "2026-08-05T14:07:00Z", from_name: "Carol", is_read: false });

const selectedDetail = { email: { id: "a", subject: "S a" }, attachments: [] } as unknown as EmailDetail;

describe("ConversationThread", () => {
  it("orders messages chronologically regardless of input order", () => {
    render(
      <ConversationThread threadEmails={[C, A, B]} selectedEmailId="a" selectedDetail={selectedDetail} />,
    );
    const names = screen
      .getAllByRole("button")
      .map((b) => b.getAttribute("aria-label") || "")
      .filter(Boolean);
    const idxAlice = names.findIndex((n) => n.startsWith("Alice"));
    const idxBob = names.findIndex((n) => n.startsWith("Bob"));
    const idxCarol = names.findIndex((n) => n.startsWith("Carol"));
    expect(idxAlice).toBeLessThan(idxBob);
    expect(idxBob).toBeLessThan(idxCarol);
  });

  it("expands selected + latest + unread by default and collapses the rest", async () => {
    render(
      <ConversationThread threadEmails={[C, A, B]} selectedEmailId="a" selectedDetail={selectedDetail} />,
    );
    expect(screen.getByTestId("body-a")).toBeInTheDocument();
    await waitFor(() => expect(screen.getByTestId("body-c")).toBeInTheDocument());
    expect(screen.queryByTestId("body-b")).not.toBeInTheDocument();
    expect(screen.getByText("snippet-b")).toBeInTheDocument();
  });

  it("expands a collapsed message when its header is clicked", async () => {
    const user = userEvent.setup();
    render(
      <ConversationThread threadEmails={[C, A, B]} selectedEmailId="a" selectedDetail={selectedDetail} />,
    );
    const bobToggle = screen.getByRole("button", { name: /^Bob,/ });
    await user.click(bobToggle);
    await waitFor(() => expect(screen.getByTestId("body-b")).toBeInTheDocument());
  });
});
