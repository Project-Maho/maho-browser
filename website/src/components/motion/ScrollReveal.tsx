import * as React from "react";
import type { JSX } from "react";

export interface ScrollRevealProps {
  children: React.ReactNode;
  as?: keyof JSX.IntrinsicElements;
  variant?: "fade" | "slide";
  delayMs?: number;
  thresholdRatio?: number;
  rootMarginPx?: number;
  className?: string;
}

export function ScrollReveal({
  children,
  as = "div",
  variant = "slide",
  delayMs = 0,
  thresholdRatio = 0.15,
  rootMarginPx = -40,
  className,
}: ScrollRevealProps) {
  const elementRef = React.useRef<Element | null>(null);
  const [isVisible, setIsVisible] = React.useState(false);
  const isServer = typeof window === "undefined";
  const setElementRef = React.useCallback((node: Element | null) => {
    elementRef.current = node;
  }, []);

  React.useEffect(() => {
    if (isServer) return;

    const element = elementRef.current;

    if (!element) return;

    if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
      setIsVisible(true);
      return;
    }

    const observer = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) {
          if (!entry.isIntersecting) continue;

          setIsVisible(true);
          observer.unobserve(entry.target);
        }
      },
      {
        threshold: thresholdRatio,
        rootMargin: `${rootMarginPx}px`,
      },
    );

    observer.observe(element);

    return () => {
      observer.disconnect();
    };
  }, [isServer, rootMarginPx, thresholdRatio]);

  const Tag = as;
  const baseClassName = variant === "fade" ? "motion-safe-fade" : "motion-safe-slide";
  const mergedClassName = [
    baseClassName,
    (isServer || isVisible) && "is-visible",
    className,
  ]
    .filter(Boolean)
    .join(" ");
  const style: React.CSSProperties | undefined =
    delayMs > 0 ? { transitionDelay: `${delayMs}ms` } : undefined;

  return React.createElement(Tag as React.ElementType, {
    ref: setElementRef as React.Ref<Element>,
    className: mergedClassName,
    style,
    suppressHydrationWarning: true,
  }, children);
}

export default ScrollReveal;
