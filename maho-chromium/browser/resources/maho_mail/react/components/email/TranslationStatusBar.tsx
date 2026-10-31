import { Languages, X } from "lucide-react";

export interface TranslationStatusBarProps {
  fromLang: string;
  toLang: string;
  onClose: () => void;
  error?: string | null;
  onOpenAiSettings?: () => void;
  /** When provided, the target language becomes a picker instead of static text. */
  languages?: ReadonlyArray<{ code: string; name: string }>;
  onChangeToLang?: (code: string) => void;
}

const NEEDS_AI_CONFIG_PATTERN = /^(no ai provider|ai provider not configured|translation provider not yet configured)/i;

export function TranslationStatusBar(props: TranslationStatusBarProps) {
  const { fromLang, toLang, onClose, error, onOpenAiSettings, languages, onChangeToLang } = props;

  const needsAiConfig = error ? NEEDS_AI_CONFIG_PATTERN.test(error) : false;
  const hasError = Boolean(error);

  return (
    <div className="flex flex-col gap-1">
      <div className="flex items-center gap-1.5 rounded-full border border-border bg-background/70 px-2.5 py-1 text-xs font-medium text-muted-foreground">
        <Languages size={12} className="text-primary" />
        {languages && onChangeToLang ? (
          <>
            <span className="uppercase tracking-wide">{fromLang} →</span>
            <select
              aria-label="Translate into"
              title="Translate into"
              value={toLang}
              onChange={(e) => onChangeToLang(e.target.value)}
              className="rounded bg-transparent text-xs font-medium text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary"
            >
              {languages.map((lang) => (
                <option key={lang.code} value={lang.code}>
                  {lang.name}
                </option>
              ))}
            </select>
          </>
        ) : (
          <span className="uppercase tracking-wide">
            {fromLang} → {toLang}
          </span>
        )}
        <button
          type="button"
          onClick={onClose}
          aria-label="Close translation"
          title="Close translation"
          className="mail-pressable flex h-6 w-6 items-center justify-center rounded-md text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
        >
          <X size={12} />
        </button>
      </div>

      {hasError && needsAiConfig && (
        <div className="rounded-md border border-amber-500/30 bg-amber-500/5 px-2 py-1 text-[11px] text-amber-700 dark:text-amber-400">
          AI provider not configured for translation.{" "}
          {onOpenAiSettings && (
            <button
              type="button"
              onClick={onOpenAiSettings}
              aria-label="Open AI settings"
              className="font-medium text-primary hover:underline"
            >
              Open AI settings →
            </button>
          )}
        </div>
      )}
    </div>
  );
}
