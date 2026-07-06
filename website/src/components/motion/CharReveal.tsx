import {
  useEffect,
  useLayoutEffect,
  useMemo,
  useState,
  type CSSProperties,
  type JSX,
} from "react";

const useIsomorphicLayoutEffect =
  typeof window === "undefined" ? useEffect : useLayoutEffect;

const visibleCharStyles: CSSProperties = {
  opacity: 1,
  filter: "none",
  transform: "none",
};

function splitGraphemes(text: string) {
  const segmenter = new Intl.Segmenter(undefined, { granularity: "grapheme" });

  return Array.from(segmenter.segment(text), ({ index, segment }) => ({ index, segment }));
}

function renderChar(char: string) {
  return char === " " ? "\u00A0" : char;
}

export interface CharRevealProps {
  text: string;
  staggerMs?: number;
  className?: string;
  as?: keyof JSX.IntrinsicElements;
  active?: boolean;
}

export function CharReveal({
  text,
  staggerMs = 40,
  className,
  as = "span",
  active = true,
}: CharRevealProps) {
  const Tag = as;
  const chars = useMemo(() => splitGraphemes(text), [text]);
  const [shouldAnimate, setShouldAnimate] = useState(false);

  useIsomorphicLayoutEffect(() => {
    if (typeof window === "undefined") {
      return;
    }

    setShouldAnimate(!window.matchMedia("(prefers-reduced-motion: reduce)").matches);
  }, []);

  const rootClassName = [className, shouldAnimate && active ? "char-reveal-active" : undefined]
    .filter(Boolean)
    .join(" ") || undefined;

  return (
    <Tag aria-label={text} className={rootClassName}>
      {chars.map(({ index: charIndex, segment }, index) => {
        const style = shouldAnimate
          ? ({ animationDelay: `${index * staggerMs}ms` } satisfies CSSProperties)
          : visibleCharStyles;

        return (
          <span
            key={charIndex}
            aria-hidden="true"
            className={shouldAnimate ? "char-reveal-item" : undefined}
            style={style}
          >
            {renderChar(segment)}
          </span>
        );
      })}
    </Tag>
  );
}

export default CharReveal;
