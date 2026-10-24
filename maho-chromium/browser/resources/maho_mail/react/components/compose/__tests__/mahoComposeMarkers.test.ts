import { afterEach, describe, expect, it } from "vitest";
import { Editor } from "@tiptap/core";
import { Fragment, Slice } from "@tiptap/pm/model";
import { NodeSelection, TextSelection } from "@tiptap/pm/state";
import StarterKit from "@tiptap/starter-kit";
import { mahoComposeMarkers } from "../mahoComposeMarkers";
import { replaceSignatureHtml } from "../ComposeForm";

const OLD_SIG = '<br><br><div data-maho-signature="true" class="email-signature">--<br>Old Sig</div>';
const NEW_SIG = '<br><br><div data-maho-signature="true" class="email-signature">--<br>New Sig</div>';
const QUOTE =
  '<br><br><p data-maho-quote="true">On Mon, a@b.c wrote:</p><blockquote data-maho-quote="true"><p>Original</p></blockquote>';

const HEADER = '<p data-maho-quote="true">On Mon, a@b.c wrote:</p>';
const INNER_HEADER = '<p data-maho-quote="true">On Sun, c@d.e wrote:</p>';
const BLOCKQUOTE = '<blockquote data-maho-quote="true"><p>Original</p></blockquote>';

let editor: Editor | null = null;

afterEach(() => {
  editor?.destroy();
  editor = null;
});

function serializeThroughEditor(html: string): string {
  editor = new Editor({ extensions: [StarterKit, ...mahoComposeMarkers], content: html });
  editor.commands.insertContentAt(1, "Hi ");
  return editor.getHTML();
}

describe("compose markers survive Tiptap serialization", () => {
  it("keeps data-maho-signature and data-maho-quote after an edit", () => {
    const html = serializeThroughEditor(`<p>Body</p>${OLD_SIG}${QUOTE}`);
    expect(html).toContain('data-maho-signature="true"');
    expect(html).toContain('data-maho-quote="true"');
    expect(html).toContain("Old Sig");
  });

  it("swaps the signature without duplicating it", () => {
    const edited = serializeThroughEditor(`<p>Body</p>${OLD_SIG}${QUOTE}`);
    const swapped = replaceSignatureHtml(edited, NEW_SIG, QUOTE);
    expect(swapped).not.toContain("Old Sig");
    expect(swapped.match(/New Sig/g)).toHaveLength(1);
    expect(swapped.indexOf("New Sig")).toBeLessThan(swapped.indexOf("Original"));
    expect(swapped.match(/Original/g)).toHaveLength(1);
  });

  it("keeps the HTML stable across repeated swaps through the editor", () => {
    const sigs = [NEW_SIG, OLD_SIG, NEW_SIG, OLD_SIG];
    const roundTrip = (html: string) => {
      editor?.destroy();
      editor = new Editor({ extensions: [StarterKit, ...mahoComposeMarkers], content: html });
      return editor.getHTML();
    };
    const blankLines = (html: string) => (html.match(/<br>/g) ?? []).length;
    let html = roundTrip(`<p>Body</p>${OLD_SIG}${QUOTE}`);
    const results: string[] = [];
    for (const sig of sigs) {
      html = roundTrip(replaceSignatureHtml(html, sig, QUOTE));
      results.push(html);
    }
    // Same signature in, byte-identical HTML out: no gap accumulates.
    expect(results[2]).toBe(results[0]);
    expect(results[3]).toBe(results[1]);
    expect(blankLines(results[0])).toBe(blankLines(roundTrip(`<p>Body</p>${NEW_SIG}${QUOTE}`)));
    // Signature stays separated from and above the quote header.
    const sigEnd = html.indexOf("</div>");
    const header = html.indexOf('<p data-maho-quote="true">On Mon');
    expect(sigEnd).toBeGreaterThan(-1);
    expect(header).toBeGreaterThan(sigEnd);
    expect(html.slice(sigEnd, header)).toBe("</div><p><br><br></p>");
  });

  it("keeps raw-HTML swaps stable without the editor", () => {
    const once = replaceSignatureHtml(`<p>Body</p>${OLD_SIG}${QUOTE}`, NEW_SIG, QUOTE);
    const twice = replaceSignatureHtml(replaceSignatureHtml(once, OLD_SIG, QUOTE), NEW_SIG, QUOTE);
    expect(once).toBe(`<p>Body</p>${NEW_SIG}${QUOTE}`);
    expect(twice).toBe(once);
  });

  it("does not carry the quote marker onto a line split off the quote header", () => {
    editor = new Editor({ extensions: [StarterKit, ...mahoComposeMarkers], content: `<p>Body</p>${OLD_SIG}${QUOTE}` });
    let headerPos = -1;
    editor.state.doc.descendants((node, pos) => {
      if (headerPos < 0 && node.type.name === "paragraph" && node.textContent.startsWith("On Mon")) headerPos = pos + 1;
    });
    // Enter at the start of the quote header, then type on the new line.
    editor.chain().setTextSelection(headerPos).splitBlock().run();
    editor.chain().setTextSelection(headerPos).insertContent("MyText").run();
    const html = editor.getHTML();
    expect(html.match(/<p data-maho-quote="true">/g)).toHaveLength(1);
    expect(html).toContain("<p>MyText</p>");

    const swapped = replaceSignatureHtml(html, NEW_SIG, QUOTE);
    // The user's line stays above the new signature, which stays above the header.
    expect(swapped.indexOf("MyText")).toBeLessThan(swapped.indexOf("New Sig"));
    expect(swapped.indexOf("New Sig")).toBeLessThan(swapped.indexOf("On Mon"));
  });

  it("keeps the quote header identity after Backspace at its start", () => {
    editor = new Editor({ extensions: [StarterKit, ...mahoComposeMarkers], content: `<p>Body</p>${OLD_SIG}${QUOTE}` });
    let headerPos = -1;
    editor.state.doc.descendants((node, pos) => {
      if (headerPos < 0 && node.type.name === "paragraph" && node.textContent.startsWith("On Mon")) headerPos = pos + 1;
    });
    // Backspace at the start of the quote header joins it into the separator above.
    editor.chain().setTextSelection(headerPos).joinBackward().run();
    const html = editor.getHTML();
    expect(html.match(/<p data-maho-quote="true">/g)).toHaveLength(1);
    expect(html).toMatch(/<p data-maho-quote="true">[^]*On Mon[^]*<\/p><blockquote/);

    const swapped = replaceSignatureHtml(html, NEW_SIG, QUOTE);
    expect(swapped.indexOf("Body")).toBeLessThan(swapped.indexOf("New Sig"));
    expect(swapped.indexOf("New Sig")).toBeLessThan(swapped.indexOf("On Mon"));
    expect(swapped.indexOf("On Mon")).toBeLessThan(swapped.indexOf("<blockquote"));
  });

  it("keeps a marked signature inside the quoted message on a swap", () => {
    // A mail sent from Maho carries its sender's marked signature.
    const quotedSig = '<div data-maho-signature="true" class="email-signature">--<br>Alice Corp</div>';
    const quote =
      `<br><br><p data-maho-quote="true">On Mon, a@b.c wrote:</p><blockquote data-maho-quote="true"><p>Hi</p><br><br>${quotedSig}</blockquote>`;
    const raw = replaceSignatureHtml(`<p>Body</p>${OLD_SIG}${quote}`, NEW_SIG, quote);
    expect(raw).toBe(`<p>Body</p>${NEW_SIG}${quote}`);

    editor = new Editor({ extensions: [StarterKit, ...mahoComposeMarkers], content: `<p>Body</p>${OLD_SIG}${quote}` });
    const html = editor.getHTML();
    const quotedPart = html.slice(html.indexOf("<blockquote"));
    const swapped = replaceSignatureHtml(html, NEW_SIG, quote);
    expect(swapped).not.toContain("Old Sig");
    expect(swapped).toContain("New Sig");
    expect(swapped.endsWith(quotedPart)).toBe(true);
    expect(quotedPart).toContain("Alice Corp");
  });

  it("round-trips the attribution line byte-identically through draft save and reload", () => {
    const ed = openEditor(`<p>Body</p>${OLD_SIG}${QUOTE}`);
    ed.chain().setTextSelection(1).insertContent("Hi ").run();
    const saved = ed.getHTML();
    ed.destroy();
    const reopened = openEditor(saved);
    expect(reopened.getHTML()).toBe(saved);
    expect(saved).toContain(`${HEADER}<blockquote data-maho-quote="true"><p>Original</p></blockquote>`);
    expect(reopened.state.doc.resolve(headerStart(reopened)).parent.type.name).toBe("mahoQuoteHeader");
  });

  it("applies a swapped draft to a live editor without the filter dropping the load", () => {
    const ed = openEditor(`<p>Body</p>${OLD_SIG}${QUOTE}`);
    const swapped = replaceSignatureHtml(ed.getHTML(), NEW_SIG, QUOTE);
    ed.commands.setContent(swapped, { emitUpdate: false });
    const html = ed.getHTML();
    expect(html).not.toContain("Old Sig");
    expect(html).toContain("New Sig");
    expect(html).toContain(HEADER);
    expect(html).toContain('<blockquote data-maho-quote="true"><p>Original</p></blockquote>');
    expect(headerCount(html)).toBe(1);
  });

  it("loading a different draft into a live editor replaces its attribution", () => {
    const ed = openEditor(`<p>Body</p>${OLD_SIG}${QUOTE}`);
    const other = '<p>Other</p><p data-maho-quote="true">On Tue, z@y.x wrote:</p><blockquote data-maho-quote="true"><p>Q</p></blockquote>';
    ed.commands.setContent(other, { emitUpdate: false });
    const html = ed.getHTML();
    expect(html).toContain('<p data-maho-quote="true">On Tue, z@y.x wrote:</p>');
    expect(headerCount(html)).toBe(1);
  });

  it("a swap never deletes a header saved inside the signature by older drafts", () => {
    const legacy =
      '<p>Body</p><div data-maho-signature="true" class="email-signature"><p>--<br>Old Sig</p>' +
      '<p data-maho-quote="true">On Mon, a@b.c wrote:</p></div><blockquote data-maho-quote="true"><p>Original</p></blockquote>';
    const swapped = replaceSignatureHtml(legacy, NEW_SIG, QUOTE);
    expect(swapped).not.toContain("Old Sig");
    expect(swapped.match(/On Mon/g)).toHaveLength(1);
    expect(swapped.indexOf("New Sig")).toBeLessThan(swapped.indexOf("On Mon"));
    expect(swapped.indexOf("On Mon")).toBeLessThan(swapped.indexOf("<blockquote"));
  });
});

