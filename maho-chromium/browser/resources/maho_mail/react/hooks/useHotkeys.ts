import { useCallback, useEffect, useRef } from "react";

type HotkeyHandler = (e: KeyboardEvent) => void;

interface HotkeyConfig {
  key: string;
  handler: HotkeyHandler;
  ctrl?: boolean;
  shift?: boolean;
  meta?: boolean;
  alt?: boolean;
  disabled?: boolean;
}

const SHIFTED_PRINTABLE_KEYS_BY_CODE: Record<string, string> = {
  Backquote: "~",
  Digit1: "!",
  Digit2: "@",
  Digit3: "#",
  Digit4: "$",
  Digit5: "%",
  Digit6: "^",
  Digit7: "&",
  Digit8: "*",
  Digit9: "(",
  Digit0: ")",
  Minus: "_",
  Equal: "+",
  BracketLeft: "{",
  BracketRight: "}",
  Backslash: "|",
  Semicolon: ":",
  Quote: '"',
  Comma: "<",
  Period: ">",
  Slash: "?",
};

export function useHotkeys(hotkeys: HotkeyConfig[]) {
  const hotkeysRef = useRef(hotkeys);
  hotkeysRef.current = hotkeys;

  const isTypingTarget = useCallback((target: EventTarget | null) => {
    const node = target as HTMLElement | null;
    if (!node) return false;
    return (
      node.tagName === "INPUT" ||
      node.tagName === "TEXTAREA" ||
      node.tagName === "SELECT" ||
      node.isContentEditable
    );
  }, []);

  useEffect(() => {
    function handleKeyDown(e: KeyboardEvent) {
      const typing = isTypingTarget(e.target);
      const allowGlobal = e.key === "Escape" || (e.key === "Enter" && (e.metaKey || e.ctrlKey));

      if (typing && !allowGlobal) return;

      for (const hotkey of hotkeysRef.current) {
        if (hotkey.disabled) continue;

        const shiftedPrintableKey = e.shiftKey ? SHIFTED_PRINTABLE_KEYS_BY_CODE[e.code] : undefined;

        const keyMatch =
          e.key.toLowerCase() === hotkey.key.toLowerCase() ||
          e.code === hotkey.key ||
          shiftedPrintableKey?.toLowerCase() === hotkey.key.toLowerCase();
        const ctrlMatch = hotkey.ctrl ? e.ctrlKey : !e.ctrlKey;
        const metaMatch = hotkey.meta ? e.metaKey : !e.metaKey;
        const shiftMatch = hotkey.shift ? e.shiftKey : !e.shiftKey || shiftedPrintableKey?.toLowerCase() === hotkey.key.toLowerCase();
        const altMatch = hotkey.alt ? e.altKey : !e.altKey;

        if (keyMatch && ctrlMatch && metaMatch && shiftMatch && altMatch) {
          e.preventDefault();
          hotkey.handler(e);
          return;
        }
      }
    }

    document.addEventListener("keydown", handleKeyDown);
    return () => document.removeEventListener("keydown", handleKeyDown);
  }, [isTypingTarget]);
}
