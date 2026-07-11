import { ArrowUpRight, Rss } from "lucide-react";
import { ScrollReveal } from "../motion/ScrollReveal";

export type ChangelogSectionConfig = {
  eyebrow: string;
  headline: string;
  subheadline: string;
  tilesRowOne: string[];
  tilesRowTwo: string[];
  primaryLink: { label: string; href: string };
  rssLink?: { label: string; href: string };
};

type ChangelogSectionProps = {
  config: ChangelogSectionConfig;
};

function MarqueeRow({ items, reverse = false }: { items: string[]; reverse?: boolean }) {
  const content = [
    ...items.map((item) => ({ item, key: `${item}-a` })),
    ...items.map((item) => ({ item, key: `${item}-b` })),
  ];
  return (
    <div className="marquee-viewport">
      <div
        className="marquee-track"
        style={{
          ["--marquee-duration" as string]: "50s",
          animationDirection: reverse ? "reverse" : "normal",
        }}
      >
        {content.map(({ item, key }) => (
          <span
            key={key}
            className="landing-body inline-flex items-center rounded-squircle-md border border-border bg-card/70 px-4 py-2.5 text-sm text-muted-foreground"
          >
            {item}
          </span>
        ))}
      </div>
    </div>
  );
}

export function ChangelogSection({ config }: ChangelogSectionProps) {
  return (
    <section className="section-rhythm border-t border-border bg-background text-foreground">
      <div className="pad-x-ladder">
        <div className="container-marketing">
          <ScrollReveal variant="slide">
            <div className="mx-auto max-w-2xl space-y-4 text-center sm:space-y-5">
              <p className="landing-eyebrow text-muted-foreground">{config.eyebrow}</p>
              <h2 className="landing-section-title">{config.headline}</h2>
              <p className="landing-body text-muted-foreground sm:text-lg">{config.subheadline}</p>
            </div>
          </ScrollReveal>
        </div>
      </div>

      <ScrollReveal variant="fade" className="mt-10 space-y-4 sm:mt-12">
        <MarqueeRow items={config.tilesRowOne} />
        <MarqueeRow items={config.tilesRowTwo} reverse />
      </ScrollReveal>

      <div className="pad-x-ladder">
        <div className="container-marketing">
          <ScrollReveal variant="fade">
            <div className="mt-10 flex flex-wrap items-center justify-center gap-4 sm:mt-12">
              <a href={config.primaryLink.href} className="landing-body inline-flex items-center gap-2 text-foreground transition-opacity hover:opacity-70">
                {config.primaryLink.label}
                <ArrowUpRight className="h-4 w-4" aria-hidden="true" />
              </a>
              {config.rssLink && (
                <a href={config.rssLink.href} className="landing-body inline-flex items-center gap-2 text-muted-foreground transition-opacity hover:text-foreground">
                  <Rss className="h-4 w-4" aria-hidden="true" />
                  {config.rssLink.label}
                </a>
              )}
            </div>
          </ScrollReveal>
        </div>
      </div>
    </section>
  );
}

export default ChangelogSection;