function openEditor(html: string): Editor {
  editor = new Editor({ extensions: [StarterKit, ...mahoComposeMarkers], content: html });
  return editor;
}

/** Position just inside the quote header, i.e. the caret at its first character. */
function headerStart(ed: Editor): number {
  let pos = -1;
  ed.state.doc.descendants((node, nodePos) => {
    if (pos < 0 && node.textContent.startsWith("On Mon") && node.isTextblock) pos = nodePos + 1;
  });
  return pos;
}

function headerEnd(ed: Editor): number {
  const start = headerStart(ed);
  return start + ed.state.doc.resolve(start).parent.content.size;
}

function textblockStart(ed: Editor, prefix: string): number {
  let pos = -1;
  ed.state.doc.descendants((node, nodePos) => {
    if (pos < 0 && node.isTextblock && node.textContent.startsWith(prefix)) pos = nodePos + 1;
  });
  return pos;
}

function textblockEnd(ed: Editor, prefix: string): number {
  const start = textblockStart(ed, prefix);
  return start + ed.state.doc.resolve(start).parent.content.size;
}

function isInsideHeader($pos: import("@tiptap/pm/model").ResolvedPos): boolean {
  for (let depth = $pos.depth; depth > 0; depth--) {
    if ($pos.node(depth).type.name === "mahoQuoteHeader") return true;
  }
  return false;
}

function isInsideBlockquote($pos: import("@tiptap/pm/model").ResolvedPos): boolean {
  for (let depth = $pos.depth; depth > 0; depth--) {
    if ($pos.node(depth).type.name === "blockquote") return true;
  }
  return false;
}

function isInsideSignature($pos: import("@tiptap/pm/model").ResolvedPos): boolean {
  for (let depth = $pos.depth; depth > 0; depth--) {
    if ($pos.node(depth).type.name === "mahoSignature") return true;
  }
  return false;
}

function headerCount(html: string): number {
  return (html.match(/<p data-maho-quote="true">/g) ?? []).length;
}

/**
 * Drives the editor's real keydown path (the Tiptap keymap plugins, including
 * the header's own shortcuts) with a real KeyboardEvent and returns whether a
 * handler actually handled the key. Unlike `editor.commands.keyboardShortcut`
 * (which always returns true and drops selection changes), this goes through
 * the view's dispatch, so doc and selection assertions reflect what the user
 * would get.
 */
function keydown(ed: Editor, key: "Enter" | "Backspace" | "Delete"): boolean {
  const event = new KeyboardEvent("keydown", { key, bubbles: true, cancelable: true });
  return ed.view.someProp("handleKeyDown", (f) => f(ed.view, event)) ?? false;
}

