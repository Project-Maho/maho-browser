import type { ReactNode } from "react";
import { Mail } from "lucide-react";

interface EmptyStateProps {
  title: string;
  description: string;
  icon?: ReactNode;
  animated?: boolean;
  action?: {
    label: string;
    onClick: () => void;
  };
}

export function EmptyState({ title, description, icon, animated = true, action }: EmptyStateProps) {
  return (
    <div className={`flex flex-col items-center gap-3 text-center ${animated ? "animate-fade-in" : ""}`}>
      <div className={`flex h-16 w-16 items-center justify-center rounded-2xl bg-background ${animated ? "animate-bounce-gentle" : ""}`}>
        {icon ?? <Mail size={28} className="text-muted-foreground" />}
      </div>
      <h3 className="text-lg font-medium text-muted-foreground">{title}</h3>
      <p className="max-w-xs text-sm text-muted-foreground">{description}</p>
      {action && (
        <button
          type="button"
          onClick={action.onClick}
          className="mail-pressable mt-2 flex h-9 items-center rounded-lg bg-primary px-4 text-sm font-medium text-primary-foreground shadow-sm hover:bg-primary/90 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
        >
          {action.label}
        </button>
      )}
    </div>
  );
}
