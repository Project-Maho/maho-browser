import type * as React from "react";

export interface CtaButtonProps {
  children: React.ReactNode;
  variant?: "primary" | "secondary" | "ghost";
  href?: string;
  onClick?: () => void;
  className?: string;
  type?: "button" | "submit";
}

export function CtaButton({
  children,
  variant = "primary",
  href,
  onClick,
  className,
  type = "button",
}: CtaButtonProps) {
  const classes = ["cta-btn", `cta-btn-${variant}`, className].filter(Boolean).join(" ");

  if (href) {
    return <a href={href} className={classes}>{children}</a>;
  }

  return (
    <button type={type} onClick={onClick} className={classes}>
      {children}
    </button>
  );
}

export default CtaButton;
