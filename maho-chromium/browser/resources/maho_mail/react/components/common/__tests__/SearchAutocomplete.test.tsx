import { render, screen, fireEvent, within } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { SearchAutocomplete } from "../SearchAutocomplete";

describe("SearchAutocomplete", () => {
  it("shows recent searches and search operators for an empty query", () => {
    render(
      <SearchAutocomplete
        query=""
        visible={true}
        onSelect={vi.fn()}
        onDismiss={vi.fn()}
        recentSearches={["invoice", "project alpha"]}
        contacts={["alice@example.com"]}
        folderNames={["Archive"]}
      />,
    );

    const listbox = screen.getByRole("listbox");
    expect(within(listbox).getByText("invoice")).toBeInTheDocument();
    expect(within(listbox).getByText("project alpha")).toBeInTheDocument();
    expect(within(listbox).getByText("from:")).toBeInTheDocument();
    expect(within(listbox).getByText("subject:")).toBeInTheDocument();
  });

  it("suggests matching contacts for sender queries", () => {
    render(
      <SearchAutocomplete
        query="from:al"
        visible={true}
        onSelect={vi.fn()}
        onDismiss={vi.fn()}
        contacts={["alice@example.com", "bob@example.com"]}
      />,
    );

    expect(screen.getByRole("option", { name: "from:alice@example.com" })).toBeInTheDocument();
    expect(screen.queryByRole("option", { name: "from:bob@example.com" })).not.toBeInTheDocument();
  });

  it("selects a suggestion and dismisses the list", () => {
    const onSelect = vi.fn();
    const onDismiss = vi.fn();

    render(
      <SearchAutocomplete
        query=""
        visible={true}
        onSelect={onSelect}
        onDismiss={onDismiss}
        recentSearches={["invoice"]}
      />,
    );

    fireEvent.click(screen.getByRole("option", { name: "invoice" }));

    expect(onSelect).toHaveBeenCalledWith("invoice");
    expect(onDismiss).toHaveBeenCalledTimes(1);
  });
});
