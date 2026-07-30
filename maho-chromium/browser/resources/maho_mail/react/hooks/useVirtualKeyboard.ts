import { useEffect, useState } from "react";

interface VirtualKeyboardState {
  keyboardVisible: boolean;
  keyboardHeight: number;
}

function getKeyboardState(): VirtualKeyboardState {
  if (typeof window === "undefined" || typeof window.visualViewport === "undefined" || window.visualViewport == null) {
    return { keyboardVisible: false, keyboardHeight: 0 };
  }

  const viewport = window.visualViewport;
  const keyboardHeight = Math.max(0, window.innerHeight - viewport.height - viewport.offsetTop);

  return {
    keyboardVisible: keyboardHeight > 120,
    keyboardHeight,
  };
}

export function useVirtualKeyboard() {
  const [state, setState] = useState<VirtualKeyboardState>(getKeyboardState);

  useEffect(() => {
    if (typeof window === "undefined" || typeof window.visualViewport === "undefined" || window.visualViewport == null) {
      return undefined;
    }

    const viewport = window.visualViewport;
    const update = () => {
      setState(getKeyboardState());
    };

    update();
    viewport.addEventListener("resize", update);
    viewport.addEventListener("scroll", update);
    window.addEventListener("orientationchange", update);

    return () => {
      viewport.removeEventListener("resize", update);
      viewport.removeEventListener("scroll", update);
      window.removeEventListener("orientationchange", update);
    };
  }, []);

  return state;
}
