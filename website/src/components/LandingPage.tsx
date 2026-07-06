import { FeaturesSection, type FeatureItem } from "./landing/FeaturesSection";
import { FinalCtaSection, type FinalCtaConfig } from "./landing/FinalCtaSection";
import { HeroSection, type HeroConfig } from "./landing/HeroSection";
import { PricingSection, type PricingConfig, type PricingTier } from "./landing/PricingSection";
import { ProductGallerySection } from "./landing/ProductGallerySection";
import { WhySection, type WhyConfig } from "./landing/WhySection";
import type { OptimizedImageData } from "../types/images";

type Product = "browser" | "mail";

type ProductContent = {
  hero: Omit<HeroConfig, "heroImage">;
  why: WhyConfig;
  featuresBadge: string;
  featuresHeadline: string;
  features: FeatureItem[];
  pricing?: PricingConfig & { tiers: PricingTier[] };
  finalCta: FinalCtaConfig;
};

export type GalleryItemMeta = {
  title: string;
  eyebrow: string;
  alt: string;
  className?: string;
};

const browserDownloadUrl = "/download";
const mailDownloadUrl = "/download?product=mail";

const content: Record<Product, ProductContent> = {
  browser: {
    hero: { headline: "Your AI.\nYour data.\nYour browser.", subheadline: "Spaces, AI assistance, and end-to-end sync — without metered credits or surveillance. macOS today, more soon.", primaryCta: { label: "Download for macOS", href: browserDownloadUrl }, microCta: "Free, macOS 12+. iPhone and Android in development." },
    why: { badge: "Why Maho", headline: "Browsers weren't built\nfor how you work.", body: "Dozens of tabs. No context. Every browser treats tabs as a flat list. Maho organizes your web into Spaces — persistent workspaces that keep your projects, research, and routines separate and focused." },
    featuresBadge: "Features",
    featuresHeadline: "Everything you need.",
    features: [
      { title: "Spaces", desc: "Persistent workspaces that group your tabs by project, client, or context. Switch between them instantly. Pick up where you left off." },
      { title: "AI Built In", desc: "Summarize pages, clean up tab titles, auto-group related tabs, rename downloads. Bring your own API key — OpenAI, Anthropic, or Ollama." },
      { title: "Split View", desc: "Browse two pages side by side. Compare, reference, and multitask without switching tabs." },
      { title: "Boosts", desc: "Customize any website. Change fonts, hide distractions, inject CSS, tweak layouts. Per-site settings that persist." },
      { title: "Sync", desc: "Keep your spaces, tabs, bookmarks, and notes on your Mac today. iPhone and Android sync are in development." },
      { title: "Extensions", desc: "Manifest V3 compatible. Install your favorite Chrome extensions and they just work." },
    ],
    pricing: {
      badge: "Pricing",
      headline: "Free. Forever.",
      body: "Maho Browser is free, forever. Every feature, no limits, no credits. For AI features, bring your own API key (OpenAI, Anthropic, Ollama) — or subscribe to managed AI for a single monthly price.",
      tiers: [
        { name: "Free", price: "$0", description: "Try Maho AI with free community models. $0.50/mo quota.", features: ["Free models only", "Basic chat", "BYOK supported alongside", "200 max tokens per call"], cta: { label: "Get Maho", href: "/download" } },
        { name: "Pro", badge: "Popular", price: "$5", suffix: "/mo", description: "Daily AI for browsing — cheap-fast models, $2.10/mo budget.", features: ["Gemini Flash / Claude Haiku / Llama family", "2000 max tokens per call", "$0.20 per-request cost cap", "Fast enough for daily browsing work"], cta: { label: "Get Maho", href: "/download" } },
        { name: "Max", price: "$20", suffix: "/mo", description: "Heavy use — mid-tier models, $13.30/mo budget.", features: ["+ Gemini Pro / Claude Sonnet / GPT-5 mini", "8000 max tokens per call", "$0.50 per-request cost cap", "Built for heavier daily usage"], cta: { label: "Get Maho", href: "/download" } },
      ],
      footerNote: "Free is sustained by Pro and Max — managed AI for users who don't want to manage API keys. No ads, no data sales, no API-spend cut.",
    },
    finalCta: { headline: "No paywalls. No agents acting without you. No data harvest.", subheadline: "Download Maho. Free forever, privacy by default, your data on your device.", cta: { label: "Download for macOS", href: browserDownloadUrl } },
  },
  mail: {
    hero: { headline: "Email for humans\nand machines.", subheadline: "A desktop email client with a CLI and MCP server. AI-powered, privacy-first, free forever.", primaryCta: { label: "Download for macOS", href: mailDownloadUrl } },
    why: { badge: "Why Maho Mail", headline: "Email clients made\nyou choose.", body: "GUI or terminal. Pretty or programmable. Manual or automated. Every email client picks a side. Maho Mail gives you all three — a desktop app, a CLI, and an MCP server — sharing one local database." },
    featuresBadge: "Features",
    featuresHeadline: "Everything you need.",
    features: [
      { title: "Desktop App", desc: "A clean GUI for daily email. Keyboard shortcuts, dark mode, smart inbox. Read, compose, and organize — fast." },
      { title: "CLI", desc: "maho search, maho get, maho list. JSON output, Unix pipes. Script your inbox like any other tool in your stack." },
      { title: "MCP Server", desc: "AI agents read, search, and draft email through stdio. Connect Claude, Cursor, or any MCP-compatible tool to your inbox." },
      { title: "AI Built In", desc: "Summarize threads, draft replies, classify mail, translate messages. Bring your own API key — OpenAI, Anthropic, Ollama, or any OpenRouter model." },
      { title: "Local & Private", desc: "Everything lives on your machine. No cloud sync, no telemetry, no data leaving your device. SQLCipher encrypted at rest." },
      { title: "Free Forever", desc: "Free, forever. No paywalls, no feature gates. Optional AI subscription via OpenRouter for those who want managed inference." },
    ],
    pricing: {
      badge: "Pricing",
      headline: "Free. Forever.",
      body: "Maho Mail is free, forever. Every feature, no limits. For AI features, bring your own API key — or use our managed AI through OpenRouter for a single monthly price.",
      tiers: [
        { name: "Free", price: "$0", description: "Everything included. Bring your own API key for AI features.", features: ["Desktop app, CLI, MCP server", "Unlimited accounts", "All features unlocked", "BYOK for AI (OpenAI, Anthropic, Ollama)"] },
        { name: "AI Pro", price: "$5", suffix: "/mo", description: "Managed AI via OpenRouter. No API key needed.", features: ["Everything in Free", "Pre-configured AI models", "No API key setup required", "Supports Maho development"] },
      ],
    },
    finalCta: { headline: "Make email programmable.", subheadline: "Download Maho Mail. Free forever, private, programmable.", cta: { label: "Download for macOS", href: mailDownloadUrl } },
  },
};

