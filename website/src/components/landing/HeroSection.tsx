import { ScrollReveal } from "../motion/ScrollReveal";
import { CtaButton } from "../marketing/CtaButton";

export type HeroConfig = {
  headline: string;
  subheadline: string;
  primaryCta: { label: string; href: string };
  heroImage?: string;
  microCta?: string;
};

export function splitHeadline(headline: string) {
  return headline.split("\n");
}

type HeroSectionProps = {
  config: HeroConfig;
};

export function HeroSection({ config }: HeroSectionProps) {
  const lines = splitHeadline(config.headline);
  const hasHeroImage = Boolean(config.heroImage);

  return (
    <section className="section-rhythm surface-noisy relative flex flex-col justify-center bg-background text-foreground sm:min-h-screen">
      <div className="pad-x-ladder w-full">
        <div className="mx-auto flex w-full max-w-6xl flex-col justify-center gap-12 sm:pt-24 lg:grid lg:grid-cols-2 lg:items-center">
          <div>
            <ScrollReveal variant="slide">
              <h1 className="heading-serif mb-8 whitespace-pre-line text-5xl leading-[1.1] sm:text-6xl lg:text-7xl xl:text-8xl">
                {lines.join("\n")}
              </h1>
            </ScrollReveal>
            <p className="mb-10 max-w-2xl text-lg leading-relaxed text-muted-foreground sm:text-xl">
              {config.subheadline}
            </p>
            <div className="flex flex-col items-start gap-4 sm:flex-row sm:items-center">
              <CtaButton variant="primary" href={config.primaryCta.href}>
                {config.primaryCta.label}
              </CtaButton>
            </div>
            {config.microCta && (
              <p className="mt-4 text-sm leading-relaxed text-muted-foreground">{config.microCta}</p>
            )}
          </div>
          {hasHeroImage && (
            <ScrollReveal variant="fade" className="hidden lg:block">
              <div className="motion-safe-scale relative overflow-hidden rounded-lg border border-border bg-card shadow-2xl">
                <div className="mask-fade-b">
                  <img
                    src={config.heroImage}
                    alt="Maho Browser interface showing a sidebar with multiple Spaces for organizing projects, AI conversation thread, and split-view tabs"
                    width={1200}
                    height={800}
                    className="w-full h-auto object-cover"
                  />
                </div>
              </div>
            </ScrollReveal>
          )}
          {!hasHeroImage && (
            <ScrollReveal variant="fade" className="hidden lg:block">
              <div className="relative aspect-[3/2] overflow-hidden rounded-squircle-lg border border-border/40 bg-background">
                <div
                  aria-hidden="true"
                  className="absolute inset-0 bg-[radial-gradient(circle_at_top_right,var(--card)_0%,transparent_62%)]"
                />
                <div aria-hidden="true" className="surface-noisy absolute inset-0 mask-fade-b" />
              </div>
            </ScrollReveal>
          )}
        </div>
      </div>
    </section>
  );
}

export default HeroSection;
