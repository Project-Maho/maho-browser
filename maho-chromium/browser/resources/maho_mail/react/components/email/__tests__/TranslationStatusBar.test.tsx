import { describe, it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/react";
import { TranslationStatusBar } from "../TranslationStatusBar";

describe("TranslationStatusBar", () => {
  const baseProps = {
    fromLang: "ja" as const,
    toLang: "en" as const,
    onClose: vi.fn(),
  };

  it("renders compact chip showing the language pair", () => {
    render(<TranslationStatusBar {...baseProps} />);
    expect(screen.getByText(/ja.*en|JA.*EN/i)).toBeInTheDocument();
  });

  it("does NOT render a Show original / Show translation button (moved to ViewSettingsMenu)", () => {
    render(<TranslationStatusBar {...baseProps} />);
    expect(screen.queryByRole("button", { name: /show original/i })).toBeNull();
    expect(screen.queryByRole("button", { name: /show translation/i })).toBeNull();
  });

  it("offers a close button that invokes onClose", () => {
    render(<TranslationStatusBar {...baseProps} />);
    const close = screen.getByRole("button", { name: /close translation/i });
    fireEvent.click(close);
    expect(baseProps.onClose).toHaveBeenCalledTimes(1);
  });

  it("does NOT render the legacy full-width 'Translation' label band", () => {
    const { container } = render(<TranslationStatusBar {...baseProps} />);
    const matches = container.querySelectorAll(
      ".border-b.border-border.px-6.py-3",
    );
    expect(matches.length).toBe(0);
  });

  it("renders an actionable amber line when error is the 'needs AI config' shape", () => {
    const onOpenAiSettings = vi.fn();
    render(
      <TranslationStatusBar
        {...baseProps}
        error="No AI provider configured"
        onOpenAiSettings={onOpenAiSettings}
      />,
    );
    expect(screen.getByText(/AI provider/i)).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: /open ai settings/i }));
    expect(onOpenAiSettings).toHaveBeenCalledTimes(1);
  });

  it("does NOT render generic errors inline (consumer must toast instead)", () => {
    render(<TranslationStatusBar {...baseProps} error="Network timeout" />);
    expect(screen.queryByText(/network timeout/i)).toBeNull();
  });

  it("still shows the language-pair pill even when a non-actionable error is set", () => {
    render(<TranslationStatusBar {...baseProps} error="Network timeout" />);
    expect(screen.getByText(/ja.*en|JA.*EN/i)).toBeInTheDocument();
  });

  it("lets the user pick the target language when languages are provided", () => {
    const onChangeToLang = vi.fn();
    render(
      <TranslationStatusBar
        {...baseProps}
        languages={[
          { code: "en", name: "English" },
          { code: "ko", name: "Korean" },
        ]}
        onChangeToLang={onChangeToLang}
      />,
    );
    const picker = screen.getByRole("combobox", { name: /translate into/i });
    expect((picker as HTMLSelectElement).value).toBe("en");
    fireEvent.change(picker, { target: { value: "ko" } });
    expect(onChangeToLang).toHaveBeenCalledWith("ko");
  });
});
