import type { ReactNode } from "react";
import { ScrollReveal } from "../motion/ScrollReveal";
import type { OptimizedImageData } from "../../types/images";

type ImageFeatureVisual = {
  type: "image";
  alt: string;
  sizes?: string;
  image?: OptimizedImageData;
};

type SchematicFeatureVisual = {
  type: "schematic";
  variant: "split-view" | "command-bar" | "providers";
  label: string;
};

export type FeatureVisual = ImageFeatureVisual | SchematicFeatureVisual;

export type FeatureItem = {
  eyebrow?: string;
  title: string;
  body: string;
  visual?: FeatureVisual;
};

type FeaturesSectionProps = {
  eyebrow: string;
  headline: string;
  items: FeatureItem[];
  textOnly?: boolean;
};

function renderImageVisual(visual: ImageFeatureVisual) {
  if (visual.image) {
    return (
      <div className="overflow-hidden rounded-squircle-lg border border-border/70 bg-background">
        <img
          src={visual.image.src}
          srcSet={visual.image.srcSet || undefined}
          sizes={visual.sizes || "(min-width: 1024px) 50vw, 100vw"}
          alt={visual.alt}
          width={visual.image.width}
          height={visual.image.height}
          loading="lazy"
          decoding="async"
          className="block h-auto w-full"
        />
      </div>
    );
  }

  return renderImageFallbackVisual();
}

function SchematicFrame({ children, label }: { children: ReactNode; label: string }) {
  return (
    <div className="relative flex min-h-[15rem] overflow-hidden rounded-squircle-lg border border-border/70 bg-background">
      <div
        className="absolute inset-0 bg-[radial-gradient(circle_at_20%_0%,color-mix(in_srgb,var(--foreground)_12%,transparent),transparent_38%),radial-gradient(circle_at_80%_100%,color-mix(in_srgb,var(--muted)_72%,transparent),transparent_48%),linear-gradient(180deg,color-mix(in_srgb,var(--card)_94%,var(--foreground)_6%),var(--background))]"
        aria-hidden="true"
      />
      <div className="relative flex w-full flex-col p-5 sm:p-6">
        <div className="flex flex-1 flex-col rounded-squircle-md border border-border/70 bg-card/80 p-3 shadow-[0_1rem_2.5rem_color-mix(in_srgb,var(--background)_72%,transparent)]">
          {children}
        </div>
        <p className="landing-eyebrow mt-4 text-muted-foreground">{label}</p>
      </div>
    </div>
  );
}

function BrowserChrome() {
  return (
    <div className="mb-3 flex items-center gap-2 border-b border-border/70 pb-3" aria-hidden="true">
      <span className="h-2 w-2 rounded-full bg-muted-foreground/40" />
      <span className="h-2 w-2 rounded-full bg-muted-foreground/30" />
      <span className="h-2 w-2 rounded-full bg-muted-foreground/20" />
      <span className="ml-2 h-2 flex-1 rounded-full bg-background" />
    </div>
  );
}

function renderImageFallbackVisual() {
  return (
    <SchematicFrame label="Browser workspace">
      <BrowserChrome />
      <div className="grid flex-1 grid-cols-[0.7fr_1.3fr] gap-3" aria-hidden="true">
        <div className="rounded-squircle-sm border border-border/70 bg-background/80 p-3">
          <div className="space-y-2">
            <div className="h-2 w-4/5 rounded-full bg-muted-foreground/25" />
            <div className="h-2 w-2/3 rounded-full bg-muted-foreground/20" />
            <div className="h-2 w-3/4 rounded-full bg-muted-foreground/20" />
          </div>
        </div>
        <div className="rounded-squircle-sm border border-border/70 bg-background/70 p-3">
          <div className="mb-3 h-14 rounded-squircle-sm bg-muted/80" />
          <div className="space-y-2">
            <div className="h-2 w-3/5 rounded-full bg-muted-foreground/30" />
            <div className="h-2 w-4/5 rounded-full bg-muted-foreground/20" />
          </div>
        </div>
      </div>
    </SchematicFrame>
  );
}

