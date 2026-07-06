import { ScrollReveal } from "../motion/ScrollReveal";
import type { OptimizedImageData } from "../../types/images";

type GalleryItem = {
  title: string;
  eyebrow: string;
  alt: string;
  image: OptimizedImageData;
  className?: string;
};

type ProductGallerySectionProps = {
  items: GalleryItem[];
};

export function ProductGallerySection({ items }: ProductGallerySectionProps) {
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
            {items.map((item, index) => (
              <ScrollReveal
                key={item.image.src}
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
                      src={item.image.src}
                      srcSet={item.image.srcSet || undefined}
                      sizes="(min-width: 1024px) 33vw, (min-width: 768px) 50vw, 100vw"
                      alt={item.alt}
                      width={item.image.width}
                      height={item.image.height}
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
