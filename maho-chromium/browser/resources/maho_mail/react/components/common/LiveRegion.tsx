import { useEffect, useRef, useState } from "react";

interface LiveRegionProps {
  message: string;
  politeness?: "polite" | "assertive";
}

export function LiveRegion({ message, politeness = "polite" }: LiveRegionProps) {
  const [announced, setAnnounced] = useState("");
  const timeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    if (!message) {
      setAnnounced("");
      return;
    }

    setAnnounced("");

    timeoutRef.current = setTimeout(() => {
      setAnnounced(message);
    }, 100);

    return () => {
      if (timeoutRef.current) clearTimeout(timeoutRef.current);
    };
  }, [message]);

  return (
    <div
      role="status"
      aria-live={politeness}
      aria-atomic="true"
      className="sr-only"
    >
      {announced}
    </div>
  );
}
