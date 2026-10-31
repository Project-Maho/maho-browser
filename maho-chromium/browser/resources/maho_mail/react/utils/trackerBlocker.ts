/**
 * Email tracker blocking utilities
 * Detects and strips tracking pixels and other tracking elements from email HTML
 */

export interface TrackerBlockResult {
  html: string;
  trackersBlocked: number;
  trackerDomains: string[];
}

// Known tracker domains list (~50 common ones)
export const KNOWN_TRACKER_DOMAINS: string[] = [
  // Email marketing
  'mailchimp.com', 'list-manage.com', 'mandrillapp.com',
  'sendgrid.net', 'sendgrid.com',
  'hubspot.com', 'hsforms.com', 'hs-analytics.net', 'hubapi.com',
  'mailgun.org', 'mailgun.com',
  'constantcontact.com', 'ctctcdn.com',
  'campaignmonitor.com', 'createsend.com',
  'getresponse.com',
  'klaviyo.com',
  'brevo.com', 'sendinblue.com',
  'activecampaign.com',
  'drip.com',
  'convertkit.com',
  'mailerlite.com',
  'aweber.com',
  'infusionsoft.com', 'keap.com',
  'emma.com', 'myemma.com',
  // Analytics / tracking
  'doubleclick.net',
  'google-analytics.com', 'googletagmanager.com',
  'facebook.com', 'facebook.net',
  'linkedin.com', 'licdn.com',
  'twitter.com', 't.co',
  'mixpanel.com',
  'segment.com', 'segment.io',
  'amplitude.com',
  'intercom.io',
  'drift.com',
  'hotjar.com',
  'fullstory.com',
  'heap.io', 'heapanalytics.com',
  // Email tracking services
  'returnpath.net',
  'litmus.com',
  'emailtracking.com',
  'yesware.com',
  'streak.com',
  'bananatag.com',
  'cirrusinsight.com',
  'getnotify.com',
  'mailtrack.io',
  'snov.io',
];

// 1x1 SVG data URI used as replacement for tracking images
const TRANSPARENT_PIXEL_SVG = "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='1' height='1'/%3E";

// Tracking patterns in URL paths
const TRACKING_PATH_PATTERNS = [
  /\/track/i,
  /\/open[?.]/i,
  /\/pixel/i,
  /\/beacon/i,
  /\/wf\/open/i,
  /\/t\.gif/i,
  /\/o\.gif/i,
];

/**
 * Extract domain from a URL string
 */
function extractDomain(url: string): string | null {
  try {
    const urlObj = new URL(url);
    return urlObj.hostname.toLowerCase();
  } catch {
    return null;
  }
}

/**
 * Check if a domain matches any known tracker domain
 */
function isKnownTrackerDomain(domain: string): boolean {
  const lowerDomain = domain.toLowerCase();
  return KNOWN_TRACKER_DOMAINS.some(trackerDomain =>
    lowerDomain === trackerDomain ||
    lowerDomain.endsWith('.' + trackerDomain)
  );
}

/**
 * Check if a URL contains tracking patterns in its path
 */
function hasTrackingPathPattern(url: string): boolean {
  try {
    const urlObj = new URL(url);
    const pathAndQuery = urlObj.pathname + urlObj.search;
    return TRACKING_PATH_PATTERNS.some(pattern => pattern.test(pathAndQuery));
  } catch {
    return false;
  }
}

/**
 * Check if an image element is a 1x1 tracking pixel
 */
