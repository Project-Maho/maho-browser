type Product = "browser" | "mail";

type HeroConfig = {
  headline: string;
  subheadline: string;
  primaryCta: { label: string; href: string };
  heroImage?: string;
  microCta?: string;
};

type WhyConfig = {
  badge: string;
  headline: string;
  body: string;
};

type FeatureItem = {
  title: string;
  desc: string;
};

type PricingTier = {
  name: string;
  badge?: string;
  price: string;
  suffix?: string;
  description: string;
  features: string[];
  cta?: { label: string; href: string };
};

type PricingConfig = {
  badge: string;
  headline: string;
  body: string;
  tiers: PricingTier[];
  footerNote?: string;
};

type FinalCtaConfig = {
  headline: string;
  subheadline: string;
  cta: { label: string; href: string };
};

type ProductContent = {
  hero: HeroConfig;
  why: WhyConfig;
  featuresBadge: string;
  featuresHeadline: string;
  features: FeatureItem[];
  pricing?: PricingConfig;
  finalCta: FinalCtaConfig;
};

const browserDownloadUrl = "/download";
const mailDownloadUrl = "/download?product=mail";

const content: Record<Product, ProductContent> = {
  browser: {
    hero: {
      headline: "Your AI.\nYour data.\nYour browser.",
      subheadline:
        "Spaces, AI assistance, and end-to-end sync — without metered credits or surveillance. macOS today, more soon.",
      primaryCta: { label: "Download for macOS", href: browserDownloadUrl },
      heroImage: "/assets/images/sidebar-target-prototype-verified.png",
      microCta: "Free, macOS 12+. iPhone and Android in development.",
    },
    why: {
      badge: "Why Maho",
      headline: "Browsers weren't built\nfor how you work.",
      body: "Dozens of tabs. No context. Every browser treats tabs as a flat list. Maho organizes your web into Spaces — persistent workspaces that keep your projects, research, and routines separate and focused.",
    },
    featuresBadge: "Features",
    featuresHeadline: "Everything you need.",
    features: [
      {
        title: "Spaces",
        desc: "Persistent workspaces that group your tabs by project, client, or context. Switch between them instantly. Pick up where you left off.",
      },
      {
        title: "AI Built In",
        desc: "Summarize pages, clean up tab titles, auto-group related tabs, rename downloads. Bring your own API key — OpenAI, Anthropic, or Ollama.",
      },
      {
        title: "Split View",
        desc: "Browse two pages side by side. Compare, reference, and multitask without switching tabs.",
      },
      {
        title: "Boosts",
        desc: "Customize any website. Change fonts, hide distractions, inject CSS, tweak layouts. Per-site settings that persist.",
      },
      {
        title: "Sync",
        desc: "Keep your spaces, tabs, bookmarks, and notes on your Mac today. iPhone and Android sync are in development.",
      },
      {
        title: "Extensions",
        desc: "Manifest V3 compatible. Install your favorite Chrome extensions and they just work.",
      },
    ],
    pricing: {
      badge: "Pricing",
      headline: "Free. Forever.",
      body: "Maho Browser is free, forever. Every feature, no limits, no credits. For AI features, bring your own API key (OpenAI, Anthropic, Ollama) — or subscribe to managed AI for a single monthly price.",
      tiers: [
        {
          name: "Free",
          price: "$0",
          description: "Try Maho AI with free community models. $0.50/mo quota.",
          features: [
            "Free models only",
            "Basic chat",
            "BYOK supported alongside",
            "200 max tokens per call",
          ],
          cta: { label: "Get Maho", href: "/download" },
        },
        {
          name: "Pro",
          badge: "Popular",
          price: "$5",
          suffix: "/mo",
          description: "Daily AI for browsing — cheap-fast models, $2.10/mo budget.",
          features: [
            "Gemini Flash / Claude Haiku / Llama family",
            "2000 max tokens per call",
            "$0.20 per-request cost cap",
            "Fast enough for daily browsing work",
          ],
          cta: { label: "Get Maho", href: "/download" },
        },
        {
          name: "Max",
          price: "$20",
          suffix: "/mo",
          description: "Heavy use — mid-tier models, $13.30/mo budget.",
          features: [
            "+ Gemini Pro / Claude Sonnet / GPT-5 mini",
            "8000 max tokens per call",
            "$0.50 per-request cost cap",
            "Built for heavier daily usage",
          ],
          cta: { label: "Get Maho", href: "/download" },
        },
      ],
      footerNote:
        "Free is sustained by Pro and Max — managed AI for users who don't want to manage API keys. No ads, no data sales, no API-spend cut.",
    },
    finalCta: {
      headline: "No paywalls. No agents acting without you. No data harvest.",
      subheadline: "Download Maho. Free forever, privacy by default, your data on your device.",
      cta: { label: "Download for macOS", href: browserDownloadUrl },
    },
  },
  mail: {
    hero: {
      headline: "Email for humans\nand machines.",
      subheadline:
        "A desktop email client with a CLI and MCP server. AI-powered, privacy-first, free forever.",
      primaryCta: { label: "Download for macOS", href: mailDownloadUrl },
    },
    why: {
      badge: "Why Maho Mail",
      headline: "Email clients made\nyou choose.",
      body: "GUI or terminal. Pretty or programmable. Manual or automated. Every email client picks a side. Maho Mail gives you all three — a desktop app, a CLI, and an MCP server — sharing one local database.",
    },
    featuresBadge: "Features",
    featuresHeadline: "Everything you need.",
    features: [
      {
        title: "Desktop App",
        desc: "A clean GUI for daily email. Keyboard shortcuts, dark mode, smart inbox. Read, compose, and organize — fast.",
      },
      {
        title: "CLI",
        desc: "maho search, maho get, maho list. JSON output, Unix pipes. Script your inbox like any other tool in your stack.",
      },
      {
        title: "MCP Server",
        desc: "AI agents read, search, and draft email through stdio. Connect Claude, Cursor, or any MCP-compatible tool to your inbox.",
      },
      {
        title: "AI Built In",
        desc: "Summarize threads, draft replies, classify mail, translate messages. Bring your own API key — OpenAI, Anthropic, Ollama, or any OpenRouter model.",
      },
      {
        title: "Local & Private",
        desc: "Everything lives on your machine. No cloud sync, no telemetry, no data leaving your device. SQLCipher encrypted at rest.",
      },
      {
        title: "Free Forever",
        desc: "Free, forever. No paywalls, no feature gates. Optional AI subscription via OpenRouter for those who want managed inference.",
      },
    ],
    pricing: {
      badge: "Pricing",
      headline: "Free. Forever.",
      body: "Maho Mail is free, forever. Every feature, no limits. For AI features, bring your own API key — or use our managed AI through OpenRouter for a single monthly price.",
      tiers: [
        {
          name: "Free",
          price: "$0",
          description: "Everything included. Bring your own API key for AI features.",
          features: [
            "Desktop app, CLI, MCP server",
            "Unlimited accounts",
            "All features unlocked",
            "BYOK for AI (OpenAI, Anthropic, Ollama)",
          ],
        },
        {
          name: "AI Pro",
          price: "$5",
          suffix: "/mo",
          description: "Managed AI via OpenRouter. No API key needed.",
          features: [
            "Everything in Free",
            "Pre-configured AI models",
            "No API key setup required",
            "Supports Maho development",
          ],
        },
      ],
    },
    finalCta: {
      headline: "Make email programmable.",
      subheadline: "Download Maho Mail. Free forever, private, programmable.",
      cta: { label: "Download for macOS", href: mailDownloadUrl },
    },
  },
};

