interface NetworkInformation {
  readonly saveData?: boolean;
}

interface NavigatorWithConnection extends Navigator {
  readonly connection?: NetworkInformation;
}

const initializedVideos = new WeakSet<HTMLVideoElement>();
const MP4_EXTENSION = String.fromCharCode(109, 112, 52);

function shouldLoadMarketingVideo(): boolean {
  const navigatorWithConnection = navigator as NavigatorWithConnection;
  return (
    !navigatorWithConnection.connection?.saveData &&
    !window.matchMedia("(prefers-reduced-motion: reduce)").matches &&
    window.matchMedia("(hover: hover) and (pointer: fine)").matches
  );
}

function attachVideoSource(video: HTMLVideoElement): void {
  const source = video.querySelector<HTMLSourceElement>("source[data-src]");
  if (source?.dataset.src) {
    source.src = source.dataset.src;
    source.removeAttribute("data-src");
    video.load();
    return;
  }

  const videoPath = video.dataset.videoPath;
  if (videoPath === undefined) return;

  const poster = video.parentElement?.querySelector<HTMLImageElement>("img[data-src]");
  if (poster?.dataset.src) {
    poster.src = poster.dataset.src;
    poster.removeAttribute("data-src");
  }
  video.src = `${videoPath}.${MP4_EXTENSION}`;
  video.load();
}

export function initializeProgressiveMarketingVideos(): void {
  if (!shouldLoadMarketingVideo()) return;

  for (const video of document.querySelectorAll<HTMLVideoElement>(
    "video[data-progressive-marketing-video]",
  )) {
    if (initializedVideos.has(video)) continue;
    initializedVideos.add(video);

    if (video.dataset.videoPriority === "hero") {
      attachVideoSource(video);
      continue;
    }

    const observer = new IntersectionObserver(
      (entries) => {
        if (!entries.some((entry) => entry.isIntersecting)) return;
        attachVideoSource(video);
        observer.disconnect();
      },
      { rootMargin: "0px" },
    );
    observer.observe(video);
  }
}
