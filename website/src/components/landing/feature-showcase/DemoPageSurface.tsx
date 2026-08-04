import React from "react";
import { Bookmark, MessageCircle, PlayCircle } from "lucide-react";

export type DemoPageVariant = "article" | "research" | "longreads" | "crash";

export interface DemoPageSurfaceProps {
  readonly variant: DemoPageVariant;
  readonly mode?: "full" | "compact";
  readonly dimmed?: boolean;
  readonly children?: React.ReactNode;
}

const ARTICLE_CARDS = [
  { eyebrow: "Essay / Art", title: "The savage bites back", body: "A study in attention, ornament, and the small revolt of seeing." },
  { eyebrow: "Video / Architecture", title: "Rooms that keep changing", body: "Inside homes where every threshold holds another way to work." },
  { eyebrow: "Essay / Philosophy", title: "The people's atheist", body: "What belief leaves behind when public life becomes practical." },
] as const;

const RESEARCH_BLOCKS = [
  "The station walls begin to hold the morning heat before the city notices.",
  "Signals gather below street level: fans, tunnels, service doors, rain grates.",
  "By evening the map has changed from routes into a weather instrument.",
] as const;

const LONGREADS_BLOCKS = [
  "The best writing this week, gathered from magazines, independent desks, and the far edges of the internet.",
  "Editor's Picks pull long essays, deep reporting, and profiles worth the hour they ask of you.",
  "Support keeps the recommendations free — and the reading list honest.",
] as const;

function DemoHeader({ variant }: { readonly variant: DemoPageVariant }): React.ReactNode {
  if (variant === "article") {
    return (
      <header className="border-b border-zinc-200 bg-white px-[2.2cqw] py-[1.2cqw] text-zinc-900">
        <div className="flex items-center justify-between text-[0.72cqw] font-semibold uppercase tracking-[0.18em] text-zinc-500">
          <span>Menu / Search</span>
          <span>Newsletter / Sign in</span>
        </div>
        <div className="mt-[0.6cqw] text-center font-serif text-[3.1cqw] font-semibold tracking-[-0.08em] text-rose-900">aeon</div>
      </header>
    );
  }

  return (
    <header className="border-b border-zinc-200 bg-stone-50 px-[2cqw] py-[1.2cqw] text-zinc-900">
      <div className="text-[0.68cqw] font-semibold uppercase tracking-[0.2em] text-zinc-500">Field document / Urban climate</div>
      <h2 className="mt-[0.3cqw] text-[1.55cqw] font-semibold tracking-[-0.04em] text-zinc-950">Listening for weather beneath the city</h2>
    </header>
  );
}

function ArticlePage({ compact }: { readonly compact: boolean }): React.ReactNode {
  return (
    <>
      <div className="border-b border-zinc-200 bg-white px-[2cqw] pb-[1cqw] pt-[0.8cqw] text-zinc-950">
        <div className="text-[0.66cqw] font-semibold uppercase tracking-[0.14em] text-rose-800">Essay / Pleasure and pain</div>
      </div>
      <div className="relative min-h-[16cqw] overflow-hidden bg-rose-950 text-white">
        <div className="absolute inset-y-0 left-0 w-[32%] bg-stone-100/95" />
        <div className="absolute inset-y-0 right-0 w-[68%] bg-[radial-gradient(circle_at_35%_38%,rgba(255,255,255,0.28),transparent_18%),linear-gradient(135deg,rgba(190,24,93,0.92),rgba(127,29,29,0.96))]" />
        <div className="relative ml-auto flex min-h-[16cqw] w-[66%] flex-col items-center justify-center px-[3.4cqw] text-center">
          <h1 className="font-serif text-[2.2cqw] leading-[0.96] tracking-[-0.06em]">Signal then storm</h1>
          <p className="mt-[0.8cqw] max-w-[26cqw] text-[0.84cqw] leading-relaxed text-white/86">Allan Basbaum spent a career searching for where pain begins — and where it can be stopped.</p>
          <div className="mt-[0.9cqw] text-[0.68cqw] font-semibold uppercase tracking-[0.16em] text-white/70">Stephanie Sutherland</div>
        </div>
      </div>
      <div className={`grid gap-[1.1cqw] bg-white px-[2cqw] py-[1.4cqw] ${compact ? "grid-cols-1" : "grid-cols-2"}`}>
        {ARTICLE_CARDS.slice(0, compact ? 2 : 3).map((card, index) => (
          <article key={card.title} className="min-w-0 text-zinc-950">
            <div className="relative h-[5.6cqw] overflow-hidden rounded-[0.5cqw] bg-gradient-to-br from-stone-200 via-rose-100 to-zinc-300">
              {index === 1 && <PlayCircle className="absolute bottom-[0.7cqw] right-[0.7cqw] h-[1.5cqw] w-[1.5cqw] text-white drop-shadow" />}
            </div>
            <div className="mt-[0.6cqw] flex items-center gap-[0.35cqw] text-[0.64cqw] font-semibold uppercase tracking-[0.12em] text-zinc-500">
              <Bookmark className="h-[0.74cqw] w-[0.74cqw]" />{card.eyebrow}
            </div>
            <h3 className="mt-[0.3cqw] font-serif text-[1.08cqw] leading-tight tracking-[-0.035em]">{card.title}</h3>
          </article>
        ))}
      </div>
    </>
  );
}

