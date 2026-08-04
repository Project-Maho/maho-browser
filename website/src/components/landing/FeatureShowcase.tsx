import React from "react";
import { FeatureShowcaseFallback } from "./feature-showcase/FeatureShowcaseFallback";
import { ShowcaseLiveDesktop } from "./feature-showcase/ShowcaseLiveDesktop";
import { useResponsiveBranch } from "./feature-showcase/useResponsiveBranch";
import type { FeatureItem } from "./FeaturesSection";

export interface FeatureShowcaseProps {
  eyebrow?: string;
  headline?: string;
  items?: FeatureItem[];
}

export const FeatureShowcase: React.FC<FeatureShowcaseProps> = ({
  eyebrow = "EVERY FEATURE, READY TO USE",
  headline = "Built for modern web workflows",
  items,
}) => {
  const branch = useResponsiveBranch();

  return (
    <div data-feature-showcase-host="true">
      {branch === "fallback" ? (
        <FeatureShowcaseFallback eyebrow={eyebrow} headline={headline} items={items} />
      ) : (
        <ShowcaseLiveDesktop eyebrow={eyebrow} headline={headline} items={items} />
      )}
    </div>
  );
};
