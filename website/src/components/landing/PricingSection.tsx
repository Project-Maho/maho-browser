import { CtaButton } from "../marketing/CtaButton";
import { ScrollReveal } from "../motion/ScrollReveal";

export type PricingTier = {
  name: string;
  badge?: string;
  price: string;
  suffix?: string;
  description: string;
  features: string[];
  cta?: { label: string; href: string };
};

export type PricingConfig = {
  eyebrow: string;
  headline: string;
  body: string;
  tiers: PricingTier[];
  footerNote?: string;
};

type PricingSectionProps = {
  config: PricingConfig;
};

export function PricingSection({ config }: PricingSectionProps) {
  return (
    <section className="section-rhythm pad-x-ladder border-t border-border bg-background text-foreground">
      <div className="container-marketing space-y-10 sm:space-y-14">
        <ScrollReveal variant="slide">
          <div className="max-w-3xl space-y-4">
            <p className="landing-eyebrow text-muted-foreground">{config.eyebrow}</p>
            <h2 className="landing-section-title">{config.headline}</h2>
            <p className="landing-body max-w-3xl text-muted-foreground sm:text-lg">{config.body}</p>
          </div>
        </ScrollReveal>

        <div className="grid gap-6 lg:grid-cols-3">
          {config.tiers.map((tier, index) => {
            const isFeatured = tier.badge?.toLowerCase() === "popular";

            return (
              <ScrollReveal key={tier.name} variant="fade" delayMs={index * 70} className="h-full">
                <article
                  className={`rounded-squircle-xl squircle-enhanced flex h-full min-h-[29rem] flex-col border p-6 sm:p-7 ${
                    isFeatured
                      ? "border-foreground/40 bg-card ring-1 ring-foreground/40"
                      : "border-border bg-card/70"
                  }`}
                >
                  <div className="space-y-4">
                    <div className="flex flex-wrap items-center gap-3">
                      <h3 className="landing-subsection-title text-foreground">{tier.name}</h3>
                      {tier.badge && (
                        <span className="landing-eyebrow rounded-full border border-foreground/20 bg-foreground px-3 py-1 text-[0.65rem] text-background">
                          {tier.badge}
                        </span>
                      )}
                    </div>

                    <p className="flex items-end gap-1 text-foreground">
                      <span className="landing-section-title text-[2.5rem] sm:text-[2.75rem]">{tier.price}</span>
                      {tier.suffix && <span className="landing-body pb-2 text-muted-foreground">{tier.suffix}</span>}
                    </p>

                    <p className="landing-body text-muted-foreground">{tier.description}</p>
                  </div>

                  <ul className="mt-8 flex flex-1 flex-col gap-3">
                    {tier.features.map((feature) => (
                      <li key={feature} className="landing-body flex items-start gap-3 text-muted-foreground">
                        <span className="mt-2 h-1.5 w-1.5 shrink-0 rounded-full bg-foreground" aria-hidden="true" />
                        <span>{feature}</span>
                      </li>
                    ))}
                  </ul>

                  {tier.cta && (
                    <div className="mt-8">
                      <CtaButton variant={isFeatured ? "primary" : "ghost"} href={tier.cta.href} className="w-full justify-center">
                        {tier.cta.label}
                      </CtaButton>
                    </div>
                  )}
                </article>
              </ScrollReveal>
            );
          })}
        </div>

        {config.footerNote && (
          <ScrollReveal variant="fade">
            <p className="landing-body max-w-3xl text-sm text-muted-foreground sm:text-base">{config.footerNote}</p>
          </ScrollReveal>
        )}
      </div>
    </section>
  );
}

export default PricingSection;
