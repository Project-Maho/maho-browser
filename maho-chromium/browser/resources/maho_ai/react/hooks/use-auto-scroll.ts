import {useEffect, useRef} from 'react';

const AUTO_SCROLL_STICKY_PX = 96;

export function useAutoScroll<T extends HTMLElement>(dependency: unknown) {
  const ref = useRef<T | null>(null);
  const rafHandle = useRef<number | null>(null);
  const stickToBottom = useRef(true);

  useEffect(() => {
    const element = ref.current;
    if (!element) {
      return;
    }

    const handleScroll = () => {
      const distanceFromBottom =
          element.scrollHeight - element.scrollTop - element.clientHeight;
      stickToBottom.current = distanceFromBottom <= AUTO_SCROLL_STICKY_PX;
    };

    element.addEventListener('scroll', handleScroll, {passive: true});
    return () => element.removeEventListener('scroll', handleScroll);
  }, []);

  useEffect(() => {
    if (!stickToBottom.current) {
      return;
    }
    if (rafHandle.current === null) {
      rafHandle.current = requestAnimationFrame(() => {
        rafHandle.current = null;
        const current = ref.current;
        if (current) {
          current.scrollTop = current.scrollHeight;
        }
      });
    }
  }, [dependency]);

  useEffect(() => {
    return () => {
      if (rafHandle.current !== null) {
        cancelAnimationFrame(rafHandle.current);
        rafHandle.current = null;
      }
    };
  }, []);

  return ref;
}
