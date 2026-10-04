import { describe, expect, it } from "vitest";
import { resolveDragAndDrop, type DragAndDropWrapper } from "../dragAndDropImport";

const wrapper = (() => null) as unknown as DragAndDropWrapper;

describe("resolveDragAndDrop", () => {
  it("returns a direct ESM function export", () => {
    // Given: an ESM-style callable export.
    // When: the calendar wrapper is resolved.
    // Then: the callable must be returned unchanged.
    expect(resolveDragAndDrop(wrapper)).toBe(wrapper);
  });

  it("unwraps the nested CommonJS default produced by the browser bundle", () => {
    // Given: the nested export shape observed in the production ESM bundle.
    // When: the calendar wrapper is resolved.
    // Then: the nested callable must be returned.
    expect(resolveDragAndDrop({ default: wrapper })).toBe(wrapper);
  });
});
