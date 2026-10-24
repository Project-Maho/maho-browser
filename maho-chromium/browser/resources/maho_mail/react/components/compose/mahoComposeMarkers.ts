import { Extension, Node, mergeAttributes } from "@tiptap/core";
import { Fragment, Node as ProseMirrorNode, ResolvedPos, Slice } from "@tiptap/pm/model";
import { EditorState, NodeSelection, Plugin, TextSelection, Transaction } from "@tiptap/pm/state";

/**
 * Keeps the compose structural markers alive through Tiptap serialization.
 * StarterKit has no `div` node and drops unknown attributes, so without these
 * the signature block and quote lose `data-maho-signature` / `data-maho-quote`
 * on the first edit and a signature swap can no longer find the old signature.
 */
export const MahoSignatureBlock = Node.create({
  name: "mahoSignature",
  group: "block",
  content: "block+",
  defining: true,

  parseHTML() {
    return [{ tag: "div[data-maho-signature]" }];
  },

  renderHTML({ HTMLAttributes }) {
    return [
      "div",
      mergeAttributes(HTMLAttributes, { "data-maho-signature": "true", class: "email-signature" }),
      0,
    ];
  },
});

const HEADER_NODE = "mahoQuoteHeader";
const SIGNATURE_NODE = "mahoSignature";

function enclosingPos($pos: ResolvedPos, typeName: string): number | null {
  for (let depth = $pos.depth; depth > 0; depth--) {
    if ($pos.node(depth).type.name === typeName) return $pos.before(depth);
  }
  return null;
}

/**
 * Position of the enclosing QUOTED blockquote - the one carrying
 * `data-maho-quote`, i.e. the quoted message - or null.
 *
 * Deliberately not every blockquote: the compose toolbar has a blockquote
 * control and the `> ` input rule creates them too, and those carry no marker.
 * A blockquote the user authored is their own formatting, and Backspace at the
 * start of its first line is exactly how that formatting is removed. The
 * barriers below exist to protect the quoted message, so they must not reach
 * into a blockquote the user made.
 */
function enclosingQuotedBlockquote($pos: ResolvedPos): number | null {
  for (let depth = $pos.depth; depth > 0; depth--) {
    const node = $pos.node(depth);
    if (node.type.name === "blockquote" && node.attrs.mahoQuote) {
      return $pos.before(depth);
    }
  }
  return null;
}

function clampToDoc(doc: ProseMirrorNode, pos: number): number {
  return Math.max(0, Math.min(pos, doc.content.size));
}

/**
 * True for the signature the user owns. A signature inside the quoted message
 * belongs to the quoted sender and is ordinary quoted text.
 */
function isInsideOwnSignature(doc: ProseMirrorNode, pos: number): boolean {
  const $pos = doc.resolve(clampToDoc(doc, pos));
  for (let depth = $pos.depth; depth > 0; depth--) {
    if ($pos.node(depth).type.name !== SIGNATURE_NODE) continue;
    for (let outer = depth - 1; outer > 0; outer--) {
      if ($pos.node(outer).type.name === "blockquote") return false;
    }
    return true;
  }
  return false;
}

type HeaderAt = { pos: number; text: string };

function headersIn(doc: ProseMirrorNode): HeaderAt[] {
  const found: HeaderAt[] = [];
  doc.descendants((node, pos) => {
    if (node.type.name !== HEADER_NODE) return true;
    found.push({ pos, text: node.textContent });
    return false;
  });
  return found;
}

function nodeStartingAt(doc: ProseMirrorNode, pos: number): ProseMirrorNode | null {
  if (pos < 0 || pos > doc.content.size) return null;
  return doc.resolve(pos).nodeAfter;
}

// A load or a signature swap, not an edit of the text the invariant protects.
function replacesWholeDoc(tr: Transaction, oldDoc: ProseMirrorNode): boolean {
  if (tr.steps.length !== 1) return false;
  const step = tr.steps[0] as unknown as { from?: number; to?: number };
  return step.from === 0 && step.to === oldDoc.content.size;
}

/**
 * I1. A header that survives a transaction must keep the text it had. Emptying
 * a header, and text coming back into an emptied one, are the two halves of the
 * removal gesture (empty, then Backspace) and of undoing it, so they are not a
 * rewrite of an attribution.
 */
