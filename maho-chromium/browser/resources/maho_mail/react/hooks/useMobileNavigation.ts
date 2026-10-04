import { useCallback, useEffect, useMemo, useRef, useState } from "react";

export type MobileScreen = "list" | "detail" | "compose" | "settings" | "search";

export interface MobileNavState {
  stack: MobileScreen[];
  currentScreen: MobileScreen;
  push: (screen: MobileScreen) => void;
  pop: () => boolean;
  reset: (screen?: MobileScreen) => void;
  canGoBack: boolean;
}

const ROOT_SCREEN: MobileScreen = "list";

function getHistoryState(depth: number) {
  if (typeof window === "undefined") {
    return undefined;
  }

  return {
    ...(window.history.state ?? {}),
    __mahoMobileNavigation: true,
    __mahoMobileNavigationDepth: depth,
  };
}

export function useMobileNavigation(initialScreen: MobileScreen = ROOT_SCREEN): MobileNavState {
  const [stack, setStack] = useState<MobileScreen[]>([initialScreen]);
  const stackRef = useRef<MobileScreen[]>([initialScreen]);

  useEffect(() => {
    stackRef.current = stack;
  }, [stack]);

  useEffect(() => {
    if (typeof window === "undefined") {
      return undefined;
    }

    window.history.replaceState(getHistoryState(0), "");

    const handlePopState = () => {
      if (stackRef.current.length <= 1) {
        return;
      }

      setStack((currentStack) => {
        if (currentStack.length <= 1) {
          return currentStack;
        }

        const nextStack = currentStack.slice(0, -1);
        stackRef.current = nextStack;
        return nextStack;
      });
    };

    window.addEventListener("popstate", handlePopState);

    return () => {
      window.removeEventListener("popstate", handlePopState);
    };
  }, []);

  const push = useCallback((screen: MobileScreen) => {
    const nextStack = [...stackRef.current, screen];
    stackRef.current = nextStack;
    setStack(nextStack);

    if (typeof window !== "undefined") {
      window.history.pushState(getHistoryState(nextStack.length - 1), "");
    }
  }, []);

  const pop = useCallback(() => {
    if (stackRef.current.length <= 1) {
      return false;
    }

    if (typeof window !== "undefined") {
      window.history.back();
    } else {
      const nextStack = stackRef.current.slice(0, -1);
      stackRef.current = nextStack;
      setStack(nextStack);
    }

    return true;
  }, []);

  const reset = useCallback((screen: MobileScreen = ROOT_SCREEN) => {
    const nextStack: MobileScreen[] = [screen];
    stackRef.current = nextStack;
    setStack(nextStack);

    if (typeof window !== "undefined") {
      window.history.replaceState(getHistoryState(0), "");
    }
  }, []);

  const currentScreen = useMemo(() => stack[stack.length - 1] ?? ROOT_SCREEN, [stack]);
  const canGoBack = stack.length > 1;

  return {
    stack,
    currentScreen,
    push,
    pop,
    reset,
    canGoBack,
  };
}