/** Static gallery metadata (titles/alts). Images are injected by the parent .astro page. */
export const browserGalleryMeta: GalleryItemMeta[] = [
  { title: "Spaces sidebar", eyebrow: "Organize", alt: "Maho Browser showing a multi-surface spaces and favorites sidebar prototype across mobile and desktop layouts.", className: "md:col-span-2 lg:col-span-2" },
  { title: "Command bar", eyebrow: "Navigate", alt: "Maho Browser command bar showing URL input with contextual suggestions and tab actions." },
  { title: "AI actions", eyebrow: "Assist", alt: "Maho Browser command bar showing AI-oriented contextual suggestions and deep-link actions." },
  { title: "Built-in passwords", eyebrow: "Protect", alt: "Maho Browser settings showing the built-in password manager and provider controls.", className: "md:col-span-2 lg:col-span-2" },
];

export type LandingPageProps = {
  product?: Product;
  heroImage?: OptimizedImageData;
  galleryImages?: OptimizedImageData[];
};

export function LandingPage({ product = "browser", heroImage, galleryImages }: LandingPageProps) {
  const page = content[product];

  const heroConfig: HeroConfig = {
    ...page.hero,
    heroImage,
  };

  const galleryItems = product === "browser" && galleryImages
    ? browserGalleryMeta.map((meta, i) => ({
        ...meta,
        image: galleryImages[i],
      }))
    : undefined;

  return (
    <>
      <HeroSection config={heroConfig} />
      {product === "browser" && galleryItems && <ProductGallerySection items={galleryItems} />}
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
