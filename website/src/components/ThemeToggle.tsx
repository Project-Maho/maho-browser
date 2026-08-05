import { Sun, Moon } from "lucide-react";
import { useState, useEffect, useRef } from "react";

export function ThemeToggle() {
  const userHasToggled = useRef(false);
  const [isDark, setIsDark] = useState(() => {
    if (typeof window === "undefined") return true;
    try {
      const saved = window.localStorage.getItem("maho-theme");
      return saved
        ? saved === "dark"
        : !window.matchMedia("(prefers-color-scheme: light)").matches;
    } catch {
      return !window.matchMedia("(prefers-color-scheme: light)").matches;
    }
  });

  useEffect(() => {
    if (typeof window === "undefined") return;
    document.documentElement.classList.remove("dark", "light");
    document.documentElement.classList.add(isDark ? "dark" : "light");
    if (!userHasToggled.current) return;
    try {
      window.localStorage.setItem("maho-theme", isDark ? "dark" : "light");
      window.localStorage.setItem("starlight-theme", isDark ? "dark" : "light");
    } catch {
      void 0;
    }
  }, [isDark]);

  return (
    <button
      type="button"
      onClick={() => {
        userHasToggled.current = true;
        setIsDark((current) => !current);
      }}
      className="rounded-lg p-2 text-muted-foreground hover:text-foreground transition-colors"
      aria-label={isDark ? "Switch to light mode" : "Switch to dark mode"}
    >
      {isDark ? <Sun size={20} /> : <Moon size={20} />}
    </button>
  );
}

export default ThemeToggle;
