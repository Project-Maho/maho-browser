import {useEffect, useRef, useState} from 'react';

function formatElapsed(totalSeconds: number): string {
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  return minutes > 0 ? `${minutes}m ${seconds}s` : `${seconds}s`;
}

export function PendingThought({label}: {label: string}) {
  const startedAtRef = useRef(Date.now());
  const [elapsedSeconds, setElapsedSeconds] = useState(0);

  useEffect(() => {
    const intervalId = setInterval(() => {
      setElapsedSeconds(
          Math.floor((Date.now() - startedAtRef.current) / 1000));
    }, 1000);
    return () => clearInterval(intervalId);
  }, []);

  return (
    <article className="flex w-full min-w-0 items-center gap-2 px-1 py-0.5">
      <span
        aria-hidden="true"
        className="size-[5px] shrink-0 rounded-full bg-panel-accent text-panel-accent shadow-[0_0_0_2.5px_color-mix(in_srgb,currentColor_18%,transparent)] motion-safe:animate-pulse" />
      <span className="min-w-0 truncate text-[12px] leading-5 tracking-[-0.005em] text-muted-foreground">
        {label}
      </span>
      {elapsedSeconds > 0 ? (
        <span
          className="ml-auto shrink-0 font-mono text-[10.5px] tabular-nums text-muted-foreground/60"
          data-pending-elapsed>
          {formatElapsed(elapsedSeconds)}
        </span>
      ) : null}
    </article>
  );
}