function rewritesHeaderText(tr: Transaction, oldDoc: ProseMirrorNode): boolean {
  const inverse = tr.mapping.invert();
  for (const header of headersIn(tr.doc)) {
    const before = nodeStartingAt(oldDoc, inverse.map(header.pos, -1));
    if (!before || before.type.name !== HEADER_NODE) continue;
    const text = before.textContent;
    if (text === header.text || text === "" || header.text === "") continue;
    return true;
  }
  return false;
}

function ownSignatureHeaderTexts(doc: ProseMirrorNode): string[] {
  return headersIn(doc)
    .filter((header) => isInsideOwnSignature(doc, header.pos))
    .map((header) => header.text);
}

/**
 * I3. No edit may put a header inside the signature the user owns. A draft
 * saved before the header became its own node can already hold one there, and
 * it stays legal for that header to survive (and be removed) untouched.
 */
function addsHeaderToSignature(tr: Transaction, oldDoc: ProseMirrorNode): boolean {
  const before = ownSignatureHeaderTexts(oldDoc);
  for (const text of ownSignatureHeaderTexts(tr.doc)) {
    const seen = before.indexOf(text);
    if (seen < 0) return true;
    before.splice(seen, 1);
  }
  return false;
}

// Nearest position outside a header and the user's signature, or null.
function nearestEditablePos(doc: ProseMirrorNode, from: number, dir: 1 | -1): number | null {
  let cursor = from;
  for (let guard = 0; guard < 64; guard++) {
    if (cursor < 0 || cursor > doc.content.size) return null;
    const near = TextSelection.near(doc.resolve(cursor), dir);
    if (!near) return null;
    const at = near.from;
    const $at = doc.resolve(at);
    const headerPos = enclosingPos($at, HEADER_NODE);
    const blocked =
      headerPos ?? (isInsideOwnSignature(doc, at) ? enclosingPos($at, SIGNATURE_NODE) : null);
    if (blocked === null) return at;
    const node = doc.nodeAt(blocked);
    if (!node) return null;
    const jump = dir < 0 ? blocked - 1 : blocked + node.nodeSize;
    if (jump === cursor) return null;
    cursor = jump;
  }
  return null;
}

/**
 * I2. No root transaction may leave the caret inside a populated header, and
 * none may leave a node selection on a header: both are states in which the
 * next keystroke is dead, because I1 rejects the edit and the caret does not
 * move. A bare caret placement counts - a click, an arrow key, a command, the
 * DOM selection a browser restores after its own Backspace merge attempt - so
 * the gate is the transaction's origin, not a document change. A range the
 * user dragged over the attribution is not a caret and is left alone. An emptied
 * header is left alone: Enter and the emptied-header Backspace are the two
 * actions that legitimately start inside one, and both move the caret out
 * themselves.
 *
 * Where the caret goes depends on how it arrived. A pointer placement and a
 * node selection return to the side they came from - the side the previous
 * caret was on - so a click on the attribution does not throw the caret across
 * it, and a Backspace that could not merge or a Delete that could not join both
 * end where the keystroke was aiming. A key-driven arrival instead continues
 * PAST the header on the side the key was heading: returning it to the origin
 * would make the key a no-op, so Up and Down could never cross the attribution
 * (prosemirror-view marks pointer-derived selection transactions with a
 * `pointer` meta; the transactions behind arrow keys carry no meta).
 */
