import { useEffect } from "react";
import { applyTheme, watchAutoTheme } from "@theme/apply_theme.js";

export type Theme = 'dark' | 'light' | 'system';

export function useTheme(theme: Theme): void {
  useEffect(() => {
    applyTheme(theme === "system" ? "auto" : theme);
    if (theme !== "system") return;
    return watchAutoTheme(applyTheme);
  }, [theme]);
}
