type Product = "browser" | "mail";

type HeroConfig = {
  headline: string;
  subheadline: string;
  primaryCta: { label: string; href: string };
  secondaryCta: { label: string; href: string };
  heroImage?: string;
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
  price: string;
  suffix?: string;
  description: string;
  features: string[];
};

type PricingConfig = {
  badge: string;
  headline: string;
  body: string;
  tiers: PricingTier[];
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

const browserReleaseUrl = "https://github.com/nicepkg/maho/releases";
const browserGithubUrl = "https://github.com/nicepkg/maho";
const mailReleaseUrl = "https://github.com/aspect-apps/maho-mail/releases";
const mailGithubUrl = "https://github.com/aspect-apps/maho-mail";

const content: Record<Product, ProductContent> = {
  browser: {
    hero: {
      headline: "The browser that\nworks like you do.",
      subheadline:
        "Spaces for every project. AI built in. Split view, notes, and boosts — with sync across every device. Available on macOS, iOS, and Android.",
      primaryCta: { label: "Download for Mac", href: browserReleaseUrl },
      secondaryCta: { label: "View on GitHub", href: browserGithubUrl },
      heroImage: "/assets/images/sidebar-target-prototype-verified.png",
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
        title: "Cross-Device Sync",
        desc: "Your spaces, tabs, bookmarks, and notes — everywhere. Seamless sync across macOS, iOS, and Android.",
      },
      {
        title: "Extensions",
        desc: "Manifest V3 compatible. Install your favorite Chrome extensions and they just work.",
      },
    ],
    pricing: {
      badge: "Pricing",
      headline: "Free. Forever.",
      body: "Maho Browser is completely free and open source. Every feature, no limits. For AI features, bring your own API key or subscribe to our managed AI plan.",
      tiers: [
        {
          name: "Free",
          price: "$0",
          description: "Everything included. No limits.",
          features: [
            "All features unlocked",
            "Unlimited spaces and tabs",
            "Cross-device sync",
            "BYOK for AI (OpenAI, Anthropic, Ollama)",
          ],
        },
        {
          name: "AI Pro",
          price: "$5",
          suffix: "/mo",
          description: "Managed AI. No API key needed.",
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
      headline: "Browse without compromise.",
      subheadline: "Download Maho. It's free, private, and open source.",
      cta: { label: "Download for Mac", href: browserReleaseUrl },
    },
  },
  mail: {
    hero: {
      headline: "Email for humans\nand machines.",
      subheadline:
        "A desktop email client with a CLI and MCP server. AI-powered, privacy-first, completely free and open source.",
      primaryCta: { label: "Download for Mac", href: mailReleaseUrl },
      secondaryCta: { label: "View on GitHub", href: mailGithubUrl },
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
        title: "Free & Open Source",
        desc: "MIT licensed. No paywalls, no feature gates. Optional AI subscription via OpenRouter for those who want a managed experience.",
      },
    ],
    pricing: {
      badge: "Pricing",
      headline: "Free. Forever.",
      body: "Maho Mail is completely free and open source. Every feature, no limits. For AI features, bring your own API key or subscribe to our managed AI plan through OpenRouter.",
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
      subheadline: "Download Maho Mail. It's free, private, and open source.",
      cta: { label: "Download for Mac", href: mailReleaseUrl },
    },
  },
};

function splitHeadline(headline: string) {
  return headline.split("\n");
}

function HeroSection({ config }: { config: HeroConfig }) {
  const lines = splitHeadline(config.headline);

  return (
    <section className="relative min-h-screen flex flex-col justify-center px-6 bg-background text-foreground pt-16">
      <div className={`max-w-6xl mx-auto w-full pt-24 ${config.heroImage ? 'grid lg:grid-cols-2 gap-12 items-center' : ''}`}>
        <div>
          <h1 className="heading-serif text-5xl sm:text-6xl lg:text-7xl xl:text-8xl leading-[1.1] mb-8">
            {lines.map((line, index) => (
              <span key={`${line}-${index}`}>
                {line}
                {index < lines.length - 1 && <br />}
              </span>
            ))}
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
            <a
              href={config.secondaryCta.href}
              className="inline-flex items-center gap-2 px-6 py-3 border border-border rounded-md text-sm font-medium text-muted-foreground hover:text-foreground hover:border-muted-foreground transition-colors"
            >
              {config.secondaryCta.label}
              <svg
                xmlns="http://www.w3.org/2000/svg"
                width="16"
                height="16"
                viewBox="0 0 24 24"
                fill="none"
                stroke="currentColor"
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
        </div>
        {config.heroImage && (
          <div className="relative rounded-lg overflow-hidden border border-border shadow-2xl bg-card hidden lg:block">
            <img src={config.heroImage} alt="Maho Product Screenshot" className="w-full h-auto object-cover" />
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
          <h2 className="heading-serif text-4xl sm:text-5xl mb-6">
            {lines.map((line, index) => (
              <span key={`${line}-${index}`}>
                {line}
                {index < lines.length - 1 && <br />}
              </span>
            ))}
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
        <div className="grid grid-cols-1 sm:grid-cols-2 gap-px bg-border max-w-3xl">
          {config.tiers.map((tier) => (
            <div key={tier.name} className="bg-background p-10">
              <h3 className="text-foreground font-medium text-lg mb-2">{tier.name}</h3>
              <p className="heading-serif text-4xl text-foreground mb-4">
                {tier.price}
                {tier.suffix && <span className="text-lg text-muted-foreground">{tier.suffix}</span>}
              </p>
              <p className="text-muted-foreground text-sm leading-relaxed mb-6">
                {tier.description}
              </p>
              <ul className="space-y-2 text-sm text-muted-foreground">
                {tier.features.map((feature) => (
                  <li key={feature}>✓ {feature}</li>
                ))}
              </ul>
            </div>
          ))}
        </div>
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
