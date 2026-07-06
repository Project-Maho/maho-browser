import { splitHeadline } from "./HeroSection";

export type WhyConfig = {
  badge: string;
  headline: string;
  body: string;
};

type WhySectionProps = {
  config: WhyConfig;
};

export function WhySection({ config }: WhySectionProps) {
  const lines = splitHeadline(config.headline);

  return (
    <section className="py-24 px-6 border-t border-border">
      <div className="max-w-6xl mx-auto">
        <div className="max-w-3xl">
          <span className="inline-block border border-border text-muted-foreground text-xs px-3 py-1 rounded-full mb-6">
            {config.badge}
          </span>
          <h2 className="heading-serif text-4xl sm:text-5xl mb-6 whitespace-pre-line">
            {lines.join("\n")}
          </h2>
          <p className="text-muted-foreground leading-relaxed text-lg">{config.body}</p>
        </div>
      </div>
    </section>
  );
}

export default WhySection;
