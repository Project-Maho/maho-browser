export type FinalCtaConfig = {
  headline: string;
  subheadline: string;
  cta: { label: string; href: string };
};

type FinalCtaSectionProps = {
  config: FinalCtaConfig;
};

export function FinalCtaSection({ config }: FinalCtaSectionProps) {
  return (
    <section className="py-24 px-6 border-t border-border">
      <div className="max-w-6xl mx-auto text-center">
        <h2 className="heading-serif text-4xl sm:text-5xl mb-6">{config.headline}</h2>
        <p className="text-muted-foreground mb-8 max-w-xl mx-auto">{config.subheadline}</p>
        <a
          href={config.cta.href}
          className="inline-flex items-center gap-2 px-6 py-3 bg-foreground text-background rounded-md text-sm font-medium hover:opacity-90 transition-opacity"
        >
          {config.cta.label}
          <svg
            xmlns="http://www.w3.org/2000/svg"
            width="16"
            height="16"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            aria-hidden="true"
            strokeWidth="2"
            strokeLinecap="round"
            strokeLinejoin="round"
            className="w-4 h-4"
          >
            <path d="M5 12h14" />
            <path d="m12 5 7 7-7 7" />
          </svg>
        </a>
      </div>
    </section>
  );
}

export default FinalCtaSection;