/**
 * Leaves the caret inside a header the way a transaction another plugin appends
 * can: the invariant ignores those, so the guards that must survive a caret in
 * the attribution (the I1 filter, Enter, Backspace) stay reachable in tests.
 */
function placeCaretAsAppendedTransaction(ed: Editor, pos: number): void {
  const tr = ed.state.tr.setSelection(TextSelection.create(ed.state.doc, pos));
  tr.setMeta("appendedTransaction", true);
  ed.view.dispatch(tr);
}

describe("quote header invariant I1: no edit may rewrite the attribution", () => {
  const NOTE_AFTER_HEADER = `${HEADER}<p>Note</p>${BLOCKQUOTE}`;

  it("Backspace at the start of the line after the header leaves the header byte-identical", () => {
    const ed = openEditor(NOTE_AFTER_HEADER);
    ed.commands.setTextSelection(textblockStart(ed, "Note"));
    expect(keydown(ed, "Backspace")).toBe(true);
    expect(ed.getHTML()).toBe(`${NOTE_AFTER_HEADER}<p></p>`);
    const sel = ed.state.selection;
    expect(sel).toBeInstanceOf(TextSelection);
    expect(sel.empty).toBe(true);
    expect(isInsideHeader(sel.$from)).toBe(false);
    // The keystroke that used to land inside the header now types into the user's line.
    ed.commands.insertContent("Q");
    const html = ed.getHTML();
    expect(headerCount(html)).toBe(1);
    expect(html).toContain(HEADER);
    expect(html).toContain("<p>QNote</p>");
  });

  it("Delete at the end of the line before the header leaves the header byte-identical", () => {
    const ed = openEditor(`<p>Thanks</p>${HEADER}${BLOCKQUOTE}`);
    ed.commands.setTextSelection(textblockEnd(ed, "Thanks"));
    expect(keydown(ed, "Delete")).toBe(true);
    const sel = ed.state.selection;
    expect(sel).toBeInstanceOf(TextSelection);
    expect(isInsideHeader(sel.$from)).toBe(false);
    ed.commands.insertContent("x");
    const html = ed.getHTML();
    expect(headerCount(html)).toBe(1);
    expect(html).toContain(HEADER);
    expect(html).toContain("<p>Thanksx</p>");
  });

  it("Backspace at the start of a line after a nested header leaves the nested header byte-identical", () => {
    const layout =
      `<p>Body</p>${HEADER}<blockquote data-maho-quote="true"><p>Reply1</p>${INNER_HEADER}` +
      `<p>Hello</p><blockquote data-maho-quote="true"><p>Oldest</p></blockquote></blockquote>`;
    const ed = openEditor(layout);
    ed.commands.setTextSelection(textblockStart(ed, "Hello"));
    expect(keydown(ed, "Backspace")).toBe(true);
    const sel = ed.state.selection;
    expect(sel).toBeInstanceOf(TextSelection);
    expect(isInsideHeader(sel.$from)).toBe(false);
    ed.commands.insertContent("Q");
    const html = ed.getHTML();
    expect(headerCount(html)).toBe(2);
    expect(html).toContain(HEADER);
    expect(html).toContain(INNER_HEADER);
    expect(html).not.toContain("OnQ Mon");
    expect(html).not.toContain("OnQ Sun");
    expect(html).toContain("QHello");
    // The substring checks above cannot see the quote being SPLIT: lifting
    // `Hello` out of the inner blockquote keeps every one of those strings
    // while turning one quoted thread into two. Pin the whole document.
    expect(html).toBe(
      `<p>Body</p>${HEADER}<blockquote data-maho-quote="true"><p>Reply1</p>${INNER_HEADER}` +
        `<p>QHello</p><blockquote data-maho-quote="true"><p>Oldest</p></blockquote></blockquote><p></p>`,
    );
  });

  it("Backspace at the start of the first quoted line keeps the quote intact", () => {
    const ed = openEditor(`<p>Body</p>${HEADER}${BLOCKQUOTE}`);
    ed.commands.setTextSelection(textblockStart(ed, "Original"));
    const before = ed.getHTML();
    expect(keydown(ed, "Backspace")).toBe(true);
    // The blockquote is the only thing standing between the caret and the
    // attribution, so joinBackward cannot merge and the keymap would lift the
    // line out - deleting the blockquote outright, silently. Nothing about the
    // quoted message may change, and the caret must not move either: a caret
    // that relocated would make the next typed character land on a different
    // line from the one the user is looking at.
    expect(ed.getHTML()).toBe(before);
    const sel = ed.state.selection;
    expect(sel).toBeInstanceOf(TextSelection);
    expect(isInsideHeader(sel.$from)).toBe(false);
    ed.commands.insertContent("Q");
    expect(ed.getHTML()).toBe(before.replace("<p>Original</p>", "<p>QOriginal</p>"));
  });

  it("Backspace still removes a blockquote the USER authored", () => {
    // The compose toolbar has a blockquote control and the `> ` input rule
    // creates them too, and those carry no data-maho-quote marker. That is the
    // user's own formatting, and Backspace at the start of its first line is
    // how it is removed - the quoted-message barriers must not reach into it.
    const ed = openEditor(`<p>Body</p><blockquote><p>note</p></blockquote>`);
    ed.commands.setTextSelection(textblockStart(ed, "note"));
    expect(keydown(ed, "Backspace")).toBe(true);
    expect(ed.getHTML()).toBe(`<p>Body</p><p>note</p><p></p>`);
  });

  it("Backspace never converts a heading or code block above into plain text", () => {
    // `tr.join` merges the caret's line into whatever type precedes it, so a
    // blanket join would silently turn a heading (or a pasted code block) into
    // a paragraph, or turn the caret's line into code. Only paragraph joins
    // into paragraph.
    for (const [block, text] of [
      ["<h1>Head</h1>", "Head"],
      ["<pre><code>code()</code></pre>", "code()"],
    ] as const) {
      const layout = `<p>Body</p>${HEADER}<blockquote data-maho-quote="true"><p>one</p>${block}</blockquote>`;
      const ed = openEditor(layout);
      ed.commands.setTextSelection(textblockStart(ed, text));
      const before = ed.getHTML();
      expect(keydown(ed, "Backspace")).toBe(true);
      expect(ed.getHTML()).toBe(before);
    }
  });

  it("Backspace still merges a quoted line into ordinary text above it", () => {
    // The barrier must not swallow the normal, expected merge.
    const ed = openEditor(
      `<p>Body</p>${HEADER}<blockquote data-maho-quote="true"><p>One</p><p>Two</p></blockquote>`,
    );
    ed.commands.setTextSelection(textblockStart(ed, "Two"));
    expect(keydown(ed, "Backspace")).toBe(true);
    expect(ed.getHTML()).toBe(
      `<p>Body</p>${HEADER}<blockquote data-maho-quote="true"><p>OneTwo</p></blockquote><p></p>`,
    );
    // The caret ends up at the join point, inside the quote, where the user
    // was looking - not thrown back past the attribution.
    expect(ed.state.selection.$from.parent.textContent).toBe("OneTwo");
    expect(ed.state.selection.$from.parentOffset).toBe(3);
  });

  it("Backspace at the start of a line whose previous sibling is a NESTED quote keeps the quote intact", () => {
    // A nested quote is not text, so there is nothing to join; the keymap would
    // lift this line OUT of the outer quote, cutting the quoted thread in two.
    const ed = openEditor(
      `<p>Body</p>${HEADER}<blockquote data-maho-quote="true"><p>R1</p>` +
        `<blockquote data-maho-quote="true"><p>Inner</p></blockquote><p>After</p></blockquote>`,
    );
    ed.commands.setTextSelection(textblockStart(ed, "After"));
    const before = ed.getHTML();
    expect(keydown(ed, "Backspace")).toBe(true);
    expect(ed.getHTML()).toBe(before);
    expect(ed.state.selection.$from.parent.textContent).toBe("After");
  });

  it("Backspace on an empty line that is the quote's only child does not move the caret out", () => {
    // The blockquote must hold at least one block, so deleting the line would
    // just re-create it - the user would see nothing change while the caret
    // jumped out of the quote. It is a barrier instead.
    const ed = openEditor(`<p>Body</p>${HEADER}<blockquote data-maho-quote="true"><p></p></blockquote>`);
    let pos = -1;
    ed.state.doc.descendants((n, p) => {
      if (pos < 0 && n.type.name === "blockquote") {
        pos = p + 1;
        return false;
      }
      return true;
    });
    ed.commands.setTextSelection(TextSelection.create(ed.state.doc, pos));
    const before = ed.getHTML();
    expect(keydown(ed, "Backspace")).toBe(true);
    expect(ed.getHTML()).toBe(before);
    // The caret must stay INSIDE the quote: a key that changes nothing must not
    // silently relocate the insertion point.
    expect(ed.state.selection.$from.parent.type.name).toBe("paragraph");
    expect(ed.state.selection.$from.parent.textContent).toBe("");
  });

  it("Delete at the end of a line before a nested header leaves the nested header byte-identical", () => {
    const layout =
      `<p>Body</p>${HEADER}<blockquote data-maho-quote="true"><p>Hello</p>${INNER_HEADER}` +
      `<blockquote data-maho-quote="true"><p>Oldest</p></blockquote></blockquote>`;
    const ed = openEditor(layout);
    ed.commands.setTextSelection(textblockEnd(ed, "Hello"));
    expect(keydown(ed, "Delete")).toBe(true);
    const sel = ed.state.selection;
    expect(sel).toBeInstanceOf(TextSelection);
    expect(isInsideHeader(sel.$from)).toBe(false);
    ed.commands.insertContent("X");
    const html = ed.getHTML();
    expect(headerCount(html)).toBe(2);
    expect(html).toContain(HEADER);
    expect(html).toContain(INNER_HEADER);
    expect(html).not.toContain("XOn Mon");
    expect(html).not.toContain("XOn Sun");
  });

  it("a programmatic insert into a header is rejected", () => {
    const ed = openEditor(`${HEADER}<p>Hello</p>${BLOCKQUOTE}`);
    placeCaretAsAppendedTransaction(ed, headerStart(ed) + 3);
    expect(isInsideHeader(ed.state.selection.$from)).toBe(true);
    const before = ed.getHTML();
    ed.commands.insertContent("INJECTED");
    expect(ed.getHTML()).toBe(before);
    expect(ed.getHTML()).toContain(HEADER);
  });

  it("pasting into a header is rejected", () => {
    const ed = openEditor(`${HEADER}<p>Hello</p>${BLOCKQUOTE}`);
    placeCaretAsAppendedTransaction(ed, headerStart(ed) + 3);
    const before = ed.getHTML();
    ed.view.pasteHTML("<b>INJECTED</b>", {} as ClipboardEvent);
    expect(ed.getHTML()).toBe(before);
    expect(ed.getHTML()).toContain(HEADER);
  });

  it("pasting content that carries a header cannot create a second attribution line", () => {
    const ed = openEditor(`<p>Body</p>${HEADER}${BLOCKQUOTE}`);
    ed.commands.setTextSelection(textblockEnd(ed, "Body"));
    ed.view.pasteHTML('<p data-maho-quote="true">Fake, f@g.h wrote:</p>', {} as ClipboardEvent);
    const html = ed.getHTML();
    expect(headerCount(html)).toBe(1);
    expect(html).toContain(HEADER);
    expect(html).toContain("<p>Fake, f@g.h wrote:</p>");
  });

  it("dropped content goes through the same slice transform as paste", () => {
    const ed = openEditor(`<p>Body</p>${HEADER}${BLOCKQUOTE}`);
    const header = ed.state.doc.nodeAt(headerStart(ed) - 1);
    expect(header?.type.name).toBe("mahoQuoteHeader");
    const slice = new Slice(Fragment.from(header!), 0, 0);
    const transformed = ed.view.someProp("transformPasted", (f) => f(slice, ed.view, false)) ?? slice;
    expect(transformed.content.child(0).type.name).toBe("paragraph");
    expect(transformed.content.child(0).textContent).toBe("On Mon, a@b.c wrote:");
  });
});