function ResearchPage(): React.ReactNode {
  return (
    <main className="bg-stone-50 px-[2cqw] py-[1.5cqw] text-zinc-900">
      <div className="grid grid-cols-[1fr_0.7fr] gap-[1.5cqw]">
        <article>
          <p className="text-[0.82cqw] leading-[1.8] text-zinc-600">A field record of the tunnels, chambers, and quiet signals that shape a summer storm before it reaches the street.</p>
          <div className="mt-[1cqw] space-y-[0.7cqw]">
            {RESEARCH_BLOCKS.map((block) => <p key={block} className="text-[0.88cqw] leading-[1.75] text-zinc-700">{block}</p>)}
          </div>
        </article>
        <aside className="rounded-[0.7cqw] border border-zinc-200 bg-white p-[1cqw] shadow-sm">
          <div className="text-[0.68cqw] font-semibold uppercase tracking-[0.16em] text-zinc-500">Notes</div>
          <div className="mt-[0.7cqw] h-[8cqw] rounded-[0.55cqw] bg-[linear-gradient(135deg,rgba(120,113,108,0.18),rgba(68,64,60,0.06))]" />
        </aside>
      </div>
    </main>
  );
}

function LongreadsPage(): React.ReactNode {
  return (
    <div className="relative h-full w-full bg-stone-100 text-zinc-900">
      <header className="flex items-center justify-between border-b border-zinc-200 bg-white px-[2cqw] py-[1.1cqw]">
        <div className="font-serif text-[1.7cqw] font-bold tracking-[-0.04em] text-zinc-950">Longreads</div>
        <div className="flex items-center gap-[1cqw] text-[0.72cqw] font-semibold uppercase tracking-[0.12em] text-zinc-600">
          <span className="hidden sm:inline">Best of</span>
          <span className="rounded-[0.4cqw] bg-rose-600 px-[0.9cqw] py-[0.45cqw] text-[0.72cqw] font-bold uppercase tracking-[0.1em] text-white">Support Us</span>
        </div>
      </header>
      <main className="px-[2cqw] py-[1.4cqw]">
        <div className="text-[0.66cqw] font-semibold uppercase tracking-[0.16em] text-rose-700">Editor's Picks</div>
        <h2 className="mt-[0.4cqw] font-serif text-[1.5cqw] font-semibold leading-tight tracking-[-0.04em]">The stories that stayed with us</h2>
        <div className="mt-[1cqw] space-y-[0.7cqw]">
          {LONGREADS_BLOCKS.map((block) => <p key={block} className="text-[0.82cqw] leading-[1.7] text-zinc-600">{block}</p>)}
        </div>
      </main>
    </div>
  );
}

function CrashPage(): React.ReactNode {
  return (
    <main className="grid h-full place-items-center bg-zinc-950 px-[2cqw] text-center text-zinc-200">
      <div>
        <div className="mx-auto grid h-[3.8cqw] w-[3.8cqw] place-items-center rounded-full border border-zinc-700 text-[1.4cqw]">!</div>
        <h3 className="mt-[1cqw] text-[1.4cqw] font-semibold">Aw, Snap!</h3>
        <p className="mt-[0.5cqw] max-w-[18cqw] text-[0.78cqw] leading-relaxed text-zinc-500">This pane paused while the other page stays available.</p>
        <button type="button" className="mt-[1cqw] rounded-full border border-zinc-700 px-[1.1cqw] py-[0.45cqw] text-[0.72cqw] font-semibold text-zinc-200">Reload</button>
      </div>
    </main>
  );
}

export const DemoPageSurface: React.FC<DemoPageSurfaceProps> = ({ variant, mode = "full", dimmed = false, children }) => {
  const compact = mode === "compact";

  if (variant === "longreads") {
    return (
      <div data-demo-page="longreads" className="relative h-full w-full overflow-hidden bg-stone-100 text-zinc-950">
        <LongreadsPage />
        {children}
      </div>
    );
  }

  return (
    <div data-demo-page={variant} className="relative h-full w-full overflow-hidden rounded-none bg-white text-zinc-950">
      <DemoHeader variant={variant} />
      {variant === "article" && <ArticlePage compact={compact} />}
      {variant === "research" && <ResearchPage />}
      {variant === "crash" && <CrashPage />}
      {dimmed && <div aria-hidden="true" className="absolute inset-0 bg-zinc-950/38" />}
      {children}
      {variant === "article" && <MessageCircle className="absolute right-[1cqw] top-[9.8cqw] h-[1.6cqw] w-[1.6cqw] rounded-full bg-white p-[0.35cqw] text-zinc-700 shadow" />}
    </div>
  );
};