function isOneByOnePixel(
  widthAttr: string | undefined,
  heightAttr: string | undefined,
  styleAttr: string | undefined,
): boolean {
  // Check width/height attributes
  if (widthAttr && heightAttr) {
    const w = widthAttr.trim();
    const h = heightAttr.trim();
    if ((w === '1' && h === '1') || (w === '0' && h === '0') ||
        (w === '"1"' && h === '"1"') || (w === "'1'" && h === "'1'") ||
        (w === '"0"' && h === '"0"') || (w === "'0'" && h === "'0'")) {
      return true;
    }
  }

  // Check inline styles for width:1px/height:1px or width:0/height:0
  if (styleAttr) {
    const style = styleAttr.toLowerCase();
    const hasOnePxWidth = /width\s*:\s*1px/.test(style);
    const hasOnePxHeight = /height\s*:\s*1px/.test(style);
    const hasZeroWidth = /width\s*:\s*0(?:px)?/.test(style);
    const hasZeroHeight = /height\s*:\s*0(?:px)?/.test(style);

    if ((hasOnePxWidth && hasOnePxHeight) || (hasZeroWidth && hasZeroHeight)) {
      return true;
    }
  }

  return false;
}

/**
 * Check if an image element has hidden styles
 */
function isHiddenImage(styleAttr: string | undefined): boolean {
  if (!styleAttr) return false;

  const style = styleAttr.toLowerCase();
  const isDisplayNone = /display\s*:\s*none/.test(style);
  const isVisibilityHidden = /visibility\s*:\s*hidden/.test(style);
  const isOpacityZero = /opacity\s*:\s*0(?:[;\s]|$)/.test(style);
  const isPositionAbsolute = /position\s*:\s*absolute/.test(style);
  const hasZeroOrOneDimensions = /width\s*:\s*(?:0|1)px?/.test(style) && /height\s*:\s*(?:0|1)px?/.test(style);

  return isDisplayNone || isVisibilityHidden || isOpacityZero ||
         (isPositionAbsolute && hasZeroOrOneDimensions);
}

export interface BlockExternalResourcesOptions {
  blockImages?: boolean;
  blockTrackers?: boolean;
}

