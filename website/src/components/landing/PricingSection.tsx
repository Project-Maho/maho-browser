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
  badge: string;
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
    <section className="py-24 px-6 border-t border-border">
      <div className="max-w-6xl mx-auto">
        <span className="inline-block border border-border text-muted-foreground text-xs px-3 py-1 rounded-full mb-6">
          {config.badge}
        </span>
        <h2 className="heading-serif text-4xl sm:text-5xl mb-6">{config.headline}</h2>
        <p className="text-muted-foreground leading-relaxed text-lg max-w-3xl mb-16">{config.body}</p>
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-px bg-border max-w-5xl">
          {config.tiers.map((tier) => {
            const isFeatured = tier.badge === "Popular";
            return (
              <div key={tier.name} className={`bg-background p-10 ${isFeatured ? "ring-2 ring-foreground ring-inset" : ""}`}>
                <div className="flex items-center gap-2 mb-2">
                  <h3 className="text-foreground font-medium text-lg">{tier.name}</h3>
                  {tier.badge && (
                    <span className="inline-block rounded-full bg-foreground px-3 py-1 text-xs font-medium uppercase tracking-[0.18em] text-background">
                      {tier.badge}
                    </span>
                  )}
                </div>
                <p className="heading-serif text-4xl text-foreground mb-4">
                  {tier.price}
                  {tier.suffix && <span className="text-lg text-muted-foreground">{tier.suffix}</span>}
                </p>
                <p className="text-muted-foreground text-sm leading-relaxed mb-6">{tier.description}</p>
                <ul className="space-y-2 text-sm text-muted-foreground mb-6">
                  {tier.features.map((feature) => (
                    <li key={feature}>✓ {feature}</li>
                  ))}
                </ul>
                {tier.cta && (
                  <a
                    href={tier.cta.href}
                    className={`inline-flex items-center gap-2 px-6 py-3 rounded-md text-sm font-medium transition-colors ${isFeatured ? "bg-foreground text-background hover:opacity-90" : "border border-border text-foreground hover:border-foreground"}`}
                  >
                    {tier.cta.label}
                  </a>
                )}
              </div>
            );
          })}
        </div>
        {config.footerNote && (
          <p className="text-muted-foreground text-sm leading-relaxed mt-6 max-w-3xl">
            {config.footerNote} <a href="/about" className="underline hover:text-foreground transition-colors">How Maho stays funded</a>.
          </p>
        )}
      </div>
    </section>
  );
}

export default PricingSection;
