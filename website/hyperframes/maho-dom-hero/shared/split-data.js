const articleA = Object.freeze({
  id: "article-a",
  kind: "document",
  publication: "Field Notes",
  navLabel: "ISSUE 08 / OBSERVATIONS",
  title: "Listening for weather beneath the city",
  deck: "A field record of the tunnels, chambers, and quiet signals that shape a summer storm before it reaches the street.",
  author: "Mara Venn",
  date: "18 July 2026",
  readingTime: "9 min read",
  category: "Field document / Urban climate",
  illustrationLabel: "Pressure map study in ember, graphite, and fog",
  blocks: Object.freeze([
    Object.freeze({
      kind: "paragraph",
      text: "At noon, the station walls begin to hold the morning heat. A low current moves through the service passages, carrying the mineral smell of rain long before the first cloud crosses the river.",
    }),
    Object.freeze({
      kind: "quote",
      text: "The city does not wait for weather. It rehearses it in concrete, wire, and air.",
    }),
    Object.freeze({
      kind: "paragraph",
      text: "Our instruments were deliberately simple: paper ribbons for airflow, a pocket barometer, and a notebook ruled into six-minute intervals. Together they produced a portrait more useful than a perfect forecast.",
    }),
  ]),
  signup: Object.freeze({
    eyebrow: "FIELD NOTES MEMBERSHIP",
    title: "Read the complete field archive.",
    body: "Join for free to receive monthly documents, annotated maps, and interviews with independent researchers.",
    action: "Continue with email",
    inputLabel: "Email address",
  }),
  cookie: Object.freeze({
    title: "Reading preferences",
    body: "We use local preferences to remember type size and reading position for this fictional demonstration.",
    action: "Accept preferences",
  }),
});

const articleB = Object.freeze({
  id: "article-b",
  kind: "article",
  publication: "Meridian Review",
  navLabel: "ESSAYS / MATERIAL CULTURE",
  title: "A room designed for changing light",
  deck: "How one coastal studio treats shade, reflection, and color as materials that move throughout the day.",
  author: "Ilan Sorrell",
  date: "21 July 2026",
  readingTime: "7 min read",
  category: "Essay / Architecture",
  illustrationLabel: "Abstract room study in vermilion, chalk, and midnight blue",
  blocks: Object.freeze([
    Object.freeze({
      kind: "paragraph",
      text: "The west wall is painted a dense mineral red. By morning it is almost matte; at dusk it reflects a narrow strip of sea light onto the ceiling and appears to lift away from the floor.",
    }),
    Object.freeze({
      kind: "facts",
      items: Object.freeze([
        "Three rooms aligned to the afternoon sun",
        "Pigment mixed from iron oxide and lime",
        "Movable linen screens instead of fixed blinds",
      ]),
    }),
    Object.freeze({
      kind: "paragraph",
      text: "Nothing in the studio is arranged as a permanent focal point. The architecture asks visitors to notice edges: where warm plaster meets cool stone, where shadow becomes color, and where a view becomes a frame.",
    }),
  ]),
  signup: Object.freeze({
    eyebrow: "MERIDIAN LETTERS",
    title: "A slower letter about design.",
    body: "Receive one original essay and one studio visit on the first Sunday of each month.",
    action: "Join the list",
    inputLabel: "Email address",
  }),
  cookie: Object.freeze({
    title: "Cookie settings",
    body: "This local demonstration stores no data. The panel exists only as a Split View layout primitive.",
    action: "Keep essential only",
  }),
});

export const DEMO_PAGE_IDS = Object.freeze([articleA.id, articleB.id]);

export const DEMO_PAGES = Object.freeze({
  [articleA.id]: articleA,
  [articleB.id]: articleB,
});

export const SPLIT_VIEW_PRESETS = Object.freeze({
  initial: Object.freeze({ left: articleA.id, right: articleB.id, leftOverlay: "none", rightOverlay: "none" }),
  action: Object.freeze({ left: articleA.id, right: articleB.id, leftOverlay: "signup", rightOverlay: "none" }),
  settled: Object.freeze({ left: articleA.id, right: articleB.id, leftOverlay: "both", rightOverlay: "none" }),
});

export function isDemoPageId(value) {
  return DEMO_PAGE_IDS.includes(value);
}

export function getDemoPage(id) {
  if (!isDemoPageId(id)) {
    throw new RangeError(`Unknown demo page: ${String(id)}`);
  }
  return DEMO_PAGES[id];
}
