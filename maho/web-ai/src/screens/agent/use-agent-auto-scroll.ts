import { useCallback, useEffect, useRef } from 'preact/hooks';

const PINNED_THRESHOLD_PX = 48;

export function useAgentAutoScroll(scrollRevision: number) {
  const scrollRef = useRef<HTMLDivElement | null>(null);
  const pinnedRef = useRef(true);
  const forceBottomRef = useRef(false);
  const rafRef = useRef<number | null>(null);

  const onScroll = useCallback(() => {
    const node = scrollRef.current;
    if (node === null) return;
    pinnedRef.current = distanceFromBottom(node) <= PINNED_THRESHOLD_PX;
  }, []);

  const forceBottom = useCallback(() => {
    forceBottomRef.current = true;
  }, []);

  useEffect(() => {
    const node = scrollRef.current;
    if (node === null) return;
    if (!forceBottomRef.current && !pinnedRef.current) return;
    if (rafRef.current !== null) return;

    rafRef.current = requestAnimationFrame(() => {
      rafRef.current = null;
      const current = scrollRef.current;
      if (current !== null) {
        current.scrollTop = current.scrollHeight;
        pinnedRef.current = true;
      }
      forceBottomRef.current = false;
    });

    return () => {
      if (rafRef.current !== null) {
        cancelAnimationFrame(rafRef.current);
        rafRef.current = null;
      }
    };
  }, [scrollRevision]);

  return { forceBottom, onScroll, scrollRef };
}

function distanceFromBottom(node: HTMLDivElement): number {
  return node.scrollHeight - node.scrollTop - node.clientHeight;
}
