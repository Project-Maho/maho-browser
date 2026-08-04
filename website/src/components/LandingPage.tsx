import { FeaturesSection, type FeatureItem } from "./landing/FeaturesSection";
import { FinalCtaSection, type FinalCtaConfig } from "./landing/FinalCtaSection";
import { HeroSection, type HeroConfig } from "./landing/HeroSection";
import { PricingSection, type PricingConfig, type PricingTier } from "./landing/PricingSection";
import { WhySection, type WhyConfig } from "./landing/WhySection";
import { ImportSection, type ImportSectionConfig } from "./landing/ImportSection";
import { ChangelogSection, type ChangelogSectionConfig } from "./landing/ChangelogSection";
import type { OptimizedImageData } from "../types/images";

export type Product = "browser" | "mail";

export type FeatureGridImageKey =
  | "ai-when-asked"
  | "mini-maho"
  | "password-manager"
  | "content-blocker";

export type FeatureGridItem = {
  readonly label: string;
  readonly body: string;
  readonly image: FeatureGridImageKey;
  readonly alt: string;
};

export type FeatureGridSectionConfig = {
  readonly eyebrow: string;
  readonly headline: string;
  readonly items: readonly FeatureGridItem[];
};

export type ProtectionFeatureItem = {
  readonly label: string;
  readonly headline: string;
  readonly body: string;
  readonly image: "content-blocker" | "password-manager";
  readonly alt: string;
};

export type ProtectionFeaturesConfig = {
  readonly eyebrow: string;
  readonly headline: string;
  readonly items: readonly [ProtectionFeatureItem, ProtectionFeatureItem];
};

type BrowserProductContent = {
  hero: Omit<HeroConfig, "heroImage">;
  socialProof: {
    eyebrow: string;
    tickerItems: string[];
  };
  featureGrid: FeatureGridSectionConfig;
  protectionFeatures: ProtectionFeaturesConfig;
  features: {
    eyebrow: string;
    headline: string;
    items: FeatureItem[];
  };
  importSection: ImportSectionConfig;
  changelog: ChangelogSectionConfig;
  why: WhyConfig;
  pricing: PricingConfig & { tiers: PricingTier[] };
  finalCta: FinalCtaConfig;
};

type MailProductContent = {
  hero: Omit<HeroConfig, "heroImage">;
  why: WhyConfig;
  features: {
    eyebrow: string;
    headline: string;
    items: FeatureItem[];
  };
  pricing?: PricingConfig & { tiers: PricingTier[] };
  finalCta: FinalCtaConfig;
};

const browserDownloadUrl = "/download";
const mailDownloadUrl = "/download?product=mail";
const discordUrl = "https://discord.gg/Ty4J6pMdqg";

