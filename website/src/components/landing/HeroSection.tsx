import { ScrollReveal } from "../motion/ScrollReveal";
import { CtaButton } from "../marketing/CtaButton";
import {
  monetWallpaperClassName,
  monetWallpaperStyle,
} from "./feature-showcase/monetWallpaper";
import type { OptimizedImageData } from "../../types/images";

export type HeroConfig = {
  headline: string;
  subheadline: string;
  primaryCta: { label: string; href: string };
  secondaryCta?: {
    label: string;
    href: string;
    variant?: "primary" | "secondary" | "ghost" | "discord";
  };
  heroImage?: OptimizedImageData;
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

  return (
    <section className="section-rhythm-tall surface-noisy relative overflow-hidden bg-background text-foreground">
      <div className="pad-x-ladder w-full">
        <div className="container-marketing flex flex-col items-center gap-12 pt-28 text-center sm:pt-32">
          <div className="flex max-w-3xl flex-col items-center gap-6 sm:gap-8">
            <ScrollReveal variant="slide">
              <h1 className="landing-display-hero whitespace-pre-line">
                {lines.join("\n")}
              </h1>
            </ScrollReveal>

            <ScrollReveal variant="slide" delayMs={70}>
              <p className="landing-body max-w-2xl text-lg text-muted-foreground sm:text-xl">
                {config.subheadline}
              </p>
            </ScrollReveal>

            <ScrollReveal variant="slide" delayMs={120}>
              <div className="flex flex-col items-center gap-3 sm:flex-row sm:flex-wrap sm:justify-center">
                <CtaButton variant="primary" href={config.primaryCta.href}>
                  {config.primaryCta.label}
                </CtaButton>
                {config.secondaryCta && (
                  <CtaButton variant="ghost" href={config.secondaryCta.href}>
                    {config.secondaryCta.label}
                  </CtaButton>
                )}
              </div>
            </ScrollReveal>

            {config.microCta && (
              <ScrollReveal variant="slide" delayMs={160}>
                <p className="landing-body text-sm text-muted-foreground sm:text-base">{config.microCta}</p>
              </ScrollReveal>
            )}
          </div>

          <ScrollReveal variant="fade" className="w-full">
            {config.heroImage ? (
              <div className="relative mx-auto w-full max-w-6xl">
                <div className="absolute inset-x-[10%] top-6 h-32 rounded-full bg-foreground/6 blur-3xl" aria-hidden="true" />
                <div
                  className={`relative overflow-hidden rounded-squircle-xl border border-border/80 ${monetWallpaperClassName} p-3 shadow-[0_1.5rem_4rem_rgba(0,0,0,0.28)] sm:p-5`}
                  style={monetWallpaperStyle}
                >
                  <div className="relative aspect-[1440/1118] overflow-hidden rounded-squircle-lg border border-border/70">
                    <img
                      src={config.heroImage.src}
                      srcSet={config.heroImage.srcSet || undefined}
                      sizes="(min-width: 1280px) 1152px, 100vw"
                      alt={config.heroImage.alt || "Maho Browser desktop screenshot showing a persistent sidebar, split panes, and a command workflow."}
                      width={config.heroImage.width}
                      height={config.heroImage.height}
                      loading="eager"
                      fetchPriority="high"
                      decoding="async"
                      className="absolute left-1/2 top-1/2 block h-auto w-[calc(100%*19/18)] max-w-none -translate-x-1/2 -translate-y-1/2"
                    />
                  </div>
                </div>
              </div>
            ) : (
              <div className="relative aspect-[16/9] w-full max-w-6xl overflow-hidden rounded-squircle-xl border border-border/60 bg-[radial-gradient(circle_at_top_right,rgba(250,250,250,0.12),transparent_50%),linear-gradient(180deg,rgba(255,255,255,0.04),rgba(255,255,255,0.01))]" />
            )}
          </ScrollReveal>
        </div>
      </div>
    </section>
  );
}

export default HeroSection;
