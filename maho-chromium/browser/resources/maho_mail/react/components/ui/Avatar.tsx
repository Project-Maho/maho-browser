import { useState, useEffect } from "react";
import { getInitials } from "./utils";
import * as api from "../../api";

export interface AvatarProps {
  name: string;
  email?: string;
  size?: "sm" | "md" | "lg" | "xl";
  className?: string;
  isVip?: boolean;
}

function hashColor(name: string): string {
  let hash = 0;
  for (let i = 0; i < name.length; i++) {
    hash = name.charCodeAt(i) + ((hash << 5) - hash);
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

function gravatarUrl(hash: string, sizePx: number): string {
  return `https://www.gravatar.com/avatar/${hash}?s=${sizePx}&d=404`;
}

export function Avatar({ name, email, size = "md", className = "", isVip }: AvatarProps) {
  const [imgFailed, setImgFailed] = useState(false);
  const [gravatarHash, setGravatarHash] = useState<string | null>(null);

  useEffect(() => {
    setImgFailed(false);
    setGravatarHash(null);
    if (!email) return;
    let cancelled = false;
    api.md5Hash(email.trim().toLowerCase()).then((hash) => {
      if (!cancelled) setGravatarHash(hash);
    }).catch(() => undefined);
    return () => { cancelled = true; };
  }, [email]);

  const sizes: Record<string, string> = {
    sm: "h-6 w-6 text-[10px]",
    md: "h-8 w-8 text-xs",
    lg: "h-10 w-10 text-sm",
    xl: "h-16 w-16 text-lg",
  };

  const pixelSizes: Record<string, number> = { sm: 24, md: 32, lg: 40, xl: 64 };
  const showGravatar = gravatarHash && !imgFailed;

  return (
    <div className={`relative inline-flex items-center justify-center rounded-full font-medium text-white ${showGravatar ? "" : hashColor(name)} ${sizes[size]} ${className}`}>
      {showGravatar ? (
        <img
          src={gravatarUrl(gravatarHash, pixelSizes[size] * 2)}
          alt=""
          className="h-full w-full rounded-full object-cover"
          onError={() => setImgFailed(true)}
        />
      ) : (
        getInitials(name)
      )}
      {isVip && (
        <span className="absolute -right-0.5 -top-0.5 flex h-3 w-3 items-center justify-center rounded-full bg-amber-500 text-[6px] font-bold text-black ring-1 ring-zinc-950">
          ★
        </span>
      )}
    </div>
  );
}
