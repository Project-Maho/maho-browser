import React from "react";
import spacesImage from "../../../assets/images/maho-feature-spaces.png";
import splitViewImage from "../../../assets/images/maho-feature-split-view.png";
import commandBarImage from "../../../assets/images/maho-feature-command-bar.png";
import agentImage from "../../../assets/images/maho-feature-agent.png";
import {
  monetWallpaperClassName,
  monetWallpaperStyle,
} from "./monetWallpaper";
import type { FeatureItem } from "../FeaturesSection";

export interface FeatureShowcaseFallbackProps {
  eyebrow?: string;
  headline?: string;
  items?: FeatureItem[];
}

export const FeatureShowcaseFallback: React.FC<FeatureShowcaseFallbackProps> = ({
  eyebrow = "Maho Browser Workflow",
  headline = "Real tools, not talking points.",
  items,
}) => {
  const defaultFeatures = [
    {
      title: "Spaces",
      badge: "Spaces",
      desc: "Persistent workspaces in the sidebar. Group tabs by project, client, or context. Switch instantly with ⌃1–9. Pick up where you left off.",
      img: spacesImage.src,
      alt: "Maho Spaces feature interface",
      width: spacesImage.width,
      height: spacesImage.height,
      srcSet: undefined as string | undefined,
    },
    {
      title: "Split View",
      badge: "Split View",
      desc: "Two pages side by side. Compare, reference, and multitask without switching tabs.",
      img: splitViewImage.src,
      alt: "Maho Split View feature interface",
      width: splitViewImage.width,
      height: splitViewImage.height,
      srcSet: undefined as string | undefined,
    },
    {
      title: "Command Bar",
      badge: "Command Bar",
      desc: "One shortcut for everything. Navigate, search open tabs, run actions, jump between Spaces. Keyboard-first from the start.",
      img: commandBarImage.src,
      alt: "Maho Command Bar feature interface",
      width: commandBarImage.width,
      height: commandBarImage.height,
      srcSet: undefined as string | undefined,
    },
    {
      title: "Agent",
      badge: "Agent",
      desc: "Analyze the current page when you ask — user-started, with your own key or local models, and always under your control. Agent never clicks, purchases, submits forms, or uses tools on its own.",
      img: agentImage.src,
      alt: "Maho Browser Agent side panel analyzing the current page after the user asked",
      width: agentImage.width,
      height: agentImage.height,
      srcSet: undefined as string | undefined,
    },
  ];

  const displayFeatures =
    items && items.length > 0
      ? items.map((item, idx) => {
          const fallback = defaultFeatures[idx] || defaultFeatures[0];
          const hasImage = item.visual?.type === "image" && Boolean(item.visual.image);
          const imgVisual = hasImage && item.visual?.type === "image" ? item.visual : null;
          const imgObj = imgVisual?.image;

          return {
            title: item.title,
            badge: fallback.badge,
            desc: item.body || fallback.desc,
            img: imgObj ? imgObj.src : fallback.img,
            srcSet: imgObj?.srcSet || undefined,
            alt: imgVisual?.alt || fallback.alt,
            width: imgObj ? imgObj.width : fallback.width,
            height: imgObj ? imgObj.height : fallback.height,
          };
        })
      : defaultFeatures;

  return (
    <div data-showcase="fallback" className="w-full max-w-7xl mx-auto px-4 py-16 flex flex-col gap-16">
      <div className="text-center max-w-3xl mx-auto space-y-4">
        <p className="landing-eyebrow text-muted-foreground">
          {eyebrow}
        </p>
        <h2 className="landing-section-title">
          {headline}
        </h2>
      </div>

      {/* Zig-Zag Feature Articles List */}
      <div className="flex flex-col gap-16">
        {displayFeatures.map((f, idx) => {
          const isEven = idx % 2 === 0;
          return (
            <article
              key={f.title}
              className={`flex flex-col ${
                isEven ? "md:flex-row" : "md:flex-row-reverse"
              } gap-8 items-center rounded-3xl p-8 bg-[#161219] border border-white/10 overflow-hidden shadow-xl`}
            >
              {/* Text Description Column */}
              <div className="flex-1 space-y-4">
                <div className="flex items-center gap-3">
                  <span className="landing-eyebrow rounded-squircle-sm border border-border bg-background/60 px-3 py-1 text-[0.65rem] text-muted-foreground">
                    {f.badge}
                  </span>
                  <span className="text-xs font-mono text-neutral-500">
                    0{idx + 1} / 04
                  </span>
                </div>
                <h3 className="text-2xl font-bold text-white tracking-tight">
                  {f.title}
                </h3>
                <p className="text-sm md:text-base text-neutral-300 leading-relaxed">
                  {f.desc}
                </p>
              </div>

              {/* Image Visual Column */}
              <div
                className={`flex-1 w-full rounded-2xl overflow-hidden border border-white/10 ${monetWallpaperClassName} p-3 shadow-2xl sm:p-4`}
                style={monetWallpaperStyle}
              >
                <div className="overflow-hidden rounded-xl">
                  <img
                    src={f.img}
                    srcSet={f.srcSet}
                    alt={f.alt}
                    width={f.width}
                    height={f.height}
                    loading="lazy"
                    decoding="async"
                    className="w-full h-auto object-cover block"
                  />
                </div>
              </div>
            </article>
          );
        })}
      </div>
    </div>
  );
};
