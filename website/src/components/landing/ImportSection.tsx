import { ArrowDown } from "lucide-react";
import { ScrollReveal } from "../motion/ScrollReveal";

export type ImportSource = {
  name: string;
  logo: string;
};

export type ImportSectionConfig = {
  eyebrow: string;
  headline: string;
  subheadline: string;
  sources: ImportSource[];
  destinationLabel: string;
};

type ImportSectionProps = {
  config: ImportSectionConfig;
};

const transferItems = ["Bookmarks", "Tabs", "History", "Passwords"];

export function ImportSection({ config }: ImportSectionProps) {
  return (
    <section className="section-rhythm pad-x-ladder border-t border-border bg-background text-foreground">
      <div className="container-marketing grid gap-10 lg:grid-cols-[minmax(0,0.9fr)_minmax(0,1.1fr)] lg:items-center">
        <ScrollReveal variant="slide">
          <div className="max-w-2xl space-y-4 sm:space-y-5">
            <p className="landing-eyebrow text-muted-foreground">{config.eyebrow}</p>
            <h2 className="landing-section-title">{config.headline}</h2>
            <p className="landing-body text-muted-foreground sm:text-lg">{config.subheadline}</p>
            <div className="flex flex-wrap gap-2 pt-2">
              {transferItems.map((item) => (
                <span key={item} className="landing-eyebrow rounded-full border border-border px-3 py-2 text-[0.65rem] text-muted-foreground">
                  {item}
                </span>
              ))}
            </div>
          </div>
        </ScrollReveal>

        <ScrollReveal variant="fade">
          <div className="rounded-squircle-xl squircle-enhanced border border-border bg-card/70 p-5 sm:p-7">
            <p className="landing-eyebrow mb-4 text-center text-muted-foreground">Import from</p>
            <div className="grid gap-2.5 sm:grid-cols-2">
              {config.sources.map((source) => (
                <div
                  key={source.name}
                  className="flex items-center justify-center gap-3 rounded-squircle-md border border-border bg-background/60 px-4 py-3"
                >
                  <img
                    src={source.logo}
                    alt=""
                    width={22}
                    height={22}
                    className="h-[22px] w-[22px] shrink-0 object-contain"
                    aria-hidden="true"
                    loading="lazy"
                    decoding="async"
                  />
                  <span className="landing-body text-sm text-foreground">{source.name}</span>
                </div>
              ))}
            </div>

            <div className="flex items-center gap-4 py-6 sm:py-8">
              <div className="h-px flex-1 bg-border" aria-hidden="true" />
              <ArrowDown className="h-4 w-4 text-muted-foreground" aria-hidden="true" />
              <div className="h-px flex-1 bg-border" aria-hidden="true" />
            </div>

            <div className="rounded-squircle-xl border border-border/80 bg-background px-5 py-6">
              <div className="flex items-center justify-center gap-4">
                <img src="/icon-512.png" alt="" width="40" height="40" className="h-10 w-10 rounded-md" aria-hidden="true" />
                <p className="landing-subsection-title text-foreground">{config.destinationLabel}</p>
              </div>
            </div>
          </div>
        </ScrollReveal>
      </div>
    </section>
  );
}

export default ImportSection;