function relocateOutOfHeader(
  state: EditorState,
  oldState: EditorState,
  options: { fromRootTransaction: boolean; pointerPlacement: boolean },
): Transaction | null {
  if (!options.fromRootTransaction) return null;
  const { selection, doc } = state;
  const onHeader = selection instanceof NodeSelection && selection.node.type.name === HEADER_NODE;
  let headerPos: number | null;
  if (onHeader) {
    headerPos = selection.from;
  } else {
    // A range the user dragged inside the attribution is kept: it cannot edit
    // anything (I1 rejects that) and it is how the attribution gets copied.
    if (!selection.empty) return null;
    headerPos = enclosingPos(doc.resolve(selection.head), HEADER_NODE);
    if (headerPos !== null && doc.nodeAt(headerPos)?.content.size === 0) return null;
  }
  if (headerPos === null) return null;
  const header = doc.nodeAt(headerPos);
  if (!header) return null;

  const before = headerPos;
  const after = headerPos + header.nodeSize;
  const oldHead = oldState.selection.head;
  const sideReturn = onHeader || options.pointerPlacement;
  const forward = sideReturn ? oldHead > after : oldHead <= before;
  const target =
    nearestEditablePos(doc, forward ? after : before - 1, forward ? 1 : -1) ??
    nearestEditablePos(doc, forward ? before - 1 : after, forward ? -1 : 1);
  if (target !== null) {
    return state.tr.setSelection(TextSelection.create(doc, target)).scrollIntoView();
  }

  // A relocation must never change the document; only the emptied-header path,
  // which arrives with a document change, may create the paragraph it needs.
  if (state.doc === oldState.doc) return null;
  const tr = state.tr.insert(after, state.schema.nodes.paragraph.create());
  return tr.setSelection(TextSelection.create(tr.doc, after + 1)).scrollIntoView();
}

function containsHeader(fragment: Fragment): boolean {
  for (let i = 0; i < fragment.childCount; i++) {
    const child = fragment.child(i);
    if (child.type.name === HEADER_NODE || containsHeader(child.content)) return true;
  }
  return false;
}

/**
 * Pasted or dropped content never becomes an attribution line: a header in the
 * slice turns into a plain paragraph holding its text, so it cannot smuggle a
 * second "On ... wrote:" line into the message.
 */
function stripHeadersFromSlice(slice: Slice): Slice {
  if (!containsHeader(slice.content)) return slice;
  const convert = (fragment: Fragment): Fragment =>
    Fragment.fromArray(
      fragment.content.map((child) =>
        child.type.name === HEADER_NODE
          ? child.type.schema.nodes.paragraph.create(null, child.content)
          : child.isLeaf
            ? child
            : child.copy(convert(child.content)),
      ),
    );
  return new Slice(convert(slice.content), slice.openStart, slice.openEnd);
}

function createHeaderInvariant(): Plugin {
  return new Plugin({
    filterTransaction(tr, state) {
      if (!tr.docChanged) return true;
      if (replacesWholeDoc(tr, state.doc)) return true;
      return !rewritesHeaderText(tr, state.doc) && !addsHeaderToSignature(tr, state.doc);
    },
    appendTransaction(transactions, oldState, newState) {
      if (!transactions.some((tr) => tr.docChanged || tr.selectionSet)) return null;
      // A transaction another plugin appended (StarterKit's trailing node, for
      // one) is housekeeping, not the edit whose caret this rule is about. The
      // gate is the transaction's ORIGIN, not whether it changed the document:
      // a root transaction moves the caret into the header without touching the
      // document too (a click, an arrow key, the DOM selection a browser
      // restores after its own Backspace merge attempt), and I1 then blocks
      // typing there, which makes the key dead.
      const fromRootTransaction = transactions.some((tr) => !tr.getMeta("appendedTransaction"));
      // prosemirror-view sets `pointer` on the selection transaction it derives
      // from a pointer input; a key-driven caret move carries no meta.
      const pointerPlacement = transactions.some(
        (tr) => !tr.getMeta("appendedTransaction") && tr.getMeta("pointer"),
      );
      return relocateOutOfHeader(newState, oldState, { fromRootTransaction, pointerPlacement });
    },
    props: {
      transformPasted: (slice) => stripHeadersFromSlice(slice),
    },
  });
}

/**
 * The "On ... wrote:" attribution line of a reply/forward quote. It is its own
 * node type instead of a marked paragraph so its identity is structural:
 * `isolating` stops Backspace/Delete from joining it with the user's text or
 * the signature on either side, and Enter at its end opens a plain paragraph.
 *
 * The invariant plugin is what makes the attribution immutable: no edit from
 * any source may rewrite its text, and the caret never comes to rest inside it
 * (see `rewritesHeaderText` / `relocateOutOfHeader`). The keyboard handlers only
 * keep the two structural actions the invariant cannot express.
 */