function SplitViewSchematic() {
  return (
    <>
      <BrowserChrome />
      <div className="grid flex-1 grid-cols-2 gap-3" aria-hidden="true">
        <div className="rounded-squircle-sm border border-border/70 bg-background/80 p-3">
          <div className="mb-3 h-16 rounded-squircle-sm bg-muted/80" />
          <div className="space-y-2">
            <div className="h-2 w-3/4 rounded-full bg-muted-foreground/30" />
            <div className="h-2 w-5/6 rounded-full bg-muted-foreground/20" />
            <div className="h-2 w-1/2 rounded-full bg-muted-foreground/20" />
          </div>
        </div>
        <div className="rounded-squircle-sm border border-foreground/20 bg-background p-3 ring-1 ring-foreground/10">
          <div className="mb-3 flex gap-2">
            <div className="h-10 flex-1 rounded-squircle-sm bg-muted/90" />
            <div className="h-10 flex-1 rounded-squircle-sm bg-muted/60" />
          </div>
          <div className="space-y-2">
            <div className="h-2 w-2/3 rounded-full bg-foreground/45" />
            <div className="h-2 w-5/6 rounded-full bg-muted-foreground/25" />
            <div className="h-2 w-3/5 rounded-full bg-muted-foreground/20" />
          </div>
        </div>
      </div>
    </>
  );
}

function CommandBarSchematic() {
  return (
    <div className="relative flex flex-1 items-center justify-center overflow-hidden rounded-squircle-sm border border-border/60 bg-background/70 p-4" aria-hidden="true">
      <div className="absolute inset-4 grid grid-cols-[0.8fr_1.2fr] gap-3 opacity-45">
        <div className="rounded-squircle-sm border border-border bg-card/70" />
        <div className="space-y-2 rounded-squircle-sm border border-border bg-card/70 p-3">
          <div className="h-2 w-1/2 rounded-full bg-muted-foreground/30" />
          <div className="h-2 w-5/6 rounded-full bg-muted-foreground/20" />
          <div className="h-2 w-2/3 rounded-full bg-muted-foreground/20" />
        </div>
      </div>
      <div className="relative w-full max-w-sm rounded-squircle-md border border-foreground/20 bg-card/95 p-3 shadow-[0_1rem_2.5rem_color-mix(in_srgb,var(--background)_65%,transparent)]">
        <div className="flex items-center gap-3 rounded-squircle-sm border border-border bg-background px-3 py-2.5">
          <span className="font-mono text-xs text-muted-foreground">⌘K</span>
          <span className="h-2 flex-1 rounded-full bg-muted-foreground/25" />
        </div>
        <div className="mt-3 space-y-2">
          <div className="flex items-center gap-3 rounded-squircle-sm bg-background/70 px-3 py-2">
            <span className="h-2.5 w-2.5 rounded-full bg-foreground/55" />
            <span className="h-2 w-2/3 rounded-full bg-muted-foreground/30" />
          </div>
          <div className="flex items-center gap-3 rounded-squircle-sm px-3 py-2">
            <span className="h-2.5 w-2.5 rounded-full bg-muted-foreground/25" />
            <span className="h-2 w-1/2 rounded-full bg-muted-foreground/20" />
          </div>
        </div>
      </div>
    </div>
  );
}

function ProvidersSchematic() {
  return (
    <>
      <BrowserChrome />
      <div className="flex flex-1 flex-col gap-2" aria-hidden="true">
        {[
          ["OpenAI", "bg-foreground"],
          ["Anthropic", "bg-muted-foreground/50"],
          ["Google", "bg-muted-foreground/35"],
          ["OpenRouter", "bg-muted-foreground/25"],
        ].map(([name, indicatorClass]) => (
          <div key={name} className="flex items-center gap-3 rounded-squircle-sm border border-border/70 bg-background/75 px-3 py-2.5">
            <span className={`h-2.5 w-2.5 rounded-full ${indicatorClass}`} />
            <span className="landing-body flex-1 text-sm text-foreground/80">{name}</span>
            <span className="h-5 w-9 rounded-full border border-border bg-card p-0.5">
              <span className="block h-3.5 w-3.5 rounded-full bg-muted-foreground/35" />
            </span>
          </div>
        ))}
      </div>
    </>
  );
}

