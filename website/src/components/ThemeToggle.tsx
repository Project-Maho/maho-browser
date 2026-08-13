import { Sun, Moon } from "lucide-react";
import { useEffect, useRef, useState } from "react";

export function ThemeToggle() {
  const userHasToggled = useRef(false);
  const [isDark, setIsDark] = useState(true);
  const [isHydrated, setIsHydrated] = useState(false);

  useEffect(() => {
    const syncFromDocument = () => {
      const root = document.documentElement;
      const nextIsDark = root.classList.contains("dark")
        ? true
        : root.classList.contains("light")
          ? false
          : !window.matchMedia("(prefers-color-scheme: light)").matches;
      setIsDark(nextIsDark);
      setIsHydrated(true);
    };

    syncFromDocument();
    window.addEventListener("maho-theme-change", syncFromDocument);
    return () => window.removeEventListener("maho-theme-change", syncFromDocument);
  }, []);

  const toggleTheme = () => {
    userHasToggled.current = true;
    const nextIsDark = !document.documentElement.classList.contains("dark");
    document.documentElement.classList.remove("dark", "light");
    document.documentElement.classList.add(nextIsDark ? "dark" : "light");
    if (userHasToggled.current) {
      try {
        const theme = nextIsDark ? "dark" : "light";
        window.localStorage.setItem("maho-theme", theme);
        window.localStorage.setItem("starlight-theme", theme);
      } catch {
        void 0;
      }
    }
    window.dispatchEvent(new Event("maho-theme-change"));
  };

  return (
    <button
      type="button"
      onClick={toggleTheme}
      className="inline-flex h-11 w-11 items-center justify-center rounded-lg text-muted-foreground transition-colors hover:text-foreground"
      aria-label={isDark ? "Switch to light mode" : "Switch to dark mode"}
      data-theme-toggle
      data-hydrated={isHydrated}
    >
      {isDark ? <Sun size={20} /> : <Moon size={20} />}
    </button>
  );
}

export default ThemeToggle;
