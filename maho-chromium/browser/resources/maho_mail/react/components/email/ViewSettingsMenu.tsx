import { useState } from "react";
import {
  Eye,
  ChevronDown,
  Moon,
  Image as ImageIcon,
  Shield,
  Languages,
} from "lucide-react";
import { Popover, PopoverContent, PopoverTrigger } from "../ui/popover";
import { Switch } from "../ui/Switch";

export interface ViewSettingsMenuProps {
  darkMode: boolean;
  onToggleDarkMode: () => void;
  imagesBlocked: boolean;
  blockedImageCount: number;
  onToggleImagesBlocked: () => void;
  trackerCount: number;
  trackerDomains: string[];
  hasActiveTranslation: boolean;
  showTranslation: boolean;
  onToggleShowTranslation: () => void;
}

function SettingsRow({
  id,
  label,
  hint,
  icon,
  checked,
  onCheckedChange,
}: {
  id: string;
  label: string;
  hint?: string;
  icon: React.ReactNode;
  checked: boolean;
  onCheckedChange: (next: boolean) => void;
}) {
  return (
    <label
      htmlFor={id}
      className="flex cursor-pointer items-center gap-3 rounded-md px-2 py-1.5 hover:bg-accent"
    >
      <span className="shrink-0 text-muted-foreground">{icon}</span>
      <span className="flex flex-1 flex-col">
        <span className="text-xs font-medium text-foreground">{label}</span>
        {hint && (
          <span className="text-[10px] text-muted-foreground">{hint}</span>
        )}
      </span>
      <Switch
        id={id}
        checked={checked}
        onCheckedChange={onCheckedChange}
        aria-label={label}
      />
    </label>
  );
}

export function ViewSettingsMenu(props: ViewSettingsMenuProps) {
  const {
    darkMode,
    onToggleDarkMode,
    imagesBlocked,
    blockedImageCount,
    onToggleImagesBlocked,
    trackerCount,
    trackerDomains,
    hasActiveTranslation,
    showTranslation,
    onToggleShowTranslation,
  } = props;

  const [open, setOpen] = useState(false);
  const hasBlocking = (imagesBlocked && blockedImageCount > 0) || trackerCount > 0;

  const imageHint =
    imagesBlocked && blockedImageCount > 0
      ? `${blockedImageCount} blocked`
      : undefined;

  const trackerDomainPreview =
    trackerDomains.length > 0
      ? `${trackerDomains.slice(0, 3).join(", ")}${
          trackerDomains.length > 3 ? ` +${trackerDomains.length - 3} more` : ""
        }`
      : "";

  return (
    <Popover open={open} onOpenChange={setOpen}>
      <PopoverTrigger asChild>
        <button
          type="button"
          aria-label="View settings"
          aria-haspopup="dialog"
          aria-expanded={open}
          className="mail-pressable relative flex h-7 items-center gap-1.5 rounded-md px-2 text-xs text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
        >
          <Eye size={12} />
          View
          <ChevronDown size={10} className="opacity-60" />
          {hasBlocking && (
            <span
              data-testid="view-settings-blocking-dot"
              aria-hidden="true"
              className="absolute right-1 top-1 h-1.5 w-1.5 rounded-full bg-primary"
            />
          )}
        </button>
      </PopoverTrigger>
      <PopoverContent align="end" sideOffset={4} className="w-72 p-2">
        <div className="space-y-3">
          <section>
            <p className="px-2 pb-1 text-[10px] font-semibold uppercase tracking-wide text-muted-foreground">
              Rendering
            </p>
            <SettingsRow
              id="view-settings-dark-mode"
              label="Dark mode rendering"
              icon={<Moon size={14} />}
              checked={darkMode}
              onCheckedChange={() => onToggleDarkMode()}
            />
            <SettingsRow
              id="view-settings-show-images"
              label="Show remote images"
              hint={imageHint}
              icon={<ImageIcon size={14} />}
              checked={!imagesBlocked}
              onCheckedChange={() => onToggleImagesBlocked()}
            />
          </section>

          {trackerCount > 0 && (
            <section>
              <p className="px-2 pb-1 text-[10px] font-semibold uppercase tracking-wide text-muted-foreground">
                Privacy
              </p>
              <div className="flex items-start gap-3 rounded-md px-2 py-1.5">
                <span className="shrink-0 pt-0.5 text-primary">
                  <Shield size={14} />
                </span>
                <span className="flex flex-1 flex-col gap-0.5">
                  <span className="text-xs font-medium text-foreground">
                    {trackerCount} tracker{trackerCount > 1 ? "s" : ""} blocked
                  </span>
                  {trackerDomainPreview && (
                    <span className="break-all text-[10px] text-muted-foreground">
                      {trackerDomainPreview}
                    </span>
                  )}
                </span>
              </div>
            </section>
          )}

          {hasActiveTranslation && (
            <section>
              <p className="px-2 pb-1 text-[10px] font-semibold uppercase tracking-wide text-muted-foreground">
                Translation
              </p>
              <SettingsRow
                id="view-settings-show-translation"
                label="Show translation"
                icon={<Languages size={14} />}
                checked={showTranslation}
                onCheckedChange={() => onToggleShowTranslation()}
              />
            </section>
          )}
        </div>
      </PopoverContent>
    </Popover>
  );
}
