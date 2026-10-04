import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { ContactAutocomplete } from "../ContactAutocomplete";
import type { Contact, ContactGroup } from "../../../types";

vi.mock("../../../api", () =>
  ({
    searchContacts: vi.fn(),
    searchContactGroups: vi.fn(),
  })
);

import { searchContacts, searchContactGroups } from "../../../api";

const mockContacts: Contact[] = [
  {
    id: "c1",
    email: "alice@test.com",
    name: "Alice",
    account_id: "acc-1",
    frequency: 5,
    last_contacted_at: null,
    is_vip: false,
    created_at: "",
    updated_at: "",
  },
];

const mockGroups: ContactGroup[] = [
  {
    id: "g1",
    name: "Team",
    member_emails: ["a@t.com", "b@t.com"],
    account_id: "acc-1",
    created_at: "",
    updated_at: "",
  },
];

function ContactAutocompleteWrapper(props: { initialValue?: string } = {}) {
  const [value, setValue] = React.useState(props.initialValue || "");
  return (
    <ContactAutocomplete
      accountId="acc-1"
      value={value}
      onChange={setValue}
      label="To"
    />
  );
}

import React from "react";

describe("ContactAutocomplete", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("renders input with placeholder", () => {
    render(<ContactAutocompleteWrapper />);

    const input = screen.getByRole("combobox");
    expect(input).toBeInTheDocument();
    expect(input).toHaveAttribute("placeholder", "recipient@example.com");
  });

  it("typing less than 2 chars does not show suggestions", async () => {
    vi.mocked(searchContacts).mockResolvedValue([]);
    vi.mocked(searchContactGroups).mockResolvedValue([]);

    render(<ContactAutocompleteWrapper />);

    const input = screen.getByRole("combobox");
    fireEvent.change(input, { target: { value: "a" } });

    await new Promise((r) => setTimeout(r, 300));

    expect(searchContacts).not.toHaveBeenCalled();
    expect(searchContactGroups).not.toHaveBeenCalled();

    const listbox = screen.queryByRole("listbox");
    expect(listbox).not.toBeInTheDocument();
  });

  it("typing 2+ chars calls searchContacts and searchContactGroups after debounce", async () => {
    vi.mocked(searchContacts).mockResolvedValue(mockContacts);
    vi.mocked(searchContactGroups).mockResolvedValue(mockGroups);

    render(<ContactAutocompleteWrapper />);

    const input = screen.getByRole("combobox");
    fireEvent.change(input, { target: { value: "al" } });

    await waitFor(() => {
      expect(searchContacts).toHaveBeenCalledWith("acc-1", "al", 5);
      expect(searchContactGroups).toHaveBeenCalledWith("acc-1", "al");
    });
  });

  it("shows contact suggestions with name and email", async () => {
    vi.mocked(searchContacts).mockResolvedValue(mockContacts);
    vi.mocked(searchContactGroups).mockResolvedValue([]);

    render(<ContactAutocompleteWrapper />);

    const input = screen.getByRole("combobox");
    fireEvent.change(input, { target: { value: "al" } });

    await waitFor(() => {
      expect(screen.getByRole("listbox")).toBeInTheDocument();
    });

    const options = screen.getAllByRole("option");
    expect(options.length).toBeGreaterThan(0);
    expect(screen.getByText("Alice")).toBeInTheDocument();
    expect(screen.getByText("alice@test.com")).toBeInTheDocument();
  });

  it("shows group suggestions with name and member count", async () => {
    vi.mocked(searchContacts).mockResolvedValue([]);
    vi.mocked(searchContactGroups).mockResolvedValue(mockGroups);

    render(<ContactAutocompleteWrapper />);

    const input = screen.getByRole("combobox");
    fireEvent.change(input, { target: { value: "te" } });

    await waitFor(() => {
      expect(screen.getByRole("listbox")).toBeInTheDocument();
    });

    expect(screen.getByText("Team")).toBeInTheDocument();
    expect(screen.getByText("2 members")).toBeInTheDocument();
  });

  it("selecting a contact appends Name <email> to value", async () => {
    vi.mocked(searchContacts).mockResolvedValue(mockContacts);
    vi.mocked(searchContactGroups).mockResolvedValue([]);

    render(<ContactAutocompleteWrapper />);

    const input = screen.getByRole("combobox");
    fireEvent.change(input, { target: { value: "al" } });

    await waitFor(() => {
      expect(screen.getByRole("listbox")).toBeInTheDocument();
    });

    const option = screen.getByRole("option");
    fireEvent.click(option);

    await waitFor(() => {
      expect(screen.getByText("Alice <alice@test.com>")).toBeInTheDocument();
    });
  });

  it("selecting a group expands member_emails into value", async () => {
    vi.mocked(searchContacts).mockResolvedValue([]);
    vi.mocked(searchContactGroups).mockResolvedValue(mockGroups);

    render(<ContactAutocompleteWrapper />);

    const input = screen.getByRole("combobox");
    fireEvent.change(input, { target: { value: "te" } });

    await waitFor(() => {
      expect(screen.getByRole("listbox")).toBeInTheDocument();
    });

    const option = screen.getByRole("option");
    fireEvent.click(option);

    await waitFor(() => {
      expect(screen.getByText("a@t.com")).toBeInTheDocument();
      expect(screen.getByText("b@t.com")).toBeInTheDocument();
    });
  });

  it("keyboard ArrowDown/ArrowUp updates active suggestion", async () => {
    vi.mocked(searchContacts).mockResolvedValue(mockContacts);
    vi.mocked(searchContactGroups).mockResolvedValue(mockGroups);

    render(<ContactAutocompleteWrapper />);

    const input = screen.getByRole("combobox");
    fireEvent.change(input, { target: { value: "ali" } });

    await waitFor(() => {
      expect(screen.getByRole("listbox")).toBeInTheDocument();
    });

    const options = screen.getAllByRole("option");
    expect(options).toHaveLength(2);

    expect(options[0]).not.toHaveAttribute("aria-selected", "true");
    expect(options[1]).not.toHaveAttribute("aria-selected", "true");

    fireEvent.keyDown(input, { key: "ArrowDown" });
    expect(options[0]).toHaveAttribute("aria-selected", "true");

    fireEvent.keyDown(input, { key: "ArrowDown" });
    expect(options[1]).toHaveAttribute("aria-selected", "true");
    expect(options[0]).toHaveAttribute("aria-selected", "false");

    fireEvent.keyDown(input, { key: "ArrowUp" });
    expect(options[0]).toHaveAttribute("aria-selected", "true");
    expect(options[1]).toHaveAttribute("aria-selected", "false");
  });

  it("Enter selects active suggestion", async () => {
    vi.mocked(searchContacts).mockResolvedValue(mockContacts);
    vi.mocked(searchContactGroups).mockResolvedValue([]);

    render(<ContactAutocompleteWrapper />);

    const input = screen.getByRole("combobox");
    fireEvent.change(input, { target: { value: "al" } });

    await waitFor(() => {
      expect(screen.getByRole("listbox")).toBeInTheDocument();
    });

    fireEvent.keyDown(input, { key: "ArrowDown" });
    fireEvent.keyDown(input, { key: "Enter" });

    await waitFor(() => {
      expect(screen.getByText("Alice <alice@test.com>")).toBeInTheDocument();
    });
  });

  it("Escape closes suggestions", async () => {
    vi.mocked(searchContacts).mockResolvedValue(mockContacts);
    vi.mocked(searchContactGroups).mockResolvedValue([]);

    render(<ContactAutocompleteWrapper />);

    const input = screen.getByRole("combobox");
    fireEvent.change(input, { target: { value: "al" } });

    await waitFor(() => {
      expect(screen.getByRole("listbox")).toBeInTheDocument();
    });

    fireEvent.keyDown(input, { key: "Escape" });

    await waitFor(() => {
      expect(screen.queryByRole("listbox")).not.toBeInTheDocument();
    });
  });

  it("shows error message for invalid email validation", async () => {
    render(<ContactAutocompleteWrapper initialValue="invalid-email, valid@test.com" />);

    const input = screen.getByRole("combobox");
    fireEvent.blur(input);

    await waitFor(() => {
      expect(screen.getByText(/Invalid:/)).toBeInTheDocument();
    });
  });
});