export const MahoQuoteHeader = Node.create({
  name: "mahoQuoteHeader",
  group: "block",
  content: "inline*",
  defining: true,
  isolating: true,

  parseHTML() {
    // Outranks StarterKit's plain `p` rule.
    return [{ tag: "p[data-maho-quote]", priority: 60 }];
  },

  renderHTML({ HTMLAttributes }) {
    return ["p", mergeAttributes(HTMLAttributes, { "data-maho-quote": "true" }), 0];
  },

  addProseMirrorPlugins() {
    return [createHeaderInvariant()];
  },

  addKeyboardShortcuts() {
    return {
      // An isolating block cannot be split (prosemirror-transform canSplit
      // rejects any split whose parent is isolating), so the core Enter
      // chain always fails inside the header. Open a plain paragraph
      // outside the header instead: before it when the caret is at the
      // very start, after it otherwise. The header text is never split.
      Enter: ({ editor }) => {
        const { empty, $from, $to } = editor.state.selection;
        if ($from.parent.type.name !== this.name || $to.parent.type.name !== this.name) return false;
        return editor.commands.command(({ tr, dispatch }) => {
          if (dispatch) {
            if (!empty) tr.delete($from.pos, $to.pos);
            const $cursor = tr.selection.$from;
            const at = $cursor.parentOffset === 0 ? $cursor.before() : $cursor.after();
            tr.insert(at, editor.schema.nodes.paragraph.create());
            tr.setSelection(TextSelection.create(tr.doc, at + 1)).scrollIntoView();
          }
          return true;
        });
      },
      Backspace: ({ editor }) => {
        const { empty, $from } = editor.state.selection;
        if (!empty) return false;
        if ($from.parent.type.name === this.name) {
          if ($from.parent.content.size > 0) return false;
          // An isolating block cannot be joined away, so an emptied header
          // is removed explicitly. The caret goes to the end of the line
          // directly above it in the same container (for a nested header,
          // the previous quoted line), so a further Backspace edits text
          // instead of lifting a stray line out of the quote. Inside a
          // container with no line directly above (the header opens the
          // quote, or follows a quoted signature or nested quote), the caret
          // goes to the nearest text before it, which is still part of the
          // quoted thread. It never lands inside the user's own signature (a
          // swap would delete what is typed there) nor inside another header;
          // then, and at top level after a container, a paragraph is created
          // in the header's place.
          return editor.commands.command(({ tr, dispatch }) => {
            if (dispatch) {
              const from = $from.before();
              tr.delete(from, $from.after());
              const $at = tr.doc.resolve(from);
              const backward =
                $at.depth > 0 && !$at.nodeBefore?.isTextblock ? TextSelection.findFrom($at, -1, true) : null;
              const backwardIsHeader =
                backward !== null && enclosingPos(backward.$from, HEADER_NODE) !== null;
              if ($at.nodeBefore?.isTextblock && $at.parent.type.name !== MahoSignatureBlock.name) {
                tr.setSelection(TextSelection.create(tr.doc, from - 1));
              } else if (backward && !backwardIsHeader && backward.$from.node(1).type.name !== MahoSignatureBlock.name) {
                tr.setSelection(backward);
              } else {
                tr.insert(from, editor.schema.nodes.paragraph.create());
                tr.setSelection(TextSelection.create(tr.doc, from + 1));
              }
              tr.scrollIntoView();
            }
            return true;
          });
        }
        // An empty line next to a header is removed here rather than by the
        // keymap: inside a quote the blockquote extension's Backspace lifts
        // that line out of the quote and splits the quoted thread in two. The
        // caret goes back to the nearest editable text the invariant allows,
        // so the next keystroke cannot land in the attribution.
        if (
          $from.parent.isTextblock &&
          $from.parentOffset === 0 &&
          $from.parent.content.size === 0
        ) {
          const $before = editor.state.doc.resolve($from.before());
          const $after = editor.state.doc.resolve($from.after());
          if ($before.nodeBefore?.type.name === this.name || $after.nodeAfter?.type.name === this.name) {
            return editor.commands.command(({ tr, dispatch }) => {
              if (dispatch) {
                const at = $from.before();
                tr.delete(at, $from.after());
                const back = nearestEditablePos(tr.doc, at, -1);
                if (back !== null) tr.setSelection(TextSelection.create(tr.doc, back));
              }
              return true;
            });
          }
        }
        // Backspace at the start of a quoted line that cannot merge upward:
        // the line directly above is either the attribution (isolating) or
        // nothing at all, because the line opens the quote. The keymap's
        // joinBackward cannot merge either way, so it falls through to
        // deleteBarrier, whose lift branch pulls the line OUT of the quote -
        // splitting the quoted thread in two, and deleting the blockquote
        // outright when that line was its only child. r10 routes the caret
        // here on purpose (a key-driven arrival crosses past the attribution),
        // so the keystroke has to be safe rather than destructive.
        //
        // The key is consumed as a pure barrier: the document and the caret
        // both stay exactly where they are. Relocating the caret backwards past
        // the attribution was the alternative, and it is worse - the next
        // character typed would land on a different line from the one the user
        // is looking at, so a keystroke that deleted nothing would silently
        // move the insertion point. Nothing to delete here is not an error, and
        // an inert key at a structural boundary is the honest result. A line
        // whose previous sibling is ordinary text is NOT intercepted: that
        // merge is the normal, expected Backspace.
        if ($from.parent.isTextblock && $from.parentOffset === 0 && $from.depth > 0) {
          const $container = $from.node($from.depth - 1);
          const index = $from.index($from.depth - 1);
          const previous = index > 0 ? $container.child(index - 1) : null;
          if (enclosingQuotedBlockquote($from) !== null) {
            if (previous === null) {
              // An EMPTY line that opens the quote: there is no text to merge
              // with, but the user is still asking to remove a line, and
              // removing an empty line destroys nothing. A pure barrier here
              // would leave the line permanently undeletable, so the empty
              // line is removed and the caret goes to the nearest editable text
              // before it. Relocating is correct in THIS case precisely because
              // something really was deleted - the objection to relocating is
              // that a key which deletes nothing must not move the caret.
              //
              // Unless it is the container's ONLY child: a blockquote must hold
              // at least one block, so the line would be re-created and the
              // user would see nothing change while the caret jumped out of the
              // quote - exactly the silent move this branch exists to avoid.
              // Then it is a barrier.
              if ($from.parent.content.size === 0 && $container.childCount > 1) {
                return editor.commands.command(({ tr, dispatch }) => {
                  if (dispatch) {
                    const at = $from.before();
                    tr.delete(at, $from.after());
                    const back = nearestEditablePos(tr.doc, at, -1);
                    if (back !== null) tr.setSelection(TextSelection.create(tr.doc, back));
                  }
                  return true;
                });
              }
              return true;
            }
            if (previous.type.name === this.name) return true;
            // The line above is ordinary quoted text, so the key means "join
            // these two lines". The keymap does not merge them: it lifts this
            // line out of the quote instead, which cuts the quoted message in
            // two at the caret. Join explicitly so the key does what it says.
            //
            // Only a PARAGRAPH joins into a paragraph: `tr.join` merges the
            // caret's line into whatever type precedes it, so joining after a
            // heading or a code block would silently turn that block into
            // plain text (or turn the caret's line into code). A quoted
            // technical mail with a <pre> is ordinary, and pasted HTML brings
            // headings in. For those the key stays a barrier.
            if (
              previous.isTextblock &&
              previous.type.name === $from.parent.type.name &&
              previous.type.name === "paragraph"
            ) {
              return editor.commands.command(({ tr, dispatch }) => {
                if (dispatch) tr.join($from.before());
                return true;
              });
            }
            // The line above is a NESTED QUOTE, not text: there is nothing to
            // join it with, and leaving it to the keymap lifts this line out of
            // the quote - the same destruction as the header case, one level
            // down. Barrier.
            return true;
          }
        }
        return false;
      },
    };
  },
});

/** Keeps `data-maho-quote` on the quoted-body blockquote. */
export const MahoQuoteMarker = Extension.create({
  name: "mahoQuoteMarker",

  addGlobalAttributes() {
    return [
      {
        types: ["blockquote"],
        attributes: {
          mahoQuote: {
            default: null,
            parseHTML: (element) => element.getAttribute("data-maho-quote"),
            renderHTML: (attributes) =>
              attributes.mahoQuote ? { "data-maho-quote": attributes.mahoQuote } : {},
          },
        },
      },
    ];
  },
});

export const mahoComposeMarkers = [MahoSignatureBlock, MahoQuoteHeader, MahoQuoteMarker];
