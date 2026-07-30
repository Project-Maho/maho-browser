import * as React from "react"

import { cn } from "@/lib/utils"

export interface InputProps
  extends Omit<React.InputHTMLAttributes<HTMLInputElement>, "onChange"> {
  label?: string;
  error?: string;
  onChange?: (value: string) => void;
}

const Input = React.forwardRef<HTMLInputElement, InputProps>(
  ({ className, type, label, error, onChange, id, ...props }, ref) => {
    const generatedId = React.useId();
    const effectiveId = id ?? generatedId;
    const errorId = `${effectiveId}-error`;

    const handleChange = React.useCallback(
      (e: React.ChangeEvent<HTMLInputElement>) => {
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
        <input
          type={type}
          id={effectiveId}
          aria-invalid={!!error}
          aria-describedby={error ? errorId : undefined}
          className={cn(
            "flex h-9 w-full rounded-md border border-input bg-transparent px-3 py-1 text-base shadow-sm transition-colors file:border-0 file:bg-transparent file:text-sm file:font-medium file:text-foreground placeholder:text-muted-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring disabled:cursor-not-allowed disabled:opacity-50 md:text-sm aria-[invalid=true]:border-destructive aria-[invalid=true]:ring-destructive/20",
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
    )
  }
)
Input.displayName = "Input"

export { Input }
