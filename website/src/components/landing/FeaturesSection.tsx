export type FeatureItem = {
  title: string;
  desc: string;
};

type FeaturesSectionProps = {
  badge: string;
  headline: string;
  items: FeatureItem[];
};

export function FeaturesSection({ badge, headline, items }: FeaturesSectionProps) {
  return (
    <section className="py-24 px-6 border-t border-border">
      <div className="max-w-6xl mx-auto">
        <span className="inline-block border border-border text-muted-foreground text-xs px-3 py-1 rounded-full mb-6">
          {badge}
        </span>
        <h2 className="heading-serif text-4xl sm:text-5xl mb-16">{headline}</h2>
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-px bg-border">
          {items.map((item, index) => (
            <div key={item.title} className="bg-background p-10">
              <span className="heading-serif text-4xl sm:text-5xl text-muted/50 block mb-6">
                {String(index + 1).padStart(2, "0")}
              </span>
              <h3 className="text-foreground font-medium text-lg mb-2">{item.title}</h3>
              <p className="text-muted-foreground text-sm leading-relaxed">{item.desc}</p>
            </div>
          ))}
        </div>
      </div>
    </section>
  );
}

export default FeaturesSection;