const content = {
  browser: {
    hero: {
      headline: "The browser tools you liked. Still shipping.",
      subheadline: "Spaces, split view, command bar — and your logins stay separate when you want them. Still yours, on a Chromium stack that keeps moving.",
      primaryCta: { label: "Download Maho", href: browserDownloadUrl },
      secondaryCta: { label: "Join our Discord", href: discordUrl, variant: "discord" },
      microCta: "Free. macOS, Windows, and Linux. Mobile in development.",
    },
    socialProof: {
      eyebrow: "By design",
      tickerItems: [
        "SPACES",
        "SPLIT VIEW",
        "COMMAND BAR",
        "PROFILES",
        "NO TELEMETRY",
      ],
    },
    featureGrid: {
      eyebrow: "Also inside",
      headline: "More of what makes Maho, Maho.",
      items: [
        {
          label: "AI when asked",
          body: "The agent reads the current page only when you ask. Bring your own key or run local models. It never clicks, buys, or submits on its own.",
          image: "ai-when-asked",
          alt: "Maho Browser Agent panel analyzing the current page after a user request.",
        },
        {
          label: "Mini Maho",
          body: "A lightweight window for quick links. Open something fast without disturbing your main workspace, then send it to a Space or let it go.",
          image: "mini-maho",
          alt: "Maho Browser window used as a temporary placeholder for the upcoming Mini Maho quick-window view.",
        },
      ],
    },
    protectionFeatures: {
      eyebrow: "Built in",
      headline: "Protection without extra extensions.",
      items: [
        {
          label: "Content blocker",
          headline: "The web, minus the noise.",
          body: "A built-in content blocker for ads and trackers, with customizable EasyList-format filter lists.",
          image: "content-blocker",
          alt: "Ads and a cookie prompt load onto a web page, then Maho removes them and expands the article into the reclaimed space.",
        },
        {
          label: "Password manager",
          headline: "Your logins, ready when you need them.",
          body: "Save, autofill, and sync your logins securely with a password manager built directly into Maho.",
          image: "password-manager",
          alt: "Maho presents a saved login and securely autofills a sign-in form.",
        },
      ],
    },
    features: {
      eyebrow: "What's inside",
      headline: "Real tools, not talking points.",
      items: [
        {
          eyebrow: "Spaces",
          title: "Spaces",
          body: "Persistent workspaces in the sidebar. Group tabs by project, client, or context. Switch instantly with ⌃1–9. Pick up where you left off.",
          visual: {
            type: "image",
            alt: "Maho Browser Spaces sidebar with grouped workspaces for active projects and saved browsing context.",
            sizes: "(min-width: 1024px) 50vw, 100vw",
          },
        },
        {
          eyebrow: "Split View",
          title: "Split View",
          body: "Two pages side by side. Compare, reference, and multitask without switching tabs.",
          visual: {
            type: "image",
            alt: "Maho Browser Split View showing two web pages open side by side with the sidebar's split tabs.",
            sizes: "(min-width: 1024px) 50vw, 100vw",
          },
        },
        {
          eyebrow: "Command Bar",
          title: "Command Bar",
          body: "One shortcut for everything. Navigate, search open tabs, run actions, jump between Spaces. Keyboard-first from the start.",
          visual: {
            type: "image",
            alt: "Maho Browser command bar (⌘K) open over a web page, listing open tabs to switch to.",
            sizes: "(min-width: 1024px) 50vw, 100vw",
          },
        },
        {
          eyebrow: "Agent",
          title: "Agent",
          body: "Analyze the current page when you ask — user-started, with your own key or local models, and always under your control. Agent never clicks, purchases, submits forms, or uses tools on its own.",
          visual: {
            type: "image",
            alt: "Maho Browser Agent side panel analyzing the current page after the user asked, next to the live web page.",
            sizes: "(min-width: 1024px) 50vw, 100vw",
          },
        },
      ],
    },
    importSection: {
      eyebrow: "Migration",
      headline: "Bring your setup with you.",
      subheadline: "One-click import from Arc, Chrome, Safari (bookmarks & history), Firefox, Edge. Bookmarks, history, and passwords land where you expect them.",
      sources: [
        { name: "Arc", logo: "/assets/browser-logos/arc.svg" },
        { name: "Chrome", logo: "/assets/browser-logos/chrome.svg" },
        { name: "Safari", logo: "/assets/browser-logos/safari.svg" },
        { name: "Firefox", logo: "/assets/browser-logos/firefox.svg" },
        { name: "Edge", logo: "/assets/browser-logos/edge.svg" },
        { name: "Brave", logo: "/assets/browser-logos/brave.svg" },
      ],
      destinationLabel: "Maho",
    },
    changelog: {
      eyebrow: "In progress",
      headline: "Everything we're building, in public.",
      subheadline: "Follow the blog for feature notes and updates as Maho takes shape. No changelog theater — just what we're actually working on.",
      tilesRowOne: ["Spaces", "Split View", "Command Bar", "Sidebar", "Boosts", "Extensions"],
      tilesRowTwo: ["End-to-end Sync", "BYOK AI", "Privacy by default", "MCP tools", "CLI", "Keyboard-first"],
      primaryLink: { label: "Read the blog", href: "/blog" },
      rssLink: { label: "Subscribe to RSS", href: "/blog/rss.xml" },
    },
    why: {
      eyebrow: "Why Maho",
      headline: "The tools you liked, on a browser that keeps moving.",
      body: "You had a browser you liked: a sidebar, Spaces, split view, and a command bar that actually worked. Maho keeps that workflow moving on Chromium, with Profiles when you want separate logins.",
    },
    pricing: {
      eyebrow: "Pricing",
      headline: "Free. Forever.",
      body: "Start free. Move to Pro or Max for more managed usage. Add PAYG credits for spikes. Use BYOK when you want your own billing lane or frontier models.",
      tiers: [
        {
          name: "Free",
          price: "$0",
          description: "Try out Maho AI with free community models. $0.50/mo quota.",
          features: [
            "Free models only",
            "Basic chat",
            "BYOK supported alongside",
            "200 max tokens per call",
          ],
          cta: { label: "Start free", href: browserDownloadUrl },
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
          cta: { label: "Choose Pro", href: browserDownloadUrl },
        },
        {
          name: "Max",
          price: "$20",
          suffix: "/mo",
          description: "Heavy use — mid-tier models for daily-driver tasks, $13.30/mo budget.",
          features: [
            "+ Gemini Pro / Claude Sonnet / GPT-5 mini",
            "8000 max tokens per call",
            "$0.50 per-request cost cap",
            "Built for heavier daily usage",
          ],
          cta: { label: "Choose Max", href: browserDownloadUrl },
        },
      ],
      footerNote: "Also available: pay-as-you-go credits and free BYOK with your own OpenAI, Anthropic, Google, or OpenRouter key. See full pricing.",
    },
    finalCta: {
      headline: "The browser tools you liked. Still yours.",
      subheadline: "Download Maho for Spaces, split view, command bar, privacy by default, and AI only when asked. macOS, Windows, and Linux; mobile in development.",
      primaryCta: { label: "Download Maho", href: browserDownloadUrl },
      secondaryCta: { label: "Join our Discord", href: discordUrl, variant: "discord" },
    },
  },
  mail: {
    hero: {
      headline: "Email for humans\nand machines.",
      subheadline: "A desktop email client with a CLI and MCP server. AI-powered, privacy-first, free forever.",
      primaryCta: { label: "Download for macOS", href: mailDownloadUrl },
    },
    why: {
      eyebrow: "Why Maho Mail",
      headline: "Email clients made you choose.",
      body: "GUI or terminal. Pretty or programmable. Manual or automated. Every email client picks a side. Maho Mail gives you all three — a desktop app, a CLI, and an MCP server — sharing one local database.",
    },
    features: {
      eyebrow: "Features",
      headline: "Everything you need.",
      items: [
        { title: "Desktop App", body: "A clean GUI for daily email. Keyboard shortcuts, dark mode, smart inbox. Read, compose, and organize — fast." },
        { title: "CLI", body: "maho search, maho get, maho list. JSON output, Unix pipes. Script your inbox like any other tool in your stack." },
        { title: "MCP Server", body: "AI agents read, search, and draft email through stdio. Connect Claude, Cursor, or any MCP-compatible tool to your inbox." },
        { title: "AI Built In", body: "Summarize threads, draft replies, classify mail, translate messages. Bring your own API key — OpenAI, Anthropic, Ollama, or any OpenRouter model." },
        { title: "Local & Private", body: "Everything lives on your machine. No cloud sync, no telemetry, no data leaving your device. SQLCipher encrypted at rest." },
        { title: "Free Forever", body: "Free, forever. No paywalls, no feature gates. Optional AI subscription via OpenRouter for those who want managed inference." },
      ],
    },
    pricing: {
      eyebrow: "Pricing",
      headline: "Free. Forever.",
      body: "Maho Mail is free, forever. Every feature, no limits. For AI features, bring your own API key — or use our managed AI through OpenRouter for a single monthly price.",
      tiers: [
        {
          name: "Free",
          price: "$0",
          description: "Everything included. Bring your own API key for AI features.",
          features: [
            "Desktop app, CLI, and MCP server",
            "Unlimited accounts",
            "All features unlocked",
            "BYOK for AI with OpenAI, Anthropic, or Ollama",
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
      primaryCta: { label: "Download for macOS", href: mailDownloadUrl },
    },
  },
} satisfies {
  browser: BrowserProductContent;
  mail: MailProductContent;
};

export const landingContent = content;

export type LandingPageProps = {
  product?: Product;
  heroImage?: OptimizedImageData;
};

export function LandingPage({ product = "browser", heroImage }: LandingPageProps) {
  if (product === "mail") {
    const page = content.mail;
    const heroConfig: HeroConfig = {
      ...page.hero,
      heroImage,
    };

    return (
      <>
        <HeroSection config={heroConfig} />
        <WhySection config={page.why} />
        <FeaturesSection
          eyebrow={page.features.eyebrow}
          headline={page.features.headline}
          items={page.features.items}
          textOnly
        />
        {page.pricing && <PricingSection config={page.pricing} />}
        <FinalCtaSection config={page.finalCta} />
      </>
    );
  }

  const page = content.browser;
  const heroConfig: HeroConfig = {
    ...page.hero,
    heroImage,
  };

  return (
    <>
      <HeroSection config={heroConfig} />
      <FeaturesSection
        eyebrow={page.features.eyebrow}
        headline={page.features.headline}
        items={page.features.items}
      />
      <ImportSection config={page.importSection} />
      <ChangelogSection config={page.changelog} />
      <WhySection config={page.why} />
      <PricingSection config={page.pricing} />
      <FinalCtaSection config={page.finalCta} />
    </>
  );
}

export default LandingPage;
