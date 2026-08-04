export {};

const sharedRoot = "/shared/";
const [{ mountBrowserShell }, { createDemoPage }, { SHELL_DATA }] = await Promise.all([
  import(`${sharedRoot}browser-shell.js`),
  import(`${sharedRoot}demo-pages.js`),
  import(`${sharedRoot}shell-data.js`),
]);

const SPACE_STATES = Object.freeze([
  Object.freeze({
    id: "initial",
    address: "garden.maho.local / water-index",
    label: "Garden",
    activeDot: 0,
    rows: Object.freeze(["Water Garden", "Color Notes", "Pond Archive", "Seasonal Studies"]),
    activeRow: "Water Garden",
  }),
  Object.freeze({
    id: "action",
    address: "atlas.maho.local / botanical-research",
    label: "Research",
    activeDot: 2,
    rows: Object.freeze(["Atlas Search", "Species Index", "Color Analysis", "Field Notes"]),
    activeRow: "Atlas Search",
  }),
  Object.freeze({
    id: "final",
    address: "meridian.maho.local / changing-light",
    label: "Reading",
    activeDot: 3,
    rows: Object.freeze(["Field Notes", "Meridian Review", "Aeon Essays", "Saved Papers"]),
    activeRow: "Aeon Essays",
  }),
]);

const shellMount = document.querySelector("#spaces-shell-mount");
if (!(shellMount instanceof HTMLElement)) throw new TypeError("Spaces shell mount is missing");

const shellData = Object.freeze({
  ...SHELL_DATA,
  address: SPACE_STATES[0].address,
  workspaceLabel: "Spaces",
  primaryTabs: Object.freeze([]),
  secondaryTabs: Object.freeze([]),
});

const shell = mountBrowserShell(shellMount, shellData);
if (!(shell instanceof HTMLElement)) throw new TypeError("Shared browser shell did not mount");

function element(tagName, className = "", text = "") {
  const node = document.createElement(tagName);
  node.className = className;
  node.textContent = text;
  return node;
}

function createSidebarState(state) {
  const group = element("section", "spaces-sidebar-state");
  group.dataset.spacesState = state.id;
  group.dataset.layoutAllowOverlap = "true";
  group.append(element("p", "spaces-sidebar-state__label", state.label));
  const rows = element("div", "spaces-sidebar-state__rows");
  for (const [index, label] of state.rows.entries()) {
    const row = element("div", "spaces-sidebar-row");
    row.dataset.activeRow = String(label === state.activeRow);
    row.append(
      element("span", "spaces-sidebar-row__icon", String(index + 1).padStart(2, "0")),
      element("span", "spaces-sidebar-row__label", label),
    );
    rows.append(row);
  }
  group.append(rows);
  return group;
}

function createAddressState(state) {
  const address = element("span", "spaces-address-state", state.address);
  address.dataset.spacesState = state.id;
  address.dataset.layoutAllowOverlap = "true";
  return address;
}

function createFooterState(state) {
  const dots = element("span", "spaces-footer-state");
  dots.dataset.spacesState = state.id;
  for (let index = 0; index < 4; index += 1) {
    const dot = element("span", "spaces-footer-dot");
    dot.dataset.active = String(index === state.activeDot);
    dots.append(dot);
  }
  return dots;
}

function createInitialContent() {
  const root = element("article", "spaces-content spaces-content--garden");
  root.dataset.spacesState = "initial";
  root.dataset.layoutAllowOverlap = "true";
  root.dataset.layoutAllowOverflow = "true";
  const heading = element("header", "spaces-garden-heading");
  heading.append(
    element("p", "spaces-kicker", "Water garden / visual archive"),
    element("h1", "spaces-garden-title", "A living index of color, reflection, and season."),
    element("p", "spaces-garden-deck", "Local studies arranged as a quiet workspace with authored document layers."),
  );
  const study = element("div", "spaces-pond-study");
  study.setAttribute("role", "img");
  study.setAttribute("aria-label", "Abstract CSS water garden study");
  for (const className of ["water", "reed", "lily-a", "lily-b", "lily-c", "bloom-a", "bloom-b"]) {
    study.append(element("span", `spaces-pond-study__${className}`));
  }
  const notes = element("div", "spaces-garden-notes");
  for (const [label, value] of [["Palette", "Moss / chalk / coral"], ["Light", "Late afternoon"], ["Collection", "18 field studies"]]) {
    const note = element("div", "spaces-garden-note");
    note.append(element("span", "spaces-garden-note__label", label), element("strong", "spaces-garden-note__value", value));
    notes.append(note);
  }
  root.append(heading, study, notes);
  return root;
}

