import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { ToneAdjustButton } from "../ToneAdjustButton";
import * as api from "../../../api";

// Mock the API module
vi.mock("../../../api", () => ({
  adjustTone: vi.fn(),
}));

// Mock useToast from components/ui/Toast
const mockToast = vi.fn();
vi.mock("../../ui/Toast", () => ({
  useToast: () => ({
    toast: mockToast,
  }),
  Toaster: () => null,
}));

describe("ToneAdjustButton", () => {
  const initialBody = "This is a test email body.";
  const setBody = vi.fn();
  const setBodyHtml = vi.fn();
  const plainTextToHtml = (t: string) => `<p>${t}</p>`;

  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("renders adjust tone button, triggers api, and allows undo", async () => {
    vi.mocked(api.adjustTone).mockResolvedValue({
      adjusted_text: "This is a professional test email body.",
    });

    const { rerender } = render(
      <ToneAdjustButton
        body={initialBody}
        setBody={setBody}
        setBodyHtml={setBodyHtml}
        plainTextToHtml={plainTextToHtml}
      />
    );

    const button = screen.getByLabelText("Rewrite message");
    expect(button).toBeInTheDocument();

    // Click to open dropdown
    fireEvent.click(button);

    // Select professional tone
    const professionalBtn = screen.getByText("professional");
    fireEvent.click(professionalBtn);

    await waitFor(() => {
      expect(api.adjustTone).toHaveBeenCalledWith({
        text: initialBody,
        tone: "professional",
      });
      expect(setBody).toHaveBeenCalledWith("This is a professional test email body.");
      expect(setBodyHtml).toHaveBeenCalledWith("<p>This is a professional test email body.</p>");
    });

    // Rerender component to simulate updated body and show undo button
    rerender(
      <ToneAdjustButton
        body="This is a professional test email body."
        setBody={setBody}
        setBodyHtml={setBodyHtml}
        plainTextToHtml={plainTextToHtml}
      />
    );

    // The undo button should not render yet unless state is preserved.
    // Wait, since we are testing state inside the hook/component, rerendering with the updated props
    // might not preserve the internal `previousBody` state because it is a different render call but the same React instance.
    // In our component, `previousBody` is set to `body` before adjustment, so `previousBody` is "This is a test email body.".
    // Let's assert on the undo button directly in the same test sequence (without rerendering if we don't need to, but wait, rerendering with new props is fine).
    // Let's check if the undo button exists:
    const undoButton = screen.getByLabelText("Undo Tone");
    expect(undoButton).toBeInTheDocument();

    // Click undo
    fireEvent.click(undoButton);
    expect(setBody).toHaveBeenCalledWith(initialBody);
    expect(setBodyHtml).toHaveBeenCalledWith("<p>This is a test email body.</p>");
  });
});
