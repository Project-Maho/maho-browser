import React from "react";
import { monetWallpaperClassName, monetWallpaperStyle } from "./monetWallpaper";

export type MahoWallpaperMatProps = {
  readonly children: React.ReactNode;
  readonly className?: string;
};

export const MahoWallpaperMat: React.FC<MahoWallpaperMatProps> = ({ children, className = "" }) => (
  <div
    className={`w-full h-full p-[2.5cqw] rounded-[1.8cqw] select-none ${monetWallpaperClassName} ${className}`}
    style={monetWallpaperStyle}
  >
    {children}
  </div>
);

export type MahoWindowProps = {
  readonly children: React.ReactNode;
  readonly className?: string;
};

export const MahoWindow: React.FC<MahoWindowProps> = ({ children, className = "" }) => (
  <div
    className={`w-full h-full flex rounded-[1.2cqw] overflow-hidden relative shadow-[0_20px_60px_var(--maho-shadow-color)] border border-[var(--maho-border-strong)] bg-[var(--maho-window)] text-[var(--maho-text-primary)] ${className}`}
  >
    {children}
  </div>
);

export type MahoContentProps = {
  readonly children: React.ReactNode;
  readonly className?: string;
};

export const MahoContent: React.FC<MahoContentProps> = ({ children, className = "" }) => (
  <div className={`flex-1 relative overflow-hidden bg-[var(--maho-window)] ${className}`}>
    {children}
  </div>
);