function createActionContent() {
  const root = element("article", "spaces-content spaces-content--atlas");
  root.dataset.spacesState = "action";
  root.dataset.layoutAllowOverlap = "true";
  root.dataset.layoutAllowOcclusion = "true";
  root.dataset.layoutAllowOverflow = "true";
  const header = element("header", "spaces-atlas-header");
  header.append(element("span", "spaces-atlas-mark", "A"), element("span", "spaces-atlas-name", "Atlas Research"));
  const search = element("div", "spaces-atlas-search", "water lily color distribution");
  const grid = element("div", "spaces-atlas-grid");
  const topics = [["Species", "46 indexed"], ["Pigment", "12 clusters"], ["Season", "May—October"], ["Habitat", "Still water"]];
  for (const [index, topic] of topics.entries()) {
    const card = element("section", "spaces-atlas-card");
    card.dataset.topic = String(index + 1);
    card.append(element("span", "spaces-atlas-card__index", `0${index + 1}`), element("h2", "spaces-atlas-card__title", topic[0]), element("p", "spaces-atlas-card__value", topic[1]));
    const bars = element("div", "spaces-atlas-card__bars");
    bars.append(element("i"), element("i"), element("i"));
    card.append(bars);
    grid.append(card);
  }
  root.append(header, element("p", "spaces-kicker", "Botanical query / local dataset"), search, grid);
  return root;
}

function createFinalContent() {
  const article = createDemoPage("article-b");
  article.classList.add("spaces-content", "spaces-content--reading");
  article.dataset.spacesState = "final";
  article.dataset.spacesArticle = "true";
  article.dataset.layoutAllowOverlap = "true";
  article.dataset.layoutAllowOcclusion = "true";
  article.dataset.layoutAllowOverflow = "true";
  return article;
}

const workspace = shell.querySelector('[data-shell="tab-groups"]');
const addressText = shell.querySelector(".shell-address__text");
const footerDots = shell.querySelector(".shell-space-dots");
const browserContent = shell.querySelector('[data-shell="browser-content"]');
if (!(workspace instanceof HTMLElement) || !(addressText instanceof HTMLElement) || !(footerDots instanceof HTMLElement) || !(browserContent instanceof HTMLElement)) {
  throw new TypeError("Shared shell slots are incomplete");
}

workspace.replaceChildren(...SPACE_STATES.map(createSidebarState));
addressText.replaceChildren(...SPACE_STATES.map(createAddressState));
footerDots.replaceChildren(...SPACE_STATES.map(createFooterState));
browserContent.replaceChildren(createInitialContent(), createActionContent(), createFinalContent());

const browserWrapper = document.querySelector("#spaces-browser-wrapper");
const timelineRegistry = Object.getOwnPropertyDescriptor(globalThis, "__timelines")?.value;
const timeline = timelineRegistry?.spaces;
if (!(browserWrapper instanceof HTMLElement) || !timeline) throw new TypeError("Spaces camera timeline is unavailable");
browserWrapper.dataset.layoutAllowOverflow = "true";

const initialState = document.querySelectorAll('[data-spaces-state="initial"]');
const actionState = document.querySelectorAll('[data-spaces-state="action"]');
const finalState = document.querySelectorAll('[data-spaces-state="final"]');

timeline.set(browserWrapper, { x: 0, y: 0, scale: 1, opacity: 1 }, 0);
timeline.set(initialState, { autoAlpha: 1 }, 0);
timeline.set(actionState, { autoAlpha: 0 }, 0);
timeline.set(finalState, { autoAlpha: 0 }, 0);
timeline.set(initialState, { autoAlpha: 0 }, 6.2);
timeline.set(actionState, { autoAlpha: 1 }, 6.2);
timeline.to(browserWrapper, { x: 96, y: -12, scale: 1.1, duration: 1.8, ease: "power2.inOut" }, 8.2);
timeline.set(actionState, { autoAlpha: 0 }, 10.8);
timeline.set(finalState, { autoAlpha: 1 }, 10.8);
timeline.to(browserWrapper, { x: 24, y: -4, scale: 1.03, duration: 1.8, ease: "power2.inOut" }, 10.8);
