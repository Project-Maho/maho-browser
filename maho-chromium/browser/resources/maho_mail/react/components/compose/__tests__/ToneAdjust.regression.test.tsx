import { act, fireEvent, render, screen } from "@testing-library/react";
import { expect, it, vi } from "vitest";
import { ToneAdjustButton } from "../ToneAdjustButton";
import { TestProviders } from "../../../test/mocks";
import * as api from "../../../api";
vi.mock("../../../api", () => ({ adjustTone: vi.fn() }));
it("C12 does not overwrite typing during rewrite", async () => {
 let resolve!: (v: { adjusted_text: string }) => void;
 vi.mocked(api.adjustTone).mockImplementation(() => new Promise(r => { resolve = r; }));
 const setBody = vi.fn();
 const props = { body: "old", setBody, plainTextToHtml: (s: string) => s };
 const { rerender } = render(<ToneAdjustButton {...props} />, { wrapper: TestProviders });
 fireEvent.click(screen.getByRole("button", { name: "Rewrite message" }));
 fireEvent.click(screen.getByRole("button", { name: "professional" }));
 rerender(<ToneAdjustButton {...props} body="LATEST-EDIT" />);
 await act(async () => resolve({ adjusted_text: "rewritten" }));
 expect(setBody).not.toHaveBeenCalled();
});
