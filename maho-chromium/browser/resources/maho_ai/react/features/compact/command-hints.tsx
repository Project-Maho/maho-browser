const SLASH_COMMANDS = [
  '/agent new',
  '/tools add mcp',
  '/tools add cli',
] as const;

// Discoverable affordance for the slash commands (P1-13): clicking fills the
// composer with the command so it can be reviewed and sent.
export function CommandHints(
    {disabled, onPick}: {disabled: boolean; onPick: (command: string) => void}) {
  if (disabled) {
    return null;
  }
  return (
    <details className="min-w-0" data-command-disclosure>
      <summary className="inline-flex h-7 cursor-pointer select-none items-center rounded-lg px-1.5 text-[11px] font-medium text-muted-foreground outline-none transition-colors hover:text-foreground focus-visible:ring-1 focus-visible:ring-ring [&::-webkit-details-marker]:hidden">
        Commands
      </summary>
      <div aria-label="Slash commands" className="mt-0.5 grid min-w-0 gap-0.5" role="toolbar">
        {SLASH_COMMANDS.map(command => (
          <button
            className="flex h-7 min-w-0 items-center truncate rounded-lg px-1.5 text-left font-mono text-[11px] text-muted-foreground outline-none transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:ring-1 focus-visible:ring-ring"
            data-command-hint={command}
            key={command}
            onClick={() => onPick(command)}
            type="button">
            {command}
          </button>
        ))}
      </div>
    </details>
  );
}
