import {Button} from '@ui/button';

const DISCORD_COMMUNITY_STYLES = `
@keyframes maho-discord-glow {
  0%, 100% { box-shadow: 0 0 4px 0 rgba(88, 101, 242, 0.18); }
  50% { box-shadow: 0 0 16px 2px rgba(88, 101, 242, 0.5); }
}
.maho-discord-community {
  animation: maho-discord-glow 3.2s ease-in-out infinite;
  will-change: box-shadow;
}
.maho-discord-community:is(:hover, :focus-within) {
  animation-play-state: paused;
}
@media (prefers-reduced-motion: reduce) {
  .maho-discord-community {
    animation: none;
    box-shadow: 0 0 8px 0 rgba(88, 101, 242, 0.28);
    will-change: auto;
  }
}`;

function DiscordBrandIcon() {
  return (
    <svg
        aria-hidden="true"
        className="size-full"
        role="img"
        viewBox="0 0 24 24"
        xmlns="http://www.w3.org/2000/svg">
      <rect width="24" height="24" fill="#5865F2" />
      <g transform="translate(4.56 4.56) scale(0.62)">
        <path
            fill="#FFFFFF"
            d="M20.317 4.3698a19.7913 19.7913 0 0 0-4.8851-1.5152.0741.0741 0 0 0-.0785.0371c-.211.3753-.4447.8648-.6083 1.2495-1.8447-.2762-3.68-.2762-5.4868 0-.1636-.3933-.4058-.8742-.6177-1.2495a.077.077 0 0 0-.0785-.037 19.7363 19.7363 0 0 0-4.8852 1.515.0699.0699 0 0 0-.0321.0277C.5334 9.0458-.319 13.5799.0992 18.0578a.0824.0824 0 0 0 .0312.0561c2.0528 1.5076 4.0413 2.4228 5.9929 3.0294a.0777.0777 0 0 0 .0842-.0276c.4616-.6304.8731-1.2952 1.226-1.9942a.076.076 0 0 0-.0416-.1057c-.6528-.2476-1.2743-.5495-1.8722-.8923a.077.077 0 0 1-.0076-.1277c.1258-.0943.2517-.1923.3718-.2914a.0743.0743 0 0 1 .0776-.0105c3.9278 1.7933 8.18 1.7933 12.0614 0a.0739.0739 0 0 1 .0785.0095c.1202.099.246.1981.3728.2924a.077.077 0 0 1-.0066.1276 12.2986 12.2986 0 0 1-1.873.8914.0766.0766 0 0 0-.0407.1067c.3604.698.7719 1.3628 1.225 1.9932a.076.076 0 0 0 .0842.0286c1.961-.6067 3.9495-1.5219 6.0023-3.0294a.077.077 0 0 0 .0313-.0552c.5004-5.177-.8382-9.6739-3.5485-13.6604a.061.061 0 0 0-.0312-.0286zM8.02 15.3312c-1.1825 0-2.1569-1.0857-2.1569-2.419 0-1.3332.9555-2.4189 2.157-2.4189 1.2108 0 2.1757 1.0952 2.1568 2.419 0 1.3332-.9555 2.4189-2.1569 2.4189zm7.9748 0c-1.1825 0-2.1569-1.0857-2.1569-2.419 0-1.3332.9554-2.4189 2.1569-2.4189 1.2108 0 2.1757 1.0952 2.1568 2.419 0 1.3332-.946 2.4189-2.1568 2.4189Z" />
      </g>
    </svg>
  );
}

export function DiscordCommunitySettingsSection() {
  return (
    <div className="mb-4 px-1">
      <div
          className="maho-discord-community grid min-h-14 w-full grid-cols-[auto_minmax(0,1fr)_auto] items-center gap-3 rounded-xl border border-border/70 bg-card/95 px-3 py-2 shadow-sm ring-1 ring-background/40"
          role="note">
        <div className="flex h-10 w-14 shrink-0 items-center" aria-hidden="true">
          <span className="relative flex size-8 items-center justify-center overflow-hidden rounded-xl border border-border/80 bg-background shadow-sm ring-1 ring-background/70">
            <DiscordBrandIcon />
            <span className="pointer-events-none absolute inset-0 rounded-xl ring-1 ring-inset ring-foreground/10" />
          </span>
        </div>
        <p className="min-w-0 text-xs font-medium leading-4 text-foreground">
          Join the Maho community on Discord
        </p>
        <Button
            aria-label="Join the Maho community on Discord in a new tab"
            className="h-8 shrink-0 rounded-lg border-border bg-background/70 px-2.5 text-xs font-semibold text-foreground shadow-none hover:bg-surface-hover"
            size="sm"
            type="button"
            variant="outline"
            onClick={() => window.open('https://discord.gg/bfqrB55UF', '_blank', 'noopener')}>
          Join Discord
        </Button>
        <style>{DISCORD_COMMUNITY_STYLES}</style>
      </div>
    </div>
  );
}
