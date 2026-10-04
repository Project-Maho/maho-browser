import { useState } from "react";
import { act, fireEvent, render, renderHook, screen } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vitest";
import { ContactAutocomplete } from "../ContactAutocomplete";
import { useComposeRecipients } from "../../../hooks/useComposeRecipients";
vi.mock("../../../api", () => ({
  searchContacts: vi.fn().mockResolvedValue([{ id: "j", name: "Doe, Jane", email: "jane@example.com" }]),
  searchContactGroups: vi.fn().mockResolvedValue([]),
}));
afterEach(() => vi.useRealTimers());
function Harness() {
  const [value, onChange] = useState("");
  return <ContactAutocomplete accountId="a" value={value} onChange={onChange} />;
}
it("C08 preserves a quoted mailbox through recipient parsing", () => {
  const { result } = renderHook(() => useComposeRecipients({ isOpen: true, prevIsOpenRef: { current: false } }));
  expect(result.current.parseRecipients('"Doe, Jane" <jane@example.com>, other@example.com')).toEqual(['"Doe, Jane" <jane@example.com>', 'other@example.com']);
});
it("C08 selects a comma-named contact as one valid chip", async () => {
  vi.useFakeTimers();
  render(<Harness />);
  fireEvent.change(screen.getByRole("combobox"), { target: { value: "ja" } });
  await act(async () => { await vi.advanceTimersByTimeAsync(200); });
  fireEvent.click(screen.getByRole("option"));
  expect(screen.queryByText(/^Invalid:/)).toBeNull();
  expect(screen.getByText('"Doe, Jane" <jane@example.com>')).toBeInTheDocument();
});
it("C09 autocomplete Escape belongs to the suggestion layer", async () => {
  vi.useFakeTimers();
  const escaped = vi.fn();
  render(<div onKeyDown={escaped}><Harness /></div>);
  fireEvent.change(screen.getByRole("combobox"), { target: { value: "ja" } });
  await act(async () => { await vi.advanceTimersByTimeAsync(200); });
  fireEvent.keyDown(screen.getByRole("combobox"), { key: "Escape" });
  expect(screen.queryByRole("listbox")).toBeNull();
  expect(screen.getByRole("combobox")).toHaveValue("ja");
  expect(escaped).not.toHaveBeenCalled();
});