describe("quote header invariant I2: the caret never settles in a header", () => {
  it("Backspace at the start of the line after a header never node-selects the header", () => {
    const ed = openEditor(`${HEADER}<p>Note</p>${BLOCKQUOTE}`);
    ed.commands.setTextSelection(textblockStart(ed, "Note"));
    expect(keydown(ed, "Backspace")).toBe(true);
    expect(ed.state.selection).toBeInstanceOf(TextSelection);
    expect(ed.state.selection).not.toBeInstanceOf(NodeSelection);
    expect(isInsideHeader(ed.state.selection.$from)).toBe(false);
    expect(ed.getHTML()).toContain(HEADER);
  });

  it("Delete at the end of the line before a header never node-selects the header", () => {
    const ed = openEditor(`<p>Thanks</p>${HEADER}${BLOCKQUOTE}`);
    ed.commands.setTextSelection(textblockEnd(ed, "Thanks"));
    expect(keydown(ed, "Delete")).toBe(true);
    expect(ed.state.selection).not.toBeInstanceOf(NodeSelection);
    expect(isInsideHeader(ed.state.selection.$from)).toBe(false);
    expect(ed.getHTML()).toContain(HEADER);
  });

  it("an edit that leaves the caret inside a populated header moves it outside", () => {
    const ed = openEditor(`<p>Body</p>${HEADER}${BLOCKQUOTE}`);
    const tr = ed.state.tr;
    tr.insertText("!", textblockEnd(ed, "Body"));
    tr.setSelection(TextSelection.create(tr.doc, headerStart(ed) + 3));
    ed.view.dispatch(tr);
    const sel = ed.state.selection;
    expect(sel).toBeInstanceOf(TextSelection);
    expect(isInsideHeader(sel.$from)).toBe(false);
    expect(ed.getHTML()).toContain(HEADER);
  });

  it("a nested quote: the caret a root transaction leaves in a populated header is pushed past it, so the key is not dead", () => {
    const layout =
      `<p>Body</p><blockquote data-maho-quote="true"><p>Reply1</p>` +
      `<blockquote data-maho-quote="true">${INNER_HEADER}<p>Hello</p></blockquote></blockquote>`;
    const ed = openEditor(layout);
    ed.commands.setTextSelection(textblockEnd(ed, "Reply1"));
    const nestedHeader = textblockStart(ed, "On Sun") - 1;
    // A key-driven arrival (no pointer meta) continues past the header: the
    // caret lands at the start of "Hello", on the side the key was heading.
    ed.view.dispatch(ed.state.tr.setSelection(TextSelection.create(ed.state.doc, nestedHeader + 2)));

    // Backspace at the start of "Hello" is handled as a no-op (the isolating
    // header above cannot be joined - the same barrier r5-3 pins), and the
    // caret stays out of the header.
    const handled = keydown(ed, "Backspace");
    expect(handled).toBe(true);
    const sel = ed.state.selection;
    expect(sel.$from.parent.type.name).not.toBe("mahoQuoteHeader");
    expect(isInsideHeader(sel.$from)).toBe(false);

    ed.commands.insertContent("Q");
    const html = ed.getHTML();
    expect(html).toContain(INNER_HEADER);
    expect(html).toContain("QHello");
  });

  it("the lead layout: a root transaction landing the caret at header offset 2 is pushed past it, so the next key is alive", () => {
    const ed = openEditor(`<blockquote><p>Quoted</p>${HEADER}<p>Hi</p></blockquote>`);
    ed.commands.setTextSelection(textblockEnd(ed, "Quoted"));
    // A key-driven arrival (no pointer meta) continues past the header: the
    // caret lands at the start of "Hi", on the side the key was heading.
    ed.view.dispatch(ed.state.tr.setSelection(TextSelection.create(ed.state.doc, headerStart(ed) + 2)));

    // Backspace at the start of "Hi" is handled as a no-op (the isolating
    // header above cannot be joined - the same barrier r5-3 pins), and the
    // caret stays out of the header.
    const handled = keydown(ed, "Backspace");
    expect(handled).toBe(true);
    const sel = ed.state.selection;
    expect(sel.$from.parent.type.name).not.toBe("mahoQuoteHeader");
    expect(isInsideHeader(sel.$from)).toBe(false);

    ed.commands.insertContent("Q");
    const html = ed.getHTML();
    expect(headerCount(html)).toBe(1);
    expect(html).toContain(HEADER);
    expect(html).toContain("<p>QHi</p>");
  });

  it("a selection-only transaction another plugin appends never relocates the caret", () => {
    const ed = openEditor(`<p>Body</p>${HEADER}${BLOCKQUOTE}`);
    placeCaretAsAppendedTransaction(ed, headerStart(ed) + 3);
    const sel = ed.state.selection;
    expect(sel.empty).toBe(true);
    expect(sel.$from.parent.type.name).toBe("mahoQuoteHeader");
    expect(sel.$from.parentOffset).toBe(3);
    expect(ed.getHTML()).toContain(HEADER);
  });

  it("a selection-only transaction outside a header leaves the caret where it was placed", () => {
    const ed = openEditor(`<p>Body</p>${HEADER}${BLOCKQUOTE}`);
    const at = textblockEnd(ed, "Body");
    ed.view.dispatch(ed.state.tr.setSelection(TextSelection.create(ed.state.doc, at)));
    expect(ed.state.selection.from).toBe(at);
    expect(ed.state.selection.$from.parent.type.name).toBe("paragraph");
  });

  it("a range dragged over the attribution is kept so the line can still be copied", () => {
    const ed = openEditor(`<p>Body</p>${HEADER}${BLOCKQUOTE}`);
    const start = headerStart(ed);
    ed.view.dispatch(ed.state.tr.setSelection(TextSelection.create(ed.state.doc, start, start + 6)));
    const sel = ed.state.selection;
    expect(sel.empty).toBe(false);
    expect(sel.from).toBe(start);
    expect(sel.to).toBe(start + 6);
    expect(ed.state.doc.textBetween(sel.from, sel.to)).toBe("On Mon");
    expect(ed.getHTML()).toContain(HEADER);
  });

  it("Delete at the end of the signature never leaves the caret in the header or the signature", () => {
    const layout = `<p>Body</p><div data-maho-signature="true" class="email-signature"><p>--<br>Old Sig</p></div>${HEADER}${BLOCKQUOTE}`;
    const ed = openEditor(layout);
    ed.commands.setTextSelection(textblockEnd(ed, "--"));
    expect(isInsideSignature(ed.state.selection.$from)).toBe(true);
    expect(keydown(ed, "Delete")).toBe(true);
    const sel = ed.state.selection;
    expect(sel).not.toBeInstanceOf(NodeSelection);
    expect(isInsideHeader(sel.$from)).toBe(false);
    expect(isInsideSignature(sel.$from)).toBe(false);
    ed.commands.insertContent("x");
    const html = ed.getHTML();
    expect(headerCount(html)).toBe(1);
    expect(html).toContain(HEADER);
    expect(html).toContain("Old Sig</p></div>");
  });
});