function renderSchematicVisual(visual: SchematicFeatureVisual) {
  const content = (() => {
    switch (visual.variant) {
      case "split-view":
        return <SplitViewSchematic />;
      case "command-bar":
        return <CommandBarSchematic />;
      case "providers":
        return <ProvidersSchematic />;
    }
  })();

  return <SchematicFrame label={visual.label}>{content}</SchematicFrame>;
}

function FeatureVisualPanel({ visual }: { visual: FeatureVisual }) {
  switch (visual.type) {
    case "image":
      return renderImageVisual(visual);
    case "schematic":
      return renderSchematicVisual(visual);
    default:
      return null;
  }
}

export function FeaturesSection({ eyebrow, headline, items, textOnly = false }: FeaturesSectionProps) {
  if (textOnly) {
    return (
      <section className="section-rhythm pad-x-ladder border-t border-border bg-background text-foreground">
        <div className="container-marketing space-y-10 sm:space-y-14">
          <ScrollReveal variant="slide">
            <div className="max-w-3xl space-y-4">
              <p className="landing-eyebrow text-muted-foreground">{eyebrow}</p>
              <h2 className="landing-section-title">{headline}</h2>
            </div>
          </ScrollReveal>
          <div className="grid gap-6 sm:grid-cols-2 lg:grid-cols-3">
            {items.map((item, index) => (
              <ScrollReveal key={item.title} variant="fade" delayMs={index * 70} className="h-full">
                <article className="rounded-squircle-xl squircle-enhanced flex h-full flex-col overflow-hidden border border-border bg-card/70">
                  <div className="flex flex-1 flex-col gap-4 p-6 sm:p-7">
                    {item.eyebrow && <p className="landing-eyebrow text-muted-foreground">{item.eyebrow}</p>}
                    <h3 className="landing-subsection-title text-foreground">{item.title}</h3>
                    <p className="landing-body text-muted-foreground">{item.body}</p>
                  </div>
                </article>
              </ScrollReveal>
            ))}
          </div>
        </div>
      </section>
    );
  }

  return (
    <section className="section-rhythm pad-x-ladder border-t border-border bg-background text-foreground">
      <div className="container-marketing space-y-16 sm:space-y-24">
        <ScrollReveal variant="slide">
          <div className="max-w-3xl space-y-4">
            <p className="landing-eyebrow text-muted-foreground">{eyebrow}</p>
            <h2 className="landing-section-title">{headline}</h2>
          </div>
        </ScrollReveal>

        <div className="space-y-16 sm:space-y-24">
          {items.map((item, index) => {
            const reversed = index % 2 === 1;
            return (
              <ScrollReveal key={item.title} variant="fade" className="w-full">
                <article className="grid items-center gap-8 lg:grid-cols-2 lg:gap-14">
                  <div className={reversed ? "lg:order-2" : "lg:order-1"}>
                    {item.visual && <FeatureVisualPanel visual={item.visual} />}
                  </div>
                  <div className={`flex flex-col gap-4 ${reversed ? "lg:order-1" : "lg:order-2"}`}>
                    {item.eyebrow && <p className="landing-eyebrow text-muted-foreground">{item.eyebrow}</p>}
                    <h3 className="landing-section-title text-foreground">{item.title}</h3>
                    <p className="landing-body max-w-xl text-muted-foreground sm:text-lg">{item.body}</p>
                  </div>
                </article>
              </ScrollReveal>
            );
          })}
        </div>
      </div>
    </section>
  );
}

export default FeaturesSection;
