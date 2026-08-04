// Shared runtime contract for the Monet "Water Lilies" (1906) outer-frame
// treatment applied to every landing-page browser mockup. Keeping the source,
// the Tailwind cover/center classes, and the typed background style in one place
// guarantees every mockup mat renders from an identical contract.

export const MONET_WALLPAPER_SRC = "/demo/monet-water-lilies-1906.jpg" as const;

export const monetWallpaperClassName = "bg-cover bg-center" as const;

export const monetWallpaperStyle = {
  backgroundImage: `url('${MONET_WALLPAPER_SRC}')`,
} as const;
