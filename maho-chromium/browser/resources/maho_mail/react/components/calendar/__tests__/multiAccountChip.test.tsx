import { describe, it, expect } from "vitest";
import type { GoogleCalendarEntry } from "../../../types";

describe("Filter chip account isolation", () => {
  it("two accounts with same google calendar_id string keep distinct DB primary keys", () => {
    const accountA_id = "acc-A";
    const accountB_id = "acc-B";
    const shared_calendar_id = "primary";

    const chips: GoogleCalendarEntry[] = [
      {
        id: "uuid-a-primary",
        account_id: accountA_id,
        calendar_id: shared_calendar_id,
        summary: "A primary",
        background_color: "#ff0000",
        foreground_color: null,
        is_primary: true,
        access_role: "owner",
        visible: true,
      },
      {
        id: "uuid-b-primary",
        account_id: accountB_id,
        calendar_id: shared_calendar_id,
        summary: "B primary",
        background_color: "#00ff00",
        foreground_color: null,
        is_primary: true,
        access_role: "owner",
        visible: true,
      },
    ];

    expect(chips[0].id).not.toBe(chips[1].id);
    expect(chips[0].calendar_id).toBe(chips[1].calendar_id);
    expect(chips[0].account_id).not.toBe(chips[1].account_id);
  });

  it("toggling one chip does not accidentally match the other", () => {
    const chipA = { id: "uuid-a", account_id: "acc-A", calendar_id: "primary", visible: true };
    const chipB = { id: "uuid-b", account_id: "acc-B", calendar_id: "primary", visible: true };
    const chips = [chipA, chipB];

    const toggledChips = chips.map((c) => (c.id === "uuid-a" ? { ...c, visible: false } : c));

    expect(toggledChips[0].visible).toBe(false);
    expect(toggledChips[1].visible).toBe(true);
  });

  it("account grouping partitions chips correctly by account_id", () => {
    const chips: GoogleCalendarEntry[] = [
      { id: "c1", account_id: "acc-A", calendar_id: "primary", summary: "A", background_color: null, foreground_color: null, is_primary: true, access_role: "owner", visible: true },
      { id: "c2", account_id: "acc-A", calendar_id: "holidays", summary: "A2", background_color: null, foreground_color: null, is_primary: false, access_role: "reader", visible: true },
      { id: "c3", account_id: "acc-B", calendar_id: "primary", summary: "B", background_color: null, foreground_color: null, is_primary: true, access_role: "owner", visible: true },
    ];

    const groups = chips.reduce<Record<string, GoogleCalendarEntry[]>>((acc, c) => {
      if (!acc[c.account_id]) acc[c.account_id] = [];
      acc[c.account_id].push(c);
      return acc;
    }, {});

    expect(Object.keys(groups)).toHaveLength(2);
    expect(groups["acc-A"]).toHaveLength(2);
    expect(groups["acc-B"]).toHaveLength(1);
  });
});
