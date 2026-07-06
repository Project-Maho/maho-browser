/** Optimized image data produced by Astro's getImage() and passed to React components. */
export type OptimizedImageData = {
  src: string;
  srcSet: string;
  width: number;
  height: number;
  alt?: string;
};
