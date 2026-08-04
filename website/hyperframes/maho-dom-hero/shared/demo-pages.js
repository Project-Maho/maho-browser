import { getDemoPage } from "./split-data.js";

const OVERLAY_KINDS = Object.freeze(["none", "signup", "cookie", "both"]);

function element(tagName, className, text) {
  const node = document.createElement(tagName);
  if (className) node.className = className;
  if (text) node.textContent = text;
  return node;
}

function appendText(parent, tagName, className, text) {
  parent.append(element(tagName, className, text));
}

function createMetadata(page) {
  const metadata = element("dl", "demo-page__metadata");
  const entries = [
    ["By", page.author],
    ["Published", page.date],
    ["Length", page.readingTime],
  ];
  for (const [label, value] of entries) {
    const item = element("div", "demo-page__metadata-item");
    appendText(item, "dt", "demo-page__metadata-label", label);
    appendText(item, "dd", "demo-page__metadata-value", value);
    metadata.append(item);
  }
  return metadata;
}

function createBodyBlock(block) {
  switch (block.kind) {
    case "paragraph":
      return element("p", "demo-page__paragraph", block.text);
    case "quote": {
      const quote = element("blockquote", "demo-page__quote");
      appendText(quote, "p", "demo-page__quote-text", block.text);
      return quote;
    }
    case "facts": {
      const list = element("ul", "demo-page__facts");
      for (const item of block.items) appendText(list, "li", "demo-page__fact", item);
      return list;
    }
    default:
      throw new TypeError(`Unsupported demo-page block: ${String(block.kind)}`);
  }
}

function createIllustration(page) {
  const figure = element("figure", `demo-page__illustration demo-page__illustration--${page.id}`);
  figure.dataset.cssIllustration = page.id;
  figure.setAttribute("role", "img");
  figure.setAttribute("aria-label", page.illustrationLabel);
  const field = element("div", "demo-page__illustration-field");
  field.setAttribute("aria-hidden", "true");
  field.append(
    element("i", "demo-page__shape demo-page__shape--one"),
    element("i", "demo-page__shape demo-page__shape--two"),
    element("i", "demo-page__shape demo-page__shape--three"),
  );
  figure.append(field);
  return figure;
}

function createSignup(page) {
  const dialog = element("section", "demo-page__signup");
  dialog.dataset.pageOverlay = "signup";
  dialog.setAttribute("aria-label", page.signup.title);
  appendText(dialog, "p", "demo-page__overlay-eyebrow", page.signup.eyebrow);
  appendText(dialog, "h2", "demo-page__overlay-title", page.signup.title);
  appendText(dialog, "p", "demo-page__overlay-body", page.signup.body);
  appendText(dialog, "span", "demo-page__input", page.signup.inputLabel);
  appendText(dialog, "span", "demo-page__button", page.signup.action);
  appendText(dialog, "span", "demo-page__privacy", "Privacy details and membership terms");
  return dialog;
}

function createCookiePanel(page) {
  const panel = element("aside", "demo-page__cookie");
  panel.dataset.pageOverlay = "cookie";
  panel.setAttribute("aria-label", page.cookie.title);
  appendText(panel, "h2", "demo-page__cookie-title", page.cookie.title);
  appendText(panel, "p", "demo-page__cookie-body", page.cookie.body);
  appendText(panel, "span", "demo-page__cookie-action", page.cookie.action);
  return panel;
}

function appendOverlays(root, page, overlay) {
  if (!OVERLAY_KINDS.includes(overlay)) {
    throw new RangeError(`Unknown demo-page overlay: ${String(overlay)}`);
  }
  if (overlay === "signup" || overlay === "both") root.append(createSignup(page));
  if (overlay === "cookie" || overlay === "both") root.append(createCookiePanel(page));
  root.dataset.overlay = overlay;
}

export function createDemoPage(id, options = {}) {
  const page = getDemoPage(id);
  const overlay = options.overlay ?? "none";
  const root = element("article", `demo-page demo-page--${page.id}`);
  root.dataset.demoPage = page.id;
  root.dataset.pageKind = page.kind;

  const header = element("header", "demo-page__header");
  appendText(header, "span", "demo-page__publication", page.publication);
  appendText(header, "span", "demo-page__nav", page.navLabel);

  const hero = element("section", "demo-page__hero");
  const titleGroup = element("div", "demo-page__title-group");
  appendText(titleGroup, "p", "demo-page__category", page.category);
  appendText(titleGroup, "h1", "demo-page__title", page.title);
  appendText(titleGroup, "p", "demo-page__deck", page.deck);
  hero.append(titleGroup, createIllustration(page));

  const body = element("section", "demo-page__body");
  body.append(createMetadata(page));
  const copy = element("div", "demo-page__copy");
  for (const block of page.blocks) copy.append(createBodyBlock(block));
  body.append(copy);

  root.append(header, hero, body);
  appendOverlays(root, page, overlay);
  return root;
}

export function createSplitView(options) {
  const root = element("section", "split-view");
  root.dataset.splitView = "true";
  root.dataset.continuousSurface = "true";
  root.dataset.leftPage = options.left;
  root.dataset.rightPage = options.right;
  root.setAttribute("aria-label", "Two local fictional documents in Split View");

  const leftPane = element("div", "split-view__pane split-view__pane--left");
  leftPane.dataset.splitPane = "left";
  leftPane.append(createDemoPage(options.left, { overlay: options.leftOverlay ?? "none" }));

  const divider = element("div", "split-view__divider");
  divider.dataset.splitDivider = "continuous";
  divider.setAttribute("role", "separator");
  divider.setAttribute("aria-orientation", "vertical");

  const rightPane = element("div", "split-view__pane split-view__pane--right");
  rightPane.dataset.splitPane = "right";
  rightPane.append(createDemoPage(options.right, { overlay: options.rightOverlay ?? "none" }));

  root.append(leftPane, divider, rightPane);
  return root;
}

export function mountSplitView(target, options) {
  const splitView = createSplitView(options);
  target.replaceChildren(splitView);
  return splitView;
}
