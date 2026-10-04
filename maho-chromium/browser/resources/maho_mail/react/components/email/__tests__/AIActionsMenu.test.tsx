import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { AIActionsMenu } from "../AIActionsMenu";

describe("AIActionsMenu", () => {
  const baseProps = {
    onSummarize: vi.fn(),
    onDraftReply: vi.fn(),
    onClassify: vi.fn(),
    onTranslate: vi.fn(),
  };

  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("renders a single trigger button labeled 'AI'", () => {
    render(<AIActionsMenu {...baseProps} />);
    const trigger = screen.getByRole("button", { name: /AI/i });
    expect(trigger).toBeInTheDocument();
  });

  it("opens a menu listing Summarize / Draft Reply / Classify / Translate when trigger is clicked", async () => {
    render(<AIActionsMenu {...baseProps} />);
    fireEvent.click(screen.getByRole("button", { name: /AI/i }));

    await waitFor(() => {
      expect(screen.getByRole("menuitem", { name: /Summarize/i })).toBeInTheDocument();
    });
    expect(screen.getByRole("menuitem", { name: /Draft Reply/i })).toBeInTheDocument();
    expect(screen.getByRole("menuitem", { name: /Classify/i })).toBeInTheDocument();
    expect(screen.getByRole("menuitem", { name: /Translate/i })).toBeInTheDocument();
  });

  it("invokes onSummarize when Summarize menu item is clicked", async () => {
    render(<AIActionsMenu {...baseProps} />);
    fireEvent.click(screen.getByRole("button", { name: /AI/i }));

    const item = await screen.findByRole("menuitem", { name: /Summarize/i });
    fireEvent.click(item);

    expect(baseProps.onSummarize).toHaveBeenCalledTimes(1);
  });

  it("invokes onDraftReply when Draft Reply menu item is clicked", async () => {
    render(<AIActionsMenu {...baseProps} />);
    fireEvent.click(screen.getByRole("button", { name: /AI/i }));

    const item = await screen.findByRole("menuitem", { name: /Draft Reply/i });
    fireEvent.click(item);

    expect(baseProps.onDraftReply).toHaveBeenCalledTimes(1);
  });

  it("invokes onClassify when Classify menu item is clicked", async () => {
    render(<AIActionsMenu {...baseProps} />);
    fireEvent.click(screen.getByRole("button", { name: /AI/i }));

    const item = await screen.findByRole("menuitem", { name: /Classify/i });
    fireEvent.click(item);

    expect(baseProps.onClassify).toHaveBeenCalledTimes(1);
  });

  it("invokes onTranslate when Translate menu item is clicked", async () => {
    render(<AIActionsMenu {...baseProps} />);
    fireEvent.click(screen.getByRole("button", { name: /AI/i }));

    const item = await screen.findByRole("menuitem", { name: /Translate/i });
    fireEvent.click(item);

    expect(baseProps.onTranslate).toHaveBeenCalledTimes(1);
  });

  it("disables Summarize and shows loading affordance while summaryLoading", async () => {
    render(<AIActionsMenu {...baseProps} summaryLoading />);
    fireEvent.click(screen.getByRole("button", { name: /AI/i }));

    const item = await screen.findByRole("menuitem", { name: /Summarize/i });
    expect(item).toHaveAttribute("aria-disabled", "true");
  });

  it("disables Translate while translating or model loading", async () => {
    const { rerender } = render(<AIActionsMenu {...baseProps} isTranslating />);
    fireEvent.click(screen.getByRole("button", { name: /AI/i }));
    let item = await screen.findByRole("menuitem", { name: /Translate/i });
    expect(item).toHaveAttribute("aria-disabled", "true");

    rerender(<AIActionsMenu {...baseProps} isTranslating={false} isModelLoading />);
    item = await screen.findByRole("menuitem", { name: /Loading Model|Translate/i });
    expect(item).toHaveAttribute("aria-disabled", "true");
  });

  it("renders an aiError summary inline within the menu when provided", async () => {
    render(<AIActionsMenu {...baseProps} aiError="OpenAI rate limit" />);
    fireEvent.click(screen.getByRole("button", { name: /AI/i }));
    await waitFor(() => {
      expect(screen.getByText(/OpenAI rate limit/)).toBeInTheDocument();
    });
  });
});
