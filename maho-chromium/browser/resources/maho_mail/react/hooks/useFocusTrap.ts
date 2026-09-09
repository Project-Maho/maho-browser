import { useEffect, useRef, type RefObject } from "react";

const FOCUSABLE_SELECTOR = [
  'a[href]',
  'button:not([disabled])',
  'input:not([disabled])',
  'select:not([disabled])',
  'textarea:not([disabled])',
  '[tabindex]:not([tabindex="-1"])',
].join(", ");

/**
 * Traps keyboard focus within a container element when active.
 * - Tab / Shift+Tab cycle through focusable elements inside the container
 * - On activation, moves focus to the first focusable element
 * - On deactivation, restores focus to the element that was focused before activation
 */
export function useFocusTrap(
  containerRef: RefObject<HTMLElement | null>,
  isActive: boolean,
) {
  const previousFocusRef = useRef<HTMLElement | null>(null);

  useEffect(() => {
    if (!isActive) return;

    previousFocusRef.current = document.activeElement as HTMLElement | null;

    const container = containerRef.current;
    if (!container) return;

    let rafHandle: number | null = null;

    const rafCallback = () => {
      if (container.contains(document.activeElement)) {
        return;
      }
      const focusable = container.querySelectorAll<HTMLElement>(FOCUSABLE_SELECTOR);
      if (focusable.length > 0) {
        const autoFocusEl = Array.from(focusable).find(
          (el) => el.hasAttribute("autofocus") || el.getAttribute("data-autofocus") === "true"
        );
        if (autoFocusEl) {
          autoFocusEl.focus();
        } else {
          focusable[0].focus();
        }
      }
    };
    rafHandle = requestAnimationFrame(rafCallback);

    function handleKeyDown(e: KeyboardEvent) {
      if (e.key !== "Tab") return;

      const el = containerRef.current;
      if (!el) return;

      const focusable = Array.from(el.querySelectorAll<HTMLElement>(FOCUSABLE_SELECTOR));
      if (focusable.length === 0) return;

      const first = focusable[0];
      const last = focusable[focusable.length - 1];

      if (e.shiftKey) {
        if (document.activeElement === first) {
          e.preventDefault();
          last.focus();
        }
      } else {
        if (document.activeElement === last) {
          e.preventDefault();
          first.focus();
        }
      }
    }

    document.addEventListener("keydown", handleKeyDown);

    return () => {
      document.removeEventListener("keydown", handleKeyDown);
      if (rafHandle !== null) {
        cancelAnimationFrame(rafHandle);
        rafHandle = null;
      }

      if (previousFocusRef.current && typeof previousFocusRef.current.focus === "function") {
        previousFocusRef.current.focus();
      }
    };
  }, [isActive, containerRef]);
}
