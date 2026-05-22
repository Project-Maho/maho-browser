import {useEffect, useRef} from 'react';

export function useAutoScroll<T extends HTMLElement>(dependency: unknown) {
  const ref = useRef<T | null>(null);

  useEffect(() => {
    const element = ref.current;
    if (!element) {
      return;
    }
    element.scrollTop = element.scrollHeight;
  }, [dependency]);

  return ref;
}