describe("quote header invariant I3: a header never lands in the user's signature", () => {
  it("inserting a header into the signature is rejected", () => {
    const layout = `<p>Body</p><div data-maho-signature="true" class="email-signature"><p>--<br>Old Sig</p></div>${HEADER}${BLOCKQUOTE}`;
    const ed = openEditor(layout);
    let sigLine = -1;
    ed.state.doc.descendants((node, pos) => {
      if (sigLine < 0 && node.isTextblock && node.textContent === "--Old Sig") sigLine = pos + 1;
    });
    ed.commands.setTextSelection(sigLine);
    const before = ed.getHTML();
    ed.chain().insertContentAt(sigLine, HEADER).run();
    expect(ed.getHTML()).toBe(before);
    expect(headerCount(ed.getHTML())).toBe(1);
  });
});

describe("quote header keyboard behavior that the invariant cannot express", () => {
  it("r6-1: Enter at the header end opens a plain paragraph after it and typing stays out of the header", () => {
    const ed = openEditor(`<p>Body</p>${OLD_SIG}${QUOTE}`);
    placeCaretAsAppendedTransaction(ed, headerEnd(ed));
    expect(keydown(ed, "Enter")).toBe(true);
    const { $from } = ed.state.selection;
    expect($from.parent.type.name).toBe("paragraph");
    expect($from.parentOffset).toBe(0);
    expect(ed.getHTML()).toContain(`${HEADER}<p></p><blockquote`);
    ed.commands.insertContent("Mine");
    const html = ed.getHTML();
    expect(html).toContain(`${HEADER}<p>Mine</p><blockquote`);
    // A signature swap still finds the quote below the signature.
    const swapped = replaceSignatureHtml(html, NEW_SIG, QUOTE);
    expect(swapped.indexOf("New Sig")).toBeLessThan(swapped.indexOf("On Mon"));
    expect(swapped.indexOf("On Mon")).toBeLessThan(swapped.indexOf("Mine"));
  });

  it("r6-1: Enter at the header start opens a plain paragraph before it", () => {
    const ed = openEditor(`<p>Body</p>${OLD_SIG}${QUOTE}`);
    placeCaretAsAppendedTransaction(ed, headerStart(ed));
    expect(keydown(ed, "Enter")).toBe(true);
    const { $from } = ed.state.selection;
    expect($from.parent.type.name).toBe("paragraph");
    ed.commands.insertContent("Lead");
    expect(ed.getHTML()).toContain(`<p>Lead</p>${HEADER}<blockquote`);
  });

  it("r6-1: Enter in the middle of the header keeps its text whole", () => {
    const ed = openEditor(`<p>Body</p>${OLD_SIG}${QUOTE}`);
    placeCaretAsAppendedTransaction(ed, headerStart(ed) + 3);
    expect(keydown(ed, "Enter")).toBe(true);
    const html = ed.getHTML();
    expect(headerCount(html)).toBe(1);
    expect(html).toContain(`${HEADER}<p></p><blockquote`);
    expect(ed.state.selection.$from.parent.type.name).toBe("paragraph");
  });

  it("r5-2: Backspace at the start of a populated header is an unhandled no-op", () => {
    const ed = openEditor(`<p>Body</p>${OLD_SIG}${QUOTE}`);
    placeCaretAsAppendedTransaction(ed, headerStart(ed));
    expect(keydown(ed, "Backspace")).toBe(false);
    const html = ed.getHTML();
    expect(headerCount(html)).toBe(1);
    expect(html).toContain(HEADER);
  });

  it("Backspace in an emptied header removes it and leaves the caret outside the quote", () => {
    const ed = openEditor(`<p>Body</p>${QUOTE}`);
    const start = headerStart(ed);
    expect(ed.chain().setTextSelection({ from: start, to: headerEnd(ed) }).deleteSelection().run()).toBe(true);
    expect(ed.chain().setTextSelection(start).run()).toBe(true);
    expect(keydown(ed, "Backspace")).toBe(true);
    expect(ed.getHTML()).not.toContain('<p data-maho-quote="true">');
    expect(ed.getHTML()).toContain('<blockquote data-maho-quote="true">');
    const { $from } = ed.state.selection;
    expect(ed.state.selection.empty).toBe(true);
    expect($from.parent.type.name).toBe("paragraph");
    expect(isInsideBlockquote($from)).toBe(false);
    expect(isInsideHeader($from)).toBe(false);
    ed.commands.insertContent("Z");
    const html = ed.getHTML();
    expect(html).toContain('<blockquote data-maho-quote="true"><p>Original</p></blockquote>');
    expect(html.indexOf("Z")).toBeLessThan(html.indexOf("<blockquote"));
  });

  it("r5-3: with the header as the first block, Backspace twice keeps its text and typing lands in a paragraph", () => {
    const ed = openEditor(`${HEADER}<p>Hi</p>${BLOCKQUOTE}`);
    ed.commands.setTextSelection(textblockStart(ed, "Hi"));
    expect(keydown(ed, "Backspace")).toBe(true);
    expect(isInsideHeader(ed.state.selection.$from)).toBe(false);
    expect(keydown(ed, "Backspace")).toBe(true);
    expect(isInsideHeader(ed.state.selection.$from)).toBe(false);
    expect(ed.getHTML()).toContain(HEADER);

    ed.commands.insertContent("Q");
    const html = ed.getHTML();
    expect(headerCount(html)).toBe(1);
    expect(html).toContain(HEADER);
    expect(html).toContain("QHi");
    expect(ed.state.selection.$from.parent.type.name).toBe("paragraph");
  });

  it("Backspace in a leading emptied header creates an editable paragraph before the quote", () => {
    const ed = openEditor(`${HEADER}${BLOCKQUOTE}`);
    const start = headerStart(ed);
    expect(ed.chain().setTextSelection({ from: start, to: headerEnd(ed) }).deleteSelection().run()).toBe(true);
    expect(ed.chain().setTextSelection(start).run()).toBe(true);
    expect(keydown(ed, "Backspace")).toBe(true);
    expect(ed.getHTML()).not.toContain('<p data-maho-quote="true">');
    const { $from } = ed.state.selection;
    expect($from.parent.type.name).toBe("paragraph");
    expect($from.depth).toBe(1);
    expect($from.pos).toBe(1);
    ed.commands.insertContent("Z");
    // (The trailing empty paragraph is deleteSelection's doing, pre-existing.)
    expect(ed.getHTML().startsWith(`<p>Z</p>${BLOCKQUOTE}`)).toBe(true);
  });

  it("r7-3: removing an emptied header after the signature leaves the caret outside the header and the signature", () => {
    const layout =
      `<p>Body</p><div data-maho-signature="true" class="email-signature"><p>--<br>Old Sig</p></div>${HEADER}${BLOCKQUOTE}`;
    const ed = openEditor(layout);
    const start = headerStart(ed);
    expect(ed.chain().setTextSelection({ from: start, to: headerEnd(ed) }).deleteSelection().run()).toBe(true);
    expect(ed.chain().setTextSelection(start).run()).toBe(true);
    expect(ed.state.selection.$from.parent.type.name).toBe("mahoQuoteHeader");
    expect(keydown(ed, "Backspace")).toBe(true);
    expect(ed.getHTML()).not.toContain('<p data-maho-quote="true">');
    const { $from } = ed.state.selection;
    expect(ed.state.selection.empty).toBe(true);
    expect($from.depth).toBe(1);
    expect($from.parent.type.name).toBe("paragraph");
    expect(isInsideSignature($from)).toBe(false);
    expect(isInsideBlockquote($from)).toBe(false);
    ed.commands.insertContent("Thanks!");
    const html = ed.getHTML();
    expect(html).toContain("Old Sig</p></div><p>Thanks!</p><blockquote");
    const swapped = replaceSignatureHtml(html, NEW_SIG, QUOTE);
    expect(swapped).not.toContain("Old Sig");
    expect(swapped).toContain("Thanks!");
    expect(swapped).toContain('<blockquote data-maho-quote="true"><p>Original</p></blockquote>');
  });

  it("r7-1: Backspace removes the blank line Enter opened after the header", () => {
    const ed = openEditor(`<p>Body</p>${OLD_SIG}${QUOTE}`);
    placeCaretAsAppendedTransaction(ed, headerEnd(ed));
    expect(keydown(ed, "Enter")).toBe(true);
    expect(ed.getHTML()).toContain(`${HEADER}<p></p><blockquote`);
    expect(keydown(ed, "Backspace")).toBe(true);
    const fresh = openEditor(`<p>Body</p>${OLD_SIG}${QUOTE}`);
    const reopened = fresh.getHTML();
    fresh.destroy();
    editor = ed;
    expect(ed.getHTML()).toBe(`${reopened}<p></p>`);
    const { $from } = ed.state.selection;
    expect(ed.state.selection.empty).toBe(true);
    expect(isInsideHeader($from)).toBe(false);
    expect(isInsideSignature($from)).toBe(false);
  });

  it("r7-1: Delete removes a blank line directly above the header", () => {
    const ed = openEditor(`<p>Thanks</p><p></p>${HEADER}${BLOCKQUOTE}`);
    ed.commands.setTextSelection(headerStart(ed) - 2);
    expect(ed.state.selection.$from.parent.content.size).toBe(0);
    expect(keydown(ed, "Delete")).toBe(true);
    expect(ed.getHTML()).toBe(`<p>Thanks</p>${HEADER}${BLOCKQUOTE}<p></p>`);
    expect(ed.state.selection.$from.parent.type.name).not.toBe("blockquote");
    expect(isInsideHeader(ed.state.selection.$from)).toBe(false);
  });

  it("select-all then delete clears the message", () => {
    const ed = openEditor(`<p>Body</p>${OLD_SIG}${QUOTE}`);
    ed.commands.selectAll();
    ed.commands.deleteSelection();
    expect(ed.getHTML()).toBe("<p></p>");
    expect(headerCount(ed.getHTML())).toBe(0);
    expect(ed.state.selection.$from.parent.type.name).toBe("paragraph");
  });

  it("undo after emptying a header restores its text instead of being blocked", () => {
    const ed = openEditor(`<p>Body</p>${HEADER}${BLOCKQUOTE}`);
    const start = headerStart(ed);
    ed.chain().setTextSelection({ from: start, to: headerEnd(ed) }).deleteSelection().run();
    expect(ed.getHTML()).toContain('<p data-maho-quote="true"></p>');
    ed.commands.undo();
    expect(ed.getHTML()).toContain(HEADER);
    expect(headerCount(ed.getHTML())).toBe(1);
  });
});

