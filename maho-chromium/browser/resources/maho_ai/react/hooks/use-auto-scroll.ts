import {useEffect, useRef} from 'react';

const AUTO_SCROLL_STICKY_PX = 96;

export function useAutoScroll<T extends HTMLElement>(dependency: unknown) {
  const ref = useRef<T | null>(null);
  const rafHandle = useRef<number | null>(null);

  useEffect(() => {
    const element = ref.current;
    if (!element) {
      return;
    }
    const distanceFromBottom =
        element.scrollHeight - element.scrollTop - element.clientHeight;
    if (distanceFromBottom > AUTO_SCROLL_STICKY_PX) {
      return;
    }
    if (rafHandle.current !== null) {
      return;
    }
    rafHandle.current = requestAnimationFrame(() => {
      rafHandle.current = null;
      const current = ref.current;
      if (current) {
        current.scrollTop = current.scrollHeight;
      }
    });
    return () => {
      if (rafHandle.current !== null) {
        cancelAnimationFrame(rafHandle.current);
        rafHandle.current = null;
      }
    };
  }, [dependency]);

  return ref;
}
