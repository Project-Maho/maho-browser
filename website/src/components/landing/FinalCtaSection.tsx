import { CtaButton } from "../marketing/CtaButton";
import { ScrollReveal } from "../motion/ScrollReveal";

export type FinalCtaConfig = {
  headline: string;
  subheadline: string;
  primaryCta: { label: string; href: string };
  secondaryCta?: {
    label: string;
    href: string;
    variant?: "primary" | "secondary" | "ghost" | "discord";
  };
};

type FinalCtaSectionProps = {
  config: FinalCtaConfig;
};

export function FinalCtaSection({ config }: FinalCtaSectionProps) {
  return (
    <section className="section-rhythm pad-x-ladder border-t border-border bg-background text-foreground">
      <div className="container-marketing">
        <ScrollReveal variant="fade">
          <div className="rounded-squircle-xl squircle-enhanced border border-border bg-card/70 px-6 py-12 text-center sm:px-10 sm:py-16 lg:px-16">
            <div className="mx-auto flex max-w-3xl flex-col items-center gap-5 sm:gap-6">
              <h2 className="landing-section-title">{config.headline}</h2>
              <p className="landing-body max-w-2xl text-muted-foreground sm:text-lg">{config.subheadline}</p>
              <div className="flex w-full flex-col items-center justify-center gap-3 sm:flex-row sm:flex-wrap">
                <CtaButton variant="primary" href={config.primaryCta.href}>
                  {config.primaryCta.label}
                </CtaButton>
                {config.secondaryCta && (
                  <CtaButton variant={config.secondaryCta.variant ?? "ghost"} href={config.secondaryCta.href}>
                    {config.secondaryCta.label}
                  </CtaButton>
                )}
              </div>
            </div>
          </div>
        </ScrollReveal>
      </div>
    </section>
  );
}

export default FinalCtaSection;
