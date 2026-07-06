import { ScrollReveal } from "../motion/ScrollReveal";

type GalleryItem = {
  title: string;
  eyebrow: string;
  alt: string;
  src: string;
  width: number;
  height: number;
  className?: string;
};

const galleryItems: GalleryItem[] = [
  {
    title: "Spaces sidebar",
    eyebrow: "Organize",
    alt: "Maho Browser showing a multi-surface spaces and favorites sidebar prototype across mobile and desktop layouts.",
    src: "/assets/images/sidebar-target-prototype-final-verified.png",
    width: 1385,
    height: 1890,
    className: "md:col-span-2 lg:col-span-2",
  },
  {
    title: "Command bar",
    eyebrow: "Navigate",
    alt: "Maho Browser command bar showing URL input with contextual suggestions and tab actions.",
    src: "/assets/images/command-bar-mockup-final-qa.png",
    width: 1200,
    height: 1077,
  },
  {
    title: "AI actions",
    eyebrow: "Assist",
    alt: "Maho Browser command bar showing AI-oriented contextual suggestions and deep-link actions.",
    src: "/assets/images/pixel-perfect-mockup-qa.png",
    width: 1200,
    height: 1077,
  },
  {
    title: "Built-in passwords",
    eyebrow: "Protect",
    alt: "Maho Browser settings showing the built-in password manager and provider controls.",
    src: "/assets/images/maho-passwords-built-in.png",
    width: 1440,
    height: 1000,
    className: "md:col-span-2 lg:col-span-2",
  },
];

export function ProductGallerySection() {
  return (
    <section className="section-rhythm pad-x-ladder border-t border-border bg-background text-foreground">
      <ScrollReveal variant="slide">
        <div className="container-marketing space-y-10">
          <div className="max-w-2xl space-y-4">
            <p className="font-mono text-xs uppercase tracking-mono-loose text-muted-foreground">
              Product Gallery
            </p>
            <h2 className="heading-serif text-4xl sm:text-5xl">
              Real browser surfaces,
              <br />
              not vaporware mock copy.
            </h2>
            <p className="max-w-xl text-base leading-relaxed text-muted-foreground sm:text-lg">
              A quick look at the spaces, command workflows, AI shortcuts, and built-in protection already taking shape inside Maho Browser.
            </p>
          </div>

          <div className="grid grid-cols-1 gap-5 md:grid-cols-2 lg:grid-cols-3">
            {galleryItems.map((item, index) => (
              <ScrollReveal
                key={item.src}
                variant="fade"
                delayMs={index * 80}
                className={item.className}
              >
                <article className="rounded-squircle-lg squircle-enhanced motion-safe-scale h-full overflow-hidden border border-border bg-card p-4 sm:p-5">
                  <div className="mb-4 flex items-start justify-between gap-4">
                    <div className="space-y-2">
                      <p className="font-mono text-xs uppercase tracking-mono-loose text-muted-foreground">
                        {item.eyebrow}
                      </p>
                      <h3 className="text-lg font-medium text-foreground sm:text-xl">{item.title}</h3>
                    </div>
                    <span className="font-mono text-xs text-muted-foreground/70">
                      {String(index + 1).padStart(2, "0")}
                    </span>
                  </div>

                  <div className="rounded-squircle-md squircle-enhanced overflow-hidden border border-border/70 bg-background">
                    <img
                      src={item.src}
                      alt={item.alt}
                      width={item.width}
                      height={item.height}
                      loading="lazy"
                      decoding="async"
                      className="mask-fade-b block h-auto w-full"
                    />
                  </div>
                </article>
              </ScrollReveal>
            ))}
          </div>
        </div>
      </ScrollReveal>
    </section>
  );
}

export default ProductGallerySection;
