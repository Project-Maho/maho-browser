import { expect, test } from "@playwright/test";

const HERO_RECORDING = "/demo/hero/maho-hero-capture.mp4";
const HERO_POSTER = "/demo/hero/maho-hero-capture-poster.png";

test("homepage Hero mounts the user-recorded product demo", async ({ page }) => {
  await page.goto("/");

  const hero = page.locator('[data-motion-surface="hero"]');
  await expect(hero.locator("img")).toHaveAttribute("src", HERO_POSTER);

  const video = hero.locator("video");
  await expect(video).toHaveAttribute("src", HERO_RECORDING);
  await expect(video).toHaveAttribute("poster", HERO_POSTER);
  await expect(hero.getByRole("button", { name: "Spaces" })).toHaveCount(0);
  await expect(hero.getByRole("button", { name: "Split View" })).toHaveCount(0);
  await expect(hero.getByRole("button", { name: "Command Bar + AI" })).toHaveCount(0);
});
