/**
 * Parses a calendar date value. Date-only strings (YYYY-MM-DD, used for
 * all-day events) are parsed as local midnight; `new Date("YYYY-MM-DD")`
 * would read them as UTC midnight and show the previous day west of UTC.
 */
export function parseAllDayDate(value: string): Date {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(value);
  if (match) {
    return new Date(Number(match[1]), Number(match[2]) - 1, Number(match[3]));
  }
  return new Date(value);
}
