import { describe, expect, it } from "vitest";
import {
  stripTrackers,
  KNOWN_TRACKER_DOMAINS,
} from "../trackerBlocker";

describe("KNOWN_TRACKER_DOMAINS", () => {
  it("should be a non-empty array of strings", () => {
    expect(Array.isArray(KNOWN_TRACKER_DOMAINS)).toBe(true);
    expect(KNOWN_TRACKER_DOMAINS.length).toBeGreaterThan(0);
    KNOWN_TRACKER_DOMAINS.forEach((domain) => {
      expect(typeof domain).toBe("string");
      expect(domain.length).toBeGreaterThan(0);
    });
  });

  it("should contain common email marketing domains", () => {
    expect(KNOWN_TRACKER_DOMAINS).toContain("mailchimp.com");
    expect(KNOWN_TRACKER_DOMAINS).toContain("sendgrid.net");
    expect(KNOWN_TRACKER_DOMAINS).toContain("hubspot.com");
  });
});

describe("stripTrackers", () => {
  const TRANSPARENT_PIXEL_SVG =
    "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='1' height='1'/%3E";

  function getImgSrc(html: string): string | null {
    const doc = new DOMParser().parseFromString(html, "text/html");
    return doc.querySelector("img")?.getAttribute("src") ?? null;
  }

  it("should return correct result shape", () => {
    const result = stripTrackers("<html></html>");
    expect(result).toHaveProperty("html");
    expect(result).toHaveProperty("trackersBlocked");
    expect(result).toHaveProperty("trackerDomains");
    expect(typeof result.html).toBe("string");
    expect(typeof result.trackersBlocked).toBe("number");
    expect(Array.isArray(result.trackerDomains)).toBe(true);
  });

  it("should handle empty HTML", () => {
    const result = stripTrackers("");
    expect(result.html).toBe("");
    expect(result.trackersBlocked).toBe(0);
    expect(result.trackerDomains).toEqual([]);
  });

  it("should handle HTML with no images", () => {
    const html = "<div><p>Hello world</p></div>";
    const result = stripTrackers(html);
    expect(result.html).toBe(html);
    expect(result.trackersBlocked).toBe(0);
    expect(result.trackerDomains).toEqual([]);
  });

  describe("1x1 pixel detection", () => {
    it("should replace img with width=1 height=1 attributes", () => {
      const html =
        '<img width="1" height="1" src="http://example.com/track.gif">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      expect(getImgSrc(result.html)).toBe(TRANSPARENT_PIXEL_SVG);
      expect(result.html).not.toContain("http://example.com/track.gif");
    });

    it("should replace img with width=0 height=0 attributes", () => {
      const html = '<img width="0" height="0" src="http://example.com/pixel">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      expect(getImgSrc(result.html)).toBe(TRANSPARENT_PIXEL_SVG);
    });

    it("should replace img with quoted 1x1 attributes", () => {
      const html = '<img width="1" height="1" src="http://example.com/track">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
    });
  });

  describe("inline style pixel detection", () => {
    it("should replace img with style width:1px;height:1px", () => {
      const html =
        '<img style="width:1px;height:1px" src="http://example.com/track.gif">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      expect(getImgSrc(result.html)).toBe(TRANSPARENT_PIXEL_SVG);
    });

    it("should replace img with style width:0px;height:0px", () => {
      const html =
        '<img style="width:0px;height:0px" src="http://example.com/pixel">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      expect(getImgSrc(result.html)).toBe(TRANSPARENT_PIXEL_SVG);
    });

    it("should replace img with style width:0;height:0 (no px)", () => {
      const html =
        '<img style="width:0;height:0" src="http://example.com/pixel">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
    });
  });

  describe("hidden image detection", () => {
    it("should replace img with display:none", () => {
      const html =
        '<img style="display:none" src="http://tracker.com/pixel.gif">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      expect(getImgSrc(result.html)).toBe(TRANSPARENT_PIXEL_SVG);
    });

    it("should replace img with visibility:hidden", () => {
      const html =
        '<img style="visibility:hidden" src="http://tracker.com/pixel.gif">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      expect(getImgSrc(result.html)).toBe(TRANSPARENT_PIXEL_SVG);
    });

    it("should replace img with opacity:0", () => {
      const html =
        '<img style="opacity:0" src="http://tracker.com/pixel.gif">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      expect(getImgSrc(result.html)).toBe(TRANSPARENT_PIXEL_SVG);
    });

    it("should replace img with position:absolute and tiny dimensions", () => {
      const html =
        '<img style="position:absolute;width:1px;height:1px" src="http://tracker.com/pixel.gif">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      expect(getImgSrc(result.html)).toBe(TRANSPARENT_PIXEL_SVG);
    });
  });

  describe("known tracker domain detection", () => {
    it("should replace img from known tracker domain (mailchimp)", () => {
      const html =
        '<img src="https://open.mailchimp.com/track/pixel.gif">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      expect(result.trackerDomains).toContain("open.mailchimp.com");
      expect(getImgSrc(result.html)).toBe(TRANSPARENT_PIXEL_SVG);
    });

    it("should replace img from known tracker subdomain (sendgrid)", () => {
      const html = '<img src="https://sub.sendgrid.net/open">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      expect(result.trackerDomains).toContain("sub.sendgrid.net");
    });

    it("should replace img from google-analytics domain", () => {
      const html =
        '<img src="https://www.google-analytics.com/collect?v=1">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      expect(result.trackerDomains).toContain("www.google-analytics.com");
    });
  });

  describe("tracking path pattern detection", () => {
    it("should detect /track pattern", () => {
      const html = '<img src="https://example.com/track?id=123">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
    });

    it("should detect /open? pattern", () => {
      const html = '<img src="https://example.com/open?msg=456">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
    });

    it("should detect /open. pattern", () => {
      const html = '<img src="https://example.com/open.gif">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
    });

    it("should detect /pixel pattern", () => {
      const html = '<img src="https://example.com/pixel.png">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
    });

    it("should detect /beacon pattern", () => {
      const html = '<img src="https://example.com/beacon.js">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
    });

    it("should detect /wf/open pattern", () => {
      const html = '<img src="https://example.com/wf/open">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
    });

    it("should detect /t.gif pattern", () => {
      const html = '<img src="https://example.com/t.gif">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
    });

    it("should detect /o.gif pattern", () => {
      const html = '<img src="https://example.com/o.gif">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
    });
  });

  describe("normal image preservation", () => {
    it("should NOT replace normal sized images", () => {
      const html =
        '<img src="https://example.com/photo.jpg" width="400" height="300">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(0);
      expect(result.html).toBe(html);
    });

    it("should NOT replace images without tracker indicators", () => {
      const html = '<img src="https://example.com/logo.png">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(0);
      expect(result.html).toBe(html);
    });

    it("should NOT replace images with only width attribute", () => {
      const html = '<img src="https://example.com/img.jpg" width="100">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(0);
    });
  });

  describe("multiple trackers", () => {
    it("should handle multiple trackers in same HTML", () => {
      const html = `
        <div>
          <img width="1" height="1" src="http://tracker1.com/pixel.gif">
          <img src="https://mailchimp.com/track/open">
          <img style="display:none" src="http://tracker2.com/beacon">
          <p>Some content</p>
        </div>
      `;
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(3);
      expect(result.trackerDomains.length).toBeGreaterThanOrEqual(1);
    });

    it("should collect all unique tracker domains", () => {
      const html = `
        <img src="https://mailchimp.com/track">
        <img src="https://sendgrid.net/open">
        <img src="https://mailchimp.com/track">
      `;
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(3);
      expect(result.trackerDomains.length).toBe(2);
    });
  });

  describe("CSS background-image tracker detection", () => {
    it("should replace CSS background-image from tracker domain", () => {
      const html = `
        <div style="background-image: url('https://mailchimp.com/track/open')">
          Content
        </div>
      `;
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      const div = doc.querySelector("div");
      expect(div?.style.backgroundImage).toBe("none");
      expect(result.html).not.toContain("mailchimp.com");
    });

    it("should replace CSS background from tracker domain", () => {
      const html = `
        <div style="background: url('https://sendgrid.net/pixel')">
          Content
        </div>
      `;
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      const div = doc.querySelector("div");
      expect(div?.style.background).toContain("none");
    });

    it("should NOT replace CSS background-image from non-tracker domain", () => {
      const html = `
        <div style="background-image: url('https://example.com/bg.jpg')">
          Content
        </div>
      `;
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(0);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      const div = doc.querySelector("div");
      expect(div?.style.backgroundImage).toContain("example.com/bg.jpg");
    });

    it("should handle double quotes in CSS url", () => {
      const html = `
        <div style='background-image: url("https://mailchimp.com/track")'>
          Content
        </div>
      `;
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
    });

    it("should handle unquoted CSS url", () => {
      const html = `
        <div style="background-image: url(https://mailchimp.com/track)">
          Content
        </div>
      `;
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
    });
  });

  describe("edge cases", () => {
    it("should handle image without src attribute", () => {
      const html = '<img width="1" height="1" alt="tracker">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      expect(getImgSrc(result.html)).toBe(TRANSPARENT_PIXEL_SVG);
    });

    it("should handle self-closing img tags", () => {
      const html = '<img width="1" height="1" src="http://example.com/pixel" />';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
    });

    it("should handle uppercase IMG tags", () => {
      const html =
        '<IMG WIDTH="1" HEIGHT="1" SRC="http://example.com/track.gif">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
    });

    it("should handle mixed case attributes", () => {
      const html =
        '<img Width="1" Height="1" Src="http://example.com/track.gif">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
    });

    it("should handle tracker with both pixel size and tracker domain", () => {
      const html =
        '<img width="1" height="1" src="https://mailchimp.com/pixel">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      expect(result.trackerDomains).toContain("mailchimp.com");
    });

    it("should handle tracker with hidden style and tracker domain", () => {
      const html =
        '<img style="display:none" src="https://sendgrid.net/track">';
      const result = stripTrackers(html);
      expect(result.trackersBlocked).toBe(1);
      expect(result.trackerDomains).toContain("sendgrid.net");
    });

    it("should preserve other attributes when replacing src", () => {
      const html =
        '<img width="1" height="1" alt="Tracking pixel" class="tracker" src="http://example.com/pixel.gif">';
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      const img = doc.querySelector("img");
      expect(img?.getAttribute("alt")).toBe("Tracking pixel");
      expect(img?.getAttribute("class")).toBe("tracker");
      expect(img?.getAttribute("width")).toBe("1");
      expect(img?.getAttribute("height")).toBe("1");
      expect(img?.getAttribute("src")).toBe(TRANSPARENT_PIXEL_SVG);
    });
  });

  describe("new bypass vectors (srcset, picture, video, object, svg image)", () => {
    it("should block external resources in srcset attribute", () => {
      const html = '<img srcset="https://mailchimp.com/p.png 1x, https://mailchimp.com/p2.png 2x" src="https://mailchimp.com/p.png">';
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      const img = doc.querySelector("img");
      expect(img?.getAttribute("src")).toBe(TRANSPARENT_PIXEL_SVG);
      expect(img?.getAttribute("srcset")).toContain(TRANSPARENT_PIXEL_SVG);
    });

    it("should block external resources in picture source tags", () => {
      const html = `
        <picture>
          <source srcset="https://sendgrid.net/wf/open?u=123" media="(min-width: 800px)">
          <img src="https://sendgrid.net/wf/open?u=123">
        </picture>
      `;
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      const source = doc.querySelector("source");
      expect(source?.getAttribute("srcset")).toContain(TRANSPARENT_PIXEL_SVG);
    });

    it("should block video poster urls pointing to tracker domains", () => {
      const html = '<video poster="https://mailchimp.com/track/open" src="https://example.com/movie.mp4"></video>';
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      const video = doc.querySelector("video");
      expect(video?.getAttribute("poster")).toBe(TRANSPARENT_PIXEL_SVG);
    });

    it("should block object tag data pointing to tracker domains", () => {
      const html = '<object data="https://mailchimp.com/track/open" type="image/svg+xml"></object>';
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      const obj = doc.querySelector("object");
      expect(obj?.hasAttribute("data")).toBe(false);
    });

    it("should block SVG image tags pointing to tracker domains", () => {
      const html = `
        <svg>
          <image href="https://mailchimp.com/track/open"></image>
        </svg>
      `;
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      const image = doc.querySelector("image");
      expect(image?.getAttribute("href")).toBe(TRANSPARENT_PIXEL_SVG);
    });
  });

  describe("additional bypass vectors (iframe, embed, audio, link, use, form, @import)", () => {
    it("strips src from <iframe> pointing to a tracker domain", () => {
      const html = '<iframe src="https://mailchimp.com/track/open"></iframe>';
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      const iframe = doc.querySelector("iframe");
      expect(iframe?.hasAttribute("src")).toBe(false);
      expect(result.trackersBlocked).toBeGreaterThan(0);
    });

    it("strips src from <embed> pointing to a tracker domain", () => {
      const html = '<embed src="https://mailchimp.com/pixel.swf">';
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      expect(doc.querySelector("embed")?.hasAttribute("src")).toBe(false);
    });

    it("strips src from <audio>", () => {
      const html = '<audio src="https://mailchimp.com/track/audio.mp3"></audio>';
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      expect(doc.querySelector("audio")?.hasAttribute("src")).toBe(false);
    });

    it("strips src from <source> inside <audio>", () => {
      const html = `
        <audio>
          <source src="https://mailchimp.com/track/audio.mp3" type="audio/mpeg">
        </audio>
      `;
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      expect(doc.querySelector("source")?.hasAttribute("src")).toBe(false);
    });

    it("strips src from <source> inside <video>", () => {
      const html = `
        <video>
          <source src="https://mailchimp.com/track/clip.mp4" type="video/mp4">
        </video>
      `;
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      expect(doc.querySelector("source")?.hasAttribute("src")).toBe(false);
    });

    it("strips href from <link rel='preload' as='image'> tracker", () => {
      const html = '<html><head><link rel="preload" as="image" href="https://mailchimp.com/track/pixel.png"></head><body></body></html>';
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      expect(doc.querySelector("link")?.hasAttribute("href")).toBe(false);
    });

    it("strips href from <link rel='stylesheet'> tracker", () => {
      const html = '<html><head><link rel="stylesheet" href="https://mailchimp.com/track/styles.css"></head><body></body></html>';
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      expect(doc.querySelector("link")?.hasAttribute("href")).toBe(false);
    });

    it("strips href from <link rel='prefetch'>", () => {
      const html = '<html><head><link rel="prefetch" href="https://mailchimp.com/track/data.json"></head><body></body></html>';
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      expect(doc.querySelector("link")?.hasAttribute("href")).toBe(false);
    });

    it("strips href and xlink:href from SVG <use> referencing tracker", () => {
      const html = `
        <svg>
          <use href="https://mailchimp.com/track/sprite.svg#icon"></use>
        </svg>
      `;
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      const use = doc.querySelector("use");
      expect(use?.hasAttribute("href")).toBe(false);
      expect(use?.hasAttribute("xlink:href")).toBe(false);
    });

    it("strips href from SVG <feImage> referencing tracker", () => {
      const html = `
        <svg>
          <filter><feImage href="https://mailchimp.com/track/filter.png"/></filter>
        </svg>
      `;
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      const feImage = doc.querySelector("feImage, feimage");
      expect(feImage?.hasAttribute("href")).toBe(false);
    });

    it("strips form action pointing to external tracker domain", () => {
      const html = '<form action="https://mailchimp.com/track/submit" method="post"><input name="x"/></form>';
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      expect(doc.querySelector("form")?.hasAttribute("action")).toBe(false);
    });

    it("rewrites bare @import (no url() wrapper) inside <style>", () => {
      const html = `
        <body>
          <style>
            @import "https://mailchimp.com/track/styles.css";
            body { color: red; }
          </style>
        </body>
      `;
      const result = stripTrackers(html);
      expect(result.html).not.toContain("mailchimp.com");
    });

    it("rewrites @import url(\"...\") form inside <style>", () => {
      const html = `<body><style>@import url("https://mailchimp.com/track/styles.css");</style></body>`;
      const result = stripTrackers(html);
      expect(result.html).not.toContain("mailchimp.com");
    });

    it("does NOT strip same-origin / data: / cid: references", () => {
      const html = `
        <iframe src="cid:embedded-1"></iframe>
        <link rel="preload" as="image" href="data:image/png;base64,abc">
        <form action="/relative/submit"></form>
      `;
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      expect(doc.querySelector("iframe")?.getAttribute("src")).toBe("cid:embedded-1");
      expect(doc.querySelector("link")?.getAttribute("href")).toBe("data:image/png;base64,abc");
      expect(doc.querySelector("form")?.getAttribute("action")).toBe("/relative/submit");
    });
  });

  describe("unknown-domain non-image external resources default blocking (trackers-only mode)", () => {
    it("blocks unknown-domain link stylesheet in trackers-only mode", () => {
      const html = '<html><head><link rel="stylesheet" href="https://unknown-domain.com/style.css"></head><body></body></html>';
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      expect(doc.querySelector("link")?.hasAttribute("href")).toBe(false);
    });

    it("blocks unknown-domain audio src in trackers-only mode", () => {
      const html = '<audio src="https://unknown-domain.com/audio.mp3"></audio>';
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      expect(doc.querySelector("audio")?.hasAttribute("src")).toBe(false);
    });

    it("blocks unknown-domain video src and source src in trackers-only mode", () => {
      const html = `
        <video src="https://unknown-domain.com/video.mp4">
          <source src="https://unknown-domain.com/clip.mp4" type="video/mp4">
        </video>
      `;
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      expect(doc.querySelector("video")?.hasAttribute("src")).toBe(false);
      expect(doc.querySelector("source")?.hasAttribute("src")).toBe(false);
    });

    it("blocks unknown-domain embed and object in trackers-only mode", () => {
      const html = `
        <div>
          <embed src="https://unknown-domain.com/flash.swf">
          <object data="https://unknown-domain.com/data.obj"></object>
        </div>
      `;
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      expect(doc.querySelector("embed")?.hasAttribute("src")).toBe(false);
      expect(doc.querySelector("object")?.hasAttribute("data")).toBe(false);
    });

    it("blocks unknown-domain iframe and form action in trackers-only mode", () => {
      const html = `
        <div>
          <iframe src="https://unknown-domain.com/frame.html"></iframe>
          <form action="https://unknown-domain.com/submit"></form>
        </div>
      `;
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      expect(doc.querySelector("iframe")?.hasAttribute("src")).toBe(false);
      expect(doc.querySelector("form")?.hasAttribute("action")).toBe(false);
    });

    it("blocks unknown-domain @import in <style> in trackers-only mode", () => {
      const html = `
        <style>
          @import "https://unknown-domain.com/style.css";
          body { color: blue; }
        </style>
      `;
      const result = stripTrackers(html);
      expect(result.html).not.toContain("unknown-domain.com");
    });

    it("blocks unknown-domain SVG <use> and <feImage> in trackers-only mode", () => {
      const html = `
        <svg>
          <use href="https://unknown-domain.com/sprite.svg#icon"></use>
          <filter><feImage href="https://unknown-domain.com/filter.png"/></filter>
        </svg>
      `;
      const result = stripTrackers(html);
      const doc = new DOMParser().parseFromString(result.html, "text/html");
      expect(doc.querySelector("use")?.hasAttribute("href")).toBe(false);
      expect(doc.querySelector("feImage, feimage")?.hasAttribute("href")).toBe(false);
    });
  });
});
