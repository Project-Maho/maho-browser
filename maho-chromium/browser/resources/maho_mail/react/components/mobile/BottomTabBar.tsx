import { Inbox, PenLine, Search, Settings } from "lucide-react";

import { cn } from "../../lib/utils";

type MobileTab = "inbox" | "search" | "compose" | "settings";

interface BottomTabBarProps {
  activeTab: MobileTab;
  onTabChange: (tab: MobileTab) => void;
  unreadCount?: number;
}

const TAB_ITEMS = [
  { id: "inbox", label: "Inbox", icon: Inbox },
  { id: "search", label: "Search", icon: Search },
  { id: "compose", label: "Compose", icon: PenLine },
  { id: "settings", label: "Settings", icon: Settings },
] satisfies Array<{ id: MobileTab; label: string; icon: typeof Inbox }>;

export function formatBadgeCount(count: number) {
  return count > 99 ? "99+" : String(count);
}

export function resolveMailBadgePresentation(count: number) {
  const text = formatBadgeCount(count);
  return {
    text,
    accessibleName: `Mail, ${text} unread ${count === 1 ? "message" : "messages"}`,
  };
}

export function BottomTabBar({
  activeTab,
  onTabChange,
  unreadCount = 0,
}: BottomTabBarProps) {
  return (
    <nav
      aria-label="Mobile navigation"
      className="fixed inset-x-0 bottom-0 z-40 border-t border-border bg-background/95 backdrop-blur md:hidden"
      style={{
        paddingBottom: "env(safe-area-inset-bottom)",
        paddingLeft: "env(safe-area-inset-left)",
        paddingRight: "env(safe-area-inset-right)",
      }}
    >
      <div className="grid min-h-14 grid-cols-4">
        {TAB_ITEMS.map((tab) => {
          const Icon = tab.icon;
          const isActive = activeTab === tab.id;
          const showBadge = tab.id === "inbox" && unreadCount > 0;
          const badge = showBadge ? resolveMailBadgePresentation(unreadCount) : null;

          return (
            <button
              key={tab.id}
              type="button"
              aria-current={isActive ? "page" : undefined}
              aria-label={badge?.accessibleName ?? tab.label}
              className={cn(
                "mail-pressable flex min-h-14 w-full flex-col items-center justify-center gap-1 px-2 pt-2 text-[11px] leading-none",
                isActive ? "text-primary" : "text-muted-foreground hover:text-foreground",
              )}
              onClick={() => onTabChange(tab.id)}
            >
              <span className="relative flex h-5 w-5 items-center justify-center">
                <Icon className="h-5 w-5" strokeWidth={2} />
                {showBadge ? (
                  <span className="absolute -right-3 -top-1 inline-flex min-w-4 items-center justify-center rounded-full bg-destructive px-1 text-[10px] font-semibold text-destructive-foreground">
                    {badge?.text}
                  </span>
                ) : null}
              </span>
              <span>{tab.label}</span>
            </button>
          );
        })}
      </div>
    </nav>
  );
}
