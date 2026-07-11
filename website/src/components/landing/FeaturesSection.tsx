import { ScrollReveal } from "../motion/ScrollReveal";
import type { OptimizedImageData } from "../../types/images";

type ImageFeatureVisual = {
  type: "image";
  alt: string;
  sizes?: string;
  image?: OptimizedImageData;
};

type PlaceholderFeatureVisual = {
  type: "placeholder";
  label: string;
};

export type FeatureVisual = ImageFeatureVisual | PlaceholderFeatureVisual;

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

  return renderPlaceholderVisual({ type: "placeholder", label: "Screenshot forthcoming" });
}

function renderPlaceholderVisual(visual: PlaceholderFeatureVisual) {
  return (
    <div className="flex min-h-[15rem] items-center justify-center rounded-squircle-lg border border-dashed border-border/60 bg-background/40 p-6">
      <p className="landing-eyebrow text-muted-foreground/70">{visual.label}</p>
    </div>
  );
}

function FeatureVisualPanel({ visual }: { visual: FeatureVisual }) {
  switch (visual.type) {
    case "image":
      return renderImageVisual(visual);
    case "placeholder":
      return renderPlaceholderVisual(visual);
    default:
      return null;
  }
}

export function FeaturesSection({ eyebrow, headline, items, textOnly = false }: FeaturesSectionProps) {
  return (
    <section className="section-rhythm pad-x-ladder border-t border-border bg-background text-foreground">
      <div className="container-marketing space-y-10 sm:space-y-14">
        <ScrollReveal variant="slide">
          <div className="max-w-3xl space-y-4">
            <p className="landing-eyebrow text-muted-foreground">{eyebrow}</p>
            <h2 className="landing-section-title">{headline}</h2>
          </div>
        </ScrollReveal>

        <div className={`grid gap-6 ${textOnly ? "sm:grid-cols-2 lg:grid-cols-3" : "lg:grid-cols-2"}`}>
          {items.map((item, index) => (
            <ScrollReveal key={item.title} variant="fade" delayMs={index * 70} className="h-full">
              <article className="rounded-squircle-xl squircle-enhanced flex h-full flex-col overflow-hidden border border-border bg-card/70">
                {!textOnly && item.visual && (
                  <div className="border-b border-border bg-background/60 p-4 sm:p-5">
                    <FeatureVisualPanel visual={item.visual} />
                  </div>
                )}
                <div className="flex flex-1 flex-col gap-4 p-6 sm:p-7">
                  {item.eyebrow && (
                    <p className="landing-eyebrow text-muted-foreground">{item.eyebrow}</p>
                  )}
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

export default FeaturesSection;
