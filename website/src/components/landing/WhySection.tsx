import { ScrollReveal } from "../motion/ScrollReveal";

export type WhyConfig = {
  eyebrow: string;
  headline: string;
  body: string;
};

type WhySectionProps = {
  config: WhyConfig;
};

export function WhySection({ config }: WhySectionProps) {
  return (
    <section className="section-rhythm pad-x-ladder border-t border-border bg-background text-foreground">
      <div className="container-marketing">
        <ScrollReveal variant="slide">
          <div className="max-w-3xl space-y-4 sm:space-y-5">
            <p className="landing-eyebrow text-muted-foreground">{config.eyebrow}</p>
            <h2 className="landing-section-title">{config.headline}</h2>
            <p className="landing-body max-w-2xl text-muted-foreground sm:text-lg">{config.body}</p>
          </div>
        </ScrollReveal>
      </div>
    </section>
  );
}

export default WhySection;
