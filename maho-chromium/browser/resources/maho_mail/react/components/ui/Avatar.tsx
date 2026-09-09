import { getInitials } from "./utils";

export interface AvatarProps {
  name: string;
  email?: string;
  size?: "sm" | "md" | "lg" | "xl";
  className?: string;
  isVip?: boolean;
}

function hashColor(key: string): string {
  let hash = 0;
  for (let i = 0; i < key.length; i++) {
    hash = key.charCodeAt(i) + ((hash << 5) - hash);
  }

  const colors = [
    "bg-blue-600",
    "bg-emerald-600",
    "bg-violet-600",
    "bg-amber-600",
    "bg-rose-600",
    "bg-cyan-600",
    "bg-indigo-600",
    "bg-pink-600",
  ];

  return colors[Math.abs(hash) % colors.length];
}

export function Avatar({ name, email, size = "md", className = "", isVip }: AvatarProps) {
  const sizes: Record<string, string> = {
    sm: "h-6 w-6 text-[10px]",
    md: "h-8 w-8 text-xs",
    lg: "h-10 w-10 text-sm",
    xl: "h-16 w-16 text-lg",
  };

  const addressKey = (email?.trim() || name).trim().toLowerCase();

  return (
    <div className={`relative inline-flex items-center justify-center rounded-full font-medium text-white ${hashColor(addressKey)} ${sizes[size] ?? sizes.md} ${className}`}>
      {getInitials(name)}
      {isVip && (
        <span className="absolute -right-0.5 -top-0.5 flex h-3 w-3 items-center justify-center rounded-full bg-amber-500 text-[6px] font-bold text-black ring-1 ring-zinc-950">
          ★
        </span>
      )}
    </div>
  );
}