const NESTED_QUOTE =
  `<p>Body</p>${HEADER}<blockquote data-maho-quote="true">` +
  `<p>Reply1</p>${INNER_HEADER}` +
  '<blockquote data-maho-quote="true"><p>Oldest</p></blockquote></blockquote>';

/** Empties the nested "On Sun" header and puts the caret in it. */
function emptyInnerHeader(ed: Editor): void {
  const innerStart = textblockStart(ed, "On Sun");
  expect(
    ed.chain().setTextSelection({ from: innerStart, to: innerStart + "On Sun, c@d.e wrote:".length }).deleteSelection().run(),
  ).toBe(true);
  expect(ed.chain().setTextSelection(innerStart).run()).toBe(true);
  expect(ed.state.selection.$from.parent.type.name).toBe("mahoQuoteHeader");
}

describe("quote header in a reply-to-reply (r8/r9 regressions)", () => {
  it("r8-1: Backspace removes the blank line Enter opened after a nested header without splitting the quote", () => {
    const ed = openEditor(NESTED_QUOTE);
    const innerEnd = textblockEnd(ed, "On Sun");
    placeCaretAsAppendedTransaction(ed, innerEnd);
    expect(keydown(ed, "Enter")).toBe(true);
    expect(keydown(ed, "Backspace")).toBe(true);
    // (StarterKit's trailing-node plugin keeps an empty paragraph at the doc end.)
    expect(ed.getHTML()).toBe(`${NESTED_QUOTE}<p></p>`);
    const sel = ed.state.selection;
    expect(sel).toBeInstanceOf(TextSelection);
    expect(sel.empty).toBe(true);
    expect(sel.from).toBe(textblockEnd(ed, "Reply1"));
    expect(isInsideHeader(sel.$from)).toBe(false);
  });

  it("r8-2: Backspace in an emptied nested header moves the caret to the previous quoted line and keeps the quote whole", () => {
    const ed = openEditor(NESTED_QUOTE);
    emptyInnerHeader(ed);
    expect(keydown(ed, "Backspace")).toBe(true);
    const withoutInnerHeader = NESTED_QUOTE.replace(INNER_HEADER, "");
    expect(ed.getHTML()).toBe(`${withoutInnerHeader}<p></p>`);
    const sel = ed.state.selection;
    expect(sel).toBeInstanceOf(TextSelection);
    expect(sel.empty).toBe(true);
    expect(sel.from).toBe(textblockEnd(ed, "Reply1"));
    expect(keydown(ed, "Backspace")).toBe(false);
    expect(ed.getHTML()).toBe(`${withoutInnerHeader}<p></p>`);
  });

  it.each([
    [
      "a quoted signature",
      `<p>Body</p>${HEADER}<blockquote data-maho-quote="true">` +
        `<p>Reply1</p><div data-maho-signature="true" class="email-signature"><p>--<br>Alice</p></div>` +
        `${INNER_HEADER}<blockquote data-maho-quote="true"><p>Oldest</p></blockquote></blockquote>`,
      "--",
    ],
    [
      "a nested blockquote",
      `<p>Body</p>${HEADER}<blockquote data-maho-quote="true">` +
        `<p>Reply1</p><blockquote><p>q</p></blockquote>` +
        `${INNER_HEADER}<blockquote data-maho-quote="true"><p>Oldest</p></blockquote></blockquote>`,
      "q",
    ],
  ])("r9-1: Backspace in an emptied nested header after %s keeps the quote whole", (_, html, lastLine) => {
    const ed = openEditor(html);
    expect(ed.getHTML()).toBe(html);
    emptyInnerHeader(ed);
    expect(keydown(ed, "Backspace")).toBe(true);
    const withoutInnerHeader = html.replace(INNER_HEADER, "");
    expect(ed.getHTML()).toBe(`${withoutInnerHeader}<p></p>`);
    const sel = ed.state.selection;
    expect(sel).toBeInstanceOf(TextSelection);
    expect(sel.empty).toBe(true);
    expect(sel.from).toBe(textblockEnd(ed, lastLine));
    expect(keydown(ed, "Backspace")).toBe(false);
    expect(ed.getHTML()).toBe(`${withoutInnerHeader}<p></p>`);
  });

  it("r9-2: Backspace removes the blank line Enter opened before a nested header without splitting the quote", () => {
    const ed = openEditor(NESTED_QUOTE);
    const innerStart = textblockStart(ed, "On Sun");
    placeCaretAsAppendedTransaction(ed, innerStart);
    expect(keydown(ed, "Enter")).toBe(true);
    expect(ed.getHTML()).toContain(`<p>Reply1</p><p></p>${INNER_HEADER}`);
    expect(keydown(ed, "Backspace")).toBe(true);
    expect(ed.getHTML()).toBe(`${NESTED_QUOTE}<p></p>`);
    const sel = ed.state.selection;
    expect(sel).toBeInstanceOf(TextSelection);
    expect(sel.empty).toBe(true);
    expect(sel.from).toBe(textblockEnd(ed, "Reply1"));
    expect(isInsideHeader(sel.$from)).toBe(false);
  });

  it("r9-3: Backspace in an emptied nested header that opens the quote leaves an editable line outside every header", () => {
    const html =
      `<p>Body</p>${HEADER}<blockquote data-maho-quote="true">` +
      `${INNER_HEADER}<blockquote data-maho-quote="true"><p>Oldest</p></blockquote></blockquote>`;
    const ed = openEditor(html);
    emptyInnerHeader(ed);
    expect(keydown(ed, "Backspace")).toBe(true);
    expect(ed.getHTML()).toBe(
      `<p>Body</p>${HEADER}<blockquote data-maho-quote="true"><p></p>` +
        '<blockquote data-maho-quote="true"><p>Oldest</p></blockquote></blockquote><p></p>',
    );
    const sel = ed.state.selection;
    expect(sel).toBeInstanceOf(TextSelection);
    expect(sel.empty).toBe(true);
    expect(isInsideHeader(sel.$from)).toBe(false);
    ed.commands.insertContent("Mine");
    expect(ed.getHTML()).toContain(`<blockquote data-maho-quote="true"><p>Mine</p><blockquote`);
  });
});

