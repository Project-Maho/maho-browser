import { useEffect } from "react";
import { render } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { useHotkeys } from "./useHotkeys";

function TestComponent({ onHotkey, keyBinding, shift }: { onHotkey: () => void; keyBinding: string; shift?: boolean }) {
  useHotkeys([{ key: keyBinding, shift, handler: () => onHotkey() }]);
  useEffect(() => {}, []);
  return <input aria-label="field" />;
}

describe("useHotkeys", () => {
  it("fires when key is pressed outside typing targets", () => {
    const onHotkey = vi.fn();
    render(<TestComponent onHotkey={onHotkey} keyBinding="k" />);

    document.dispatchEvent(new KeyboardEvent("keydown", { key: "k" }));

    expect(onHotkey).toHaveBeenCalledOnce();
  });

  it("fires for printable shifted characters like ?", () => {
    const onHotkey = vi.fn();
    render(<TestComponent onHotkey={onHotkey} keyBinding="?" />);

    document.dispatchEvent(new KeyboardEvent("keydown", { key: "/", code: "Slash", shiftKey: true, bubbles: true }));

    expect(onHotkey).toHaveBeenCalledOnce();
  });
});
