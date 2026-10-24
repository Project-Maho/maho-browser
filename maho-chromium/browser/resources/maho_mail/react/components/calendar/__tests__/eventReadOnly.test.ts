import { describe, expect, it } from "vitest";
import { isEventReadOnly } from "../CalendarGrid";
import type { CalendarEvent, GoogleCalendarEntry } from "../../../types";

const calendar = (calendar_id: string, access_role: string | null): GoogleCalendarEntry => ({
  id: calendar_id, account_id: "acc-1", calendar_id, summary: calendar_id, background_color: null,
  foreground_color: null, is_primary: false, access_role, visible: true,
});
const event = (google_calendar_id: string | null) => ({ id: "e", google_calendar_id } as CalendarEvent);

describe("isEventReadOnly", () => {
  const calendars = [calendar("mine", "owner"), calendar("shared", "writer"), calendar("holidays", "reader")];

  it("blocks keyboard edits on reader calendars", () => {
    expect(isEventReadOnly(event("holidays"), calendars)).toBe(true);
  });

  it("allows owner and writer calendars", () => {
    expect(isEventReadOnly(event("mine"), calendars)).toBe(false);
    expect(isEventReadOnly(event("shared"), calendars)).toBe(false);
  });

  it("treats events without a known calendar as editable", () => {
    expect(isEventReadOnly(event(null), calendars)).toBe(false);
  });
});