/**
 * A real ArrowDown/ArrowUp cannot be driven here: prosemirror-view's
 * endOfTextblock measures the DOM (coordsAtPos -> getClientRects) and jsdom has
 * no layout, so the keydown crashes before any handler runs. What the browser
 * dispatches for a vertical arrow is a root, selection-only, meta-less
 * transaction holding the caret it computed (readDOMChange,
 * prosemirror-view dist:5093-5110); dispatching that transaction directly is
 * the exact equivalent. A click arrives through the same path but carrying the
 * `pointer` meta prosemirror-view sets at dist:5106-5107.
 */
function dispatchKeyDrivenCaretIntoHeader(ed: Editor): void {
  ed.view.dispatch(ed.state.tr.setSelection(TextSelection.create(ed.state.doc, headerStart(ed) + 3)));
}

describe("quote header keyboard traversal (r10): a key crosses the attribution, a click returns", () => {
  const REPLY = `<p>Body</p>${HEADER}${BLOCKQUOTE}`;

  it("r10-1: a key-driven move down from the user's text lands past the header, and typing works there", () => {
    const ed = openEditor(REPLY);
    ed.commands.setTextSelection(textblockEnd(ed, "Body"));
    const origin = ed.state.selection.from;
    dispatchKeyDrivenCaretIntoHeader(ed);
    const sel = ed.state.selection;
    expect(sel.empty).toBe(true);
    expect(isInsideHeader(sel.$from)).toBe(false);
    expect(sel.from).not.toBe(origin);
    expect(sel.from).toBe(textblockStart(ed, "Original"));
    ed.commands.insertContent("Q");
    const html = ed.getHTML();
    expect(headerCount(html)).toBe(1);
    expect(html).toContain(HEADER);
    expect(html).toContain("<p>QOriginal</p>");
  });

  it("r10-1: a key-driven move up from the quoted line lands before the header, and typing works there", () => {
    const ed = openEditor(REPLY);
    ed.commands.setTextSelection(textblockStart(ed, "Original"));
    const origin = ed.state.selection.from;
    dispatchKeyDrivenCaretIntoHeader(ed);
    const sel = ed.state.selection;
    expect(sel.empty).toBe(true);
    expect(isInsideHeader(sel.$from)).toBe(false);
    expect(sel.from).not.toBe(origin);
    expect(sel.from).toBe(textblockEnd(ed, "Body"));
    ed.commands.insertContent("Q");
    const html = ed.getHTML();
    expect(headerCount(html)).toBe(1);
    expect(html).toContain(HEADER);
    expect(html).toContain("<p>BodyQ</p>");
  });

  it("r10-1: a pointer placement inside the header still returns to the side it came from", () => {
    const ed = openEditor(REPLY);
    ed.commands.setTextSelection(textblockEnd(ed, "Body"));
    const fromAbove = ed.state.selection.from;
    const down = ed.state.tr.setSelection(TextSelection.create(ed.state.doc, headerStart(ed) + 3));
    down.setMeta("pointer", true);
    ed.view.dispatch(down);
    expect(ed.state.selection.from).toBe(fromAbove);

    ed.commands.setTextSelection(textblockStart(ed, "Original"));
    const fromBelow = ed.state.selection.from;
    const up = ed.state.tr.setSelection(TextSelection.create(ed.state.doc, headerStart(ed) + 3));
    up.setMeta("pointer", true);
    ed.view.dispatch(up);
    expect(ed.state.selection.from).toBe(fromBelow);
    expect(ed.getHTML()).toContain(HEADER);
  });
});