export function blockExternalResources(
  html: string,
  options: BlockExternalResourcesOptions = { blockImages: true, blockTrackers: true }
): {
  html: string;
  trackersBlocked: number;
  trackerDomains: string[];
  imagesBlocked: number;
} {
  const parser = new DOMParser();
  const doc = parser.parseFromString(html, "text/html");

  let trackersBlockedCount = 0;
  let imagesBlockedCount = 0;
  const trackerDomainsSet = new Set<string>();

  const blockImages = options.blockImages ?? false;
  const blockTrackers = options.blockTrackers ?? false;

  function shouldBlockUrl(
    url: string | null | undefined,
    isImage: boolean = false,
  ): { block: boolean; isTracker: boolean; domain: string | null } {
    if (!url) return { block: false, isTracker: false, domain: null };
    const trimmed = url.trim();
    if (!/^https?:\/\//i.test(trimmed)) {
      return { block: false, isTracker: false, domain: null };
    }
    const domain = extractDomain(trimmed);
    let isTracker = false;
    if (blockTrackers) {
      if (domain && isKnownTrackerDomain(domain)) {
        isTracker = true;
      } else if (hasTrackingPathPattern(trimmed)) {
        isTracker = true;
      }
    }

    // Non-image external resources (<link>, <audio>, <video src>, <embed>, <object>, <iframe>, form action, etc.)
    // are default-BLOCKED regardless of tracker-list membership.
    // Image handling stays governed by user settings (blockImages / blockTrackers).
    const block = !isImage || isTracker || blockImages;
    return { block, isTracker, domain };
  }

  function processStyleString(style: string): { newStyle: string; blocked: boolean; isTracker: boolean; domains: string[] } {
    let blocked = false;
    let isTracker = false;
    const domains: string[] = [];

    let newStyle = style.replace(/url\(\s*(['"]?)([^)]*)\1\s*\)/gi, (match, _quote, url) => {
      const check = shouldBlockUrl(url, true);
      if (check.block) {
        blocked = true;
        if (check.isTracker) {
          isTracker = true;
          if (check.domain) domains.push(check.domain);
        }
        return "none";
      }
      return match;
    });

    newStyle = newStyle.replace(
      /@import\s+(['"])([^'"]+)\1(\s*[^;]*)?;?/gi,
      (match, _quote, url) => {
        const check = shouldBlockUrl(url, false);
        if (check.block) {
          blocked = true;
          if (check.isTracker) {
            isTracker = true;
            if (check.domain) domains.push(check.domain);
          }
          return "";
        }
        return match;
      },
    );

    return { newStyle, blocked, isTracker, domains };
  }

  function recordBlock(check: { isTracker: boolean; domain: string | null }): void {
    if (check.isTracker) {
      trackersBlockedCount++;
      if (check.domain) trackerDomainsSet.add(check.domain);
    } else {
      imagesBlockedCount++;
    }
  }

  function blockUrlAttribute(
    el: Element,
    attributes: readonly string[],
    isImage: boolean = false,
  ): void {
    for (const attr of attributes) {
      const value = el.getAttribute(attr);
      if (!value) continue;
      const check = shouldBlockUrl(value, isImage);
      if (check.block) {
        el.removeAttribute(attr);
        recordBlock(check);
      }
    }
  }

  const elements = doc.getElementsByTagName("*");
  for (let i = 0; i < elements.length; i++) {
    const el = elements[i];
    const tagName = el.tagName.toLowerCase();

    const styleAttr = el.getAttribute("style");
    if (styleAttr) {
      const { newStyle, blocked, isTracker, domains } = processStyleString(styleAttr);
      if (blocked) {
        el.setAttribute("style", newStyle);
        if (isTracker) {
          trackersBlockedCount++;
          domains.forEach(d => trackerDomainsSet.add(d));
        } else {
          imagesBlockedCount++;
        }
      }
    }

    if (tagName === "style") {
      const cssText = el.textContent ?? "";
      const { newStyle, blocked, isTracker, domains } = processStyleString(cssText);
      if (blocked) {
        el.textContent = newStyle;
        if (isTracker) {
          trackersBlockedCount++;
          domains.forEach(d => trackerDomainsSet.add(d));
        } else {
          imagesBlockedCount++;
        }
      }
    }

    if (tagName === "img") {
      const w = el.getAttribute("width") || undefined;
      const h = el.getAttribute("height") || undefined;
      const style = el.getAttribute("style") || undefined;
      const src = el.getAttribute("src");
      const srcset = el.getAttribute("srcset");

      let isPixelTracker = false;
      if (blockTrackers) {
        if (isOneByOnePixel(w, h, style) || isHiddenImage(style)) {
          isPixelTracker = true;
        }
      }

      if (src) {
        const check = shouldBlockUrl(src, true);
        if (check.block || isPixelTracker) {
          el.setAttribute("src", TRANSPARENT_PIXEL_SVG);
          if (check.isTracker || isPixelTracker) {
            trackersBlockedCount++;
            if (check.domain) trackerDomainsSet.add(check.domain);
          } else {
            imagesBlockedCount++;
          }
        }
      } else if (isPixelTracker) {
        el.setAttribute("src", TRANSPARENT_PIXEL_SVG);
        trackersBlockedCount++;
      }

      if (srcset) {
        const parts = srcset.split(",");
        let modifiedSrcset = false;
        const newParts = parts.map(part => {
          const match = part.trim().match(/^(\S+)(.*)$/);
          if (match) {
            const url = match[1];
            const descriptor = match[2];
            const check = shouldBlockUrl(url, true);
            if (check.block || isPixelTracker) {
              if (check.isTracker || isPixelTracker) {
                trackersBlockedCount++;
                if (check.domain) trackerDomainsSet.add(check.domain);
              } else {
                imagesBlockedCount++;
              }
              modifiedSrcset = true;
              return `${TRANSPARENT_PIXEL_SVG}${descriptor}`;
            }
          }
          return part;
        });
        if (modifiedSrcset) {
          el.setAttribute("srcset", newParts.join(", "));
        }
      }
    } else if (tagName === "source") {
      const srcset = el.getAttribute("srcset");
      if (srcset) {
        const parts = srcset.split(",");
        let modifiedSrcset = false;
        const newParts = parts.map(part => {
          const match = part.trim().match(/^(\S+)(.*)$/);
          if (match) {
            const url = match[1];
            const descriptor = match[2];
            const check = shouldBlockUrl(url, true);
            if (check.block) {
              if (check.isTracker) {
                trackersBlockedCount++;
                if (check.domain) trackerDomainsSet.add(check.domain);
              } else {
                imagesBlockedCount++;
              }
              modifiedSrcset = true;
              return `${TRANSPARENT_PIXEL_SVG}${descriptor}`;
            }
          }
          return part;
        });
        if (modifiedSrcset) {
          el.setAttribute("srcset", newParts.join(", "));
        }
      }
      blockUrlAttribute(el, ["src"], false);
    } else if (tagName === "video") {
      const poster = el.getAttribute("poster");
      if (poster) {
        const check = shouldBlockUrl(poster, true);
        if (check.block) {
          el.setAttribute("poster", TRANSPARENT_PIXEL_SVG);
          if (check.isTracker) {
            trackersBlockedCount++;
            if (check.domain) trackerDomainsSet.add(check.domain);
          } else {
            imagesBlockedCount++;
          }
        }
      }
      blockUrlAttribute(el, ["src"], false);
    } else if (tagName === "input") {
      if (el.getAttribute("type") === "image") {
        const src = el.getAttribute("src");
        if (src) {
          const check = shouldBlockUrl(src, true);
          if (check.block) {
            el.setAttribute("src", TRANSPARENT_PIXEL_SVG);
            if (check.isTracker) {
              trackersBlockedCount++;
              if (check.domain) trackerDomainsSet.add(check.domain);
            } else {
              imagesBlockedCount++;
            }
          }
        }
      }
    } else if (tagName === "object") {
      blockUrlAttribute(el, ["data"], false);
    } else if (tagName === "image") {
      const href = el.getAttribute("href") || el.getAttribute("xlink:href");
      if (href) {
        const check = shouldBlockUrl(href, true);
        if (check.block) {
          el.setAttribute("href", TRANSPARENT_PIXEL_SVG);
          el.removeAttribute("xlink:href");
          if (check.isTracker) {
            trackersBlockedCount++;
            if (check.domain) trackerDomainsSet.add(check.domain);
          } else {
            imagesBlockedCount++;
          }
        }
      }
    } else if (tagName === "iframe" || tagName === "embed" || tagName === "audio") {
      blockUrlAttribute(el, ["src"], false);
    } else if (tagName === "link") {
      blockUrlAttribute(el, ["href"], false);
    } else if (tagName === "use" || tagName === "feimage") {
      blockUrlAttribute(el, ["href", "xlink:href"], false);
    } else if (tagName === "form") {
      blockUrlAttribute(el, ["action"], false);
    }
  }

  const hasHtmlTag = /<html/i.test(html);
  const hasBodyTag = /<body/i.test(html);

  let finalHtml = "";
  if (hasHtmlTag) {
    finalHtml = "<!DOCTYPE html>\n" + doc.documentElement.outerHTML;
  } else if (hasBodyTag) {
    finalHtml = doc.body.outerHTML;
  } else {
    finalHtml = doc.body.innerHTML;
  }

  return {
    html: finalHtml,
    trackersBlocked: trackersBlockedCount,
    trackerDomains: Array.from(trackerDomainsSet),
    imagesBlocked: imagesBlockedCount
  };
}

export function stripTrackers(html: string): TrackerBlockResult {
  const result = blockExternalResources(html, { blockImages: false, blockTrackers: true });
  return {
    html: result.html,
    trackersBlocked: result.trackersBlocked,
    trackerDomains: result.trackerDomains
  };
}