function splitHeadline(headline: string) {
  return headline.split("\n");
}

function HeroSection({ config }: { config: HeroConfig }) {
  const lines = splitHeadline(config.headline);

  return (
    <section className="relative flex flex-col justify-center px-6 bg-background text-foreground pt-20 sm:pt-16 pb-16 sm:pb-0 sm:min-h-screen">
      <div className={`max-w-6xl mx-auto w-full sm:pt-24 ${config.heroImage ? 'grid lg:grid-cols-2 gap-12 items-center' : ''}`}>
        <div>
          <h1 className="heading-serif text-5xl sm:text-6xl lg:text-7xl xl:text-8xl leading-[1.1] mb-8 whitespace-pre-line">
            {lines.join("\n")}
          </h1>
          <p className="text-lg sm:text-xl text-muted-foreground max-w-2xl mb-10 leading-relaxed">
            {config.subheadline}
          </p>
          <div className="flex flex-col sm:flex-row items-start sm:items-center gap-4">
            <a
              href={config.primaryCta.href}
              className="inline-flex items-center gap-2 px-6 py-3 bg-foreground text-background rounded-md text-sm font-medium hover:opacity-90 transition-opacity"
            >
              {config.primaryCta.label}
            </a>
          </div>
          {config.microCta && (
            <p className="text-muted-foreground text-sm leading-relaxed mt-4">{config.microCta}</p>
          )}
        </div>
        {config.heroImage && (
          <div className="relative rounded-lg overflow-hidden border border-border shadow-2xl bg-card hidden lg:block">
            <img src={config.heroImage} alt="Maho Browser interface showing a sidebar with multiple Spaces for organizing projects, AI conversation thread, and split-view tabs" className="w-full h-auto object-cover" />
          </div>
        )}
      </div>
    </section>
  );
}

function WhySection({ config }: { config: WhyConfig }) {
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
          <p className="text-muted-foreground leading-relaxed text-lg">
            {config.body}
          </p>
        </div>
      </div>
    </section>
  );
}

function FeaturesSection({
  badge,
  headline,
  items,
}: {
  badge: string;
  headline: string;
  items: FeatureItem[];
}) {
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

function PricingSection({ config }: { config: PricingConfig }) {
  return (
    <section className="py-24 px-6 border-t border-border">
      <div className="max-w-6xl mx-auto">
        <span className="inline-block border border-border text-muted-foreground text-xs px-3 py-1 rounded-full mb-6">
          {config.badge}
        </span>
        <h2 className="heading-serif text-4xl sm:text-5xl mb-6">{config.headline}</h2>
        <p className="text-muted-foreground leading-relaxed text-lg max-w-3xl mb-16">
          {config.body}
        </p>
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
                <p className="text-muted-foreground text-sm leading-relaxed mb-6">
                  {tier.description}
                </p>
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
          <p className="text-muted-foreground text-sm leading-relaxed mt-6 max-w-3xl">{config.footerNote} <a href="/about" className="underline hover:text-foreground transition-colors">How Maho stays funded</a>.</p>
        )}
      </div>
    </section>
  );
}

function FinalCtaSection({ config }: { config: FinalCtaConfig }) {
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

export function LandingPage({ product = "browser" }: { product?: Product }) {
  const page = content[product];

  return (
    <>
      <HeroSection config={page.hero} />
      <WhySection config={page.why} />
      <FeaturesSection
        badge={page.featuresBadge}
        headline={page.featuresHeadline}
        items={page.features}
      />
      {page.pricing && <PricingSection config={page.pricing} />}
      <FinalCtaSection config={page.finalCta} />
    </>
  );
}

export default LandingPage;
