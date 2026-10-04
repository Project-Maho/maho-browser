import * as React from "react"

import { cn } from "@/lib/utils"

export interface TextareaProps
  extends Omit<React.TextareaHTMLAttributes<HTMLTextAreaElement>, "onChange"> {
  label?: string;
  error?: string;
  onChange?: (value: string) => void;
}

const Textarea = React.forwardRef<HTMLTextAreaElement, TextareaProps>(
  ({ className, label, error, onChange, id, "aria-describedby": ariaDescribedBy, ...props }, ref) => {
    const generatedId = React.useId();
    const effectiveId = id ?? generatedId;
    const errorId = `${effectiveId}-error`;
    const effectiveDescribedBy = [ariaDescribedBy, error ? errorId : undefined]
      .filter(Boolean)
      .join(" ") || undefined;

    const handleChange = React.useCallback(
      (e: React.ChangeEvent<HTMLTextAreaElement>) => {
        if (!onChange) return;
        onChange(e.target.value);
      },
      [onChange],
    );

    return (
      <div className="flex flex-col gap-1.5">
        {label && (
          <label htmlFor={effectiveId} className="text-sm font-medium text-foreground">
            {label}
          </label>
        )}
        <textarea
          id={effectiveId}
          aria-invalid={!!error}
          aria-describedby={effectiveDescribedBy}
          className={cn(
            "flex min-h-[60px] w-full rounded-md border border-input bg-transparent px-3 py-2 text-base shadow-sm placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring disabled:cursor-not-allowed disabled:opacity-50 md:text-sm aria-[invalid=true]:border-destructive aria-[invalid=true]:ring-destructive/20",
            className,
          )}
          onChange={handleChange}
          ref={ref}
          {...props}
        />
        {error && (
          <p id={errorId} className="text-xs text-destructive">{error}</p>
        )}
      </div>
    );
  },
);
Textarea.displayName = "Textarea";

export { Textarea }
