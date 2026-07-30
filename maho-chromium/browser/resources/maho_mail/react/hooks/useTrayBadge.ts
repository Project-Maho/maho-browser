import { useEffect } from "react";

export function useTrayBadge(unreadCount: number) {
  useEffect(() => {
    if (unreadCount > 0) {
      document.title = `(${unreadCount}) Maho Mail`;
    } else {
      document.title = "Maho Mail";
    }

    return () => {
      document.title = "Maho Mail";
    };
  }, [unreadCount]);
}
