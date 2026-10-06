/**
 * Bounded TeX -> MathML conversion for assistant responses.
 *
 * Assistant text streams in chunks, so this converter fails closed: anything
 * it cannot parse exactly returns `ok: false` and the caller keeps the
 * literal source visible instead of guessing.
 *
 * MathML is rendered natively by Chromium, so no stylesheet or font is
 * fetched and the page CSP (script-src chrome://resources 'self') holds. Only
 * the elements rendered below are ever emitted, every character is escaped,
 * and no attribute value is taken from the source, so assistant text cannot
 * introduce markup.
 */

/** Ceilings that keep one response from monopolising the panel thread. */
export interface MathLimits {
  maxSourceLength: number;
  maxTokens: number;
  maxDepth: number;
  maxNodes: number;
}

export const MATH_LIMITS: MathLimits = {
  maxSourceLength: 2048,
  maxTokens: 512,
  maxDepth: 12,
  maxNodes: 512,
};

export type MathRenderResult =
    {ok: true; mathml: string} | {ok: false; reason: string};

interface TokenNode {
  /** Layout hint: sub/superscripts sit under/over this base in display math. */
  limits?: boolean;
}

type MathNode =
    | ({kind: 'mi'; text: string; variant?: string} & TokenNode)
    | ({kind: 'mn'; text: string} & TokenNode)
    | ({kind: 'mo'; text: string; stretchy?: boolean} & TokenNode)
    | {kind: 'mtext'; text: string; variant?: string}
    | {kind: 'mspace'; width: string}
    | {kind: 'mrow'; children: MathNode[]}
    | {kind: 'mfrac'; numerator: MathNode; denominator: MathNode}
    | {kind: 'msqrt'; body: MathNode}
    | {kind: 'mroot'; body: MathNode; index: MathNode}
    | {kind: 'script'; base: MathNode; sub?: MathNode; sup?: MathNode; limits: boolean}
    | {kind: 'accent'; base: MathNode; glyph: string};

type MathToken =
    | {kind: 'number'; value: string}
    | {kind: 'char'; value: string}
    | {kind: 'command'; name: string}
    | {kind: 'space'; width?: string}
    | {kind: 'open'}
    | {kind: 'close'}
    | {kind: 'sup'}
    | {kind: 'sub'};

/** Internal control flow: any parse problem becomes a literal fallback. */
class MathSyntaxError extends Error {}

const WHITESPACE = /\s/;
const LETTER = /[A-Za-z]/;
const DIGIT = /[0-9]/;
const UNICODE_LETTER = /\p{L}/u;
const UNICODE_MATH_SYMBOL = /\p{Sm}/u;

/** Characters TeX typesets as operators, with the glyph it actually shows. */
const OPERATOR_GLYPHS: Readonly<Record<string, string>> = {
  '-': '\u2212',
  '*': '\u2217',
  "'": '\u2032',
};

const OPERATOR_CHARS = new Set([
  '+', '=', '<', '>', '/', '|', '(', ')', '[', ']', ',', ';', ':', '!', '?',
  '.', '@',
]);

interface SymbolSpec {
  readonly glyph: string;
  readonly element: 'mi' | 'mo';
  readonly variant?: string;
  readonly limits?: boolean;
}

/**
 * Builds a command table from compact `name:glyph` pairs; the escapes keep
 * this source file ASCII while the glyphs stay readable at runtime.
 */
function symbols(
    element: 'mi' | 'mo', pairs: string,
    options: {variant?: string; limits?: boolean} = {}):
    Record<string, SymbolSpec> {
  const table: Record<string, SymbolSpec> = {};
  for (const pair of pairs.split(' ')) {
    if (pair.length === 0) {
      continue;
    }
    const separator = pair.indexOf(':');
    const name = pair.slice(0, separator);
    table[name] = {glyph: pair.slice(separator + 1), element, ...options};
  }
  return table;
}

const SYMBOL_COMMANDS: Readonly<Record<string, SymbolSpec>> = {
  ...symbols(
      'mi',
      'alpha:\u03b1 beta:\u03b2 gamma:\u03b3 delta:\u03b4 epsilon:\u03b5 ' +
          'varepsilon:\u03f5 zeta:\u03b6 eta:\u03b7 theta:\u03b8 ' +
          'vartheta:\u03d1 iota:\u03b9 kappa:\u03ba varkappa:\u03f0 ' +
          'lambda:\u03bb mu:\u03bc nu:\u03bd xi:\u03be pi:\u03c0 ' +
          'varpi:\u03d6 rho:\u03c1 varrho:\u03f1 sigma:\u03c3 ' +
          'varsigma:\u03c2 tau:\u03c4 upsilon:\u03c5 phi:\u03d5 ' +
          'varphi:\u03c6 chi:\u03c7 psi:\u03c8 omega:\u03c9 ell:\u2113 ' +
          'hbar:\u210f imath:\u0131 jmath:\u0237'),
  ...symbols(
      'mi',
      'Gamma:\u0393 Delta:\u0394 Theta:\u0398 Lambda:\u039b Xi:\u039e ' +
          'Pi:\u03a0 Sigma:\u03a3 Upsilon:\u03a5 Phi:\u03a6 Psi:\u03a8 ' +
          'Omega:\u03a9',
      {variant: 'normal'}),
  ...symbols(
      'mo',
      'pm:\u00b1 mp:\u2213 times:\u00d7 cdot:\u22c5 div:\u00f7 ' +
          'le:\u2264 leq:\u2264 ge:\u2265 geq:\u2265 ne:\u2260 neq:\u2260 ' +
          'approx:\u2248 equiv:\u2261 sim:\u223c simeq:\u2243 propto:\u221d ' +
          'in:\u2208 notin:\u2209 ni:\u220b subset:\u2282 subseteq:\u2286 ' +
          'supset:\u2283 supseteq:\u2287 cup:\u222a cap:\u2229 ' +
          'setminus:\u2216 emptyset:\u2205 varnothing:\u2205 forall:\u2200 ' +
          'exists:\u2203 nexists:\u2204 neg:\u00ac lnot:\u00ac land:\u2227 ' +
          'wedge:\u2227 lor:\u2228 vee:\u2228 to:\u2192 rightarrow:\u2192 ' +
          'leftarrow:\u2190 leftrightarrow:\u2194 Rightarrow:\u21d2 ' +
          'Leftarrow:\u21d0 Leftrightarrow:\u21d4 mapsto:\u21a6 ' +
          'implies:\u27f9 iff:\u27fa infty:\u221e partial:\u2202 ' +
          'nabla:\u2207 circ:\u2218 bullet:\u2219 ast:\u2217 star:\u22c6 ' +
          'oplus:\u2295 otimes:\u2297 ominus:\u2296 perp:\u22a5 ' +
          'parallel:\u2225 angle:\u2220 triangle:\u25b3 degree:\u00b0 ' +
          'prime:\u2032 cdots:\u22ef ldots:\u2026 dots:\u2026 vdots:\u22ee ' +
          'ddots:\u22f1 therefore:\u2234 because:\u2235 langle:\u27e8 ' +
          'rangle:\u27e9 lceil:\u2308 rceil:\u2309 lfloor:\u230a ' +
          'rfloor:\u230b backslash:\u2216 mid:\u2223'),
  ...symbols(
      'mo',
      'sum:\u2211 prod:\u220f coprod:\u2210 bigcup:\u22c3 bigcap:\u22c2 ' +
          'bigoplus:\u2a01 bigotimes:\u2a02 bigodot:\u2a00 bigvee:\u22c1 ' +
          'bigwedge:\u22c0 bigsqcup:\u2a06',
      {limits: true}),
  ...symbols(
      'mo', 'int:\u222b iint:\u222c iiint:\u222d oint:\u222e',
      {limits: false}),
  ...symbols(
      'mi',
      'sin:sin cos:cos tan:tan cot:cot sec:sec csc:csc arcsin:arcsin ' +
          'arccos:arccos arctan:arctan sinh:sinh cosh:cosh tanh:tanh ' +
          'coth:coth exp:exp log:log ln:ln lg:lg arg:arg deg:deg ' +
          'dim:dim hom:hom ker:ker',
      {variant: 'normal'}),
  ...symbols(
      'mi',
      'lim:lim limsup:limsup liminf:liminf sup:sup inf:inf min:min max:max ' +
          'det:det gcd:gcd mod:mod',
      {variant: 'normal', limits: true}),
};

const ACCENT_COMMANDS: Readonly<Record<string, string>> = {
  hat: '\u02c6',
  widehat: '\u02c6',
  bar: '\u00af',
  overline: '\u00af',
  tilde: '\u02dc',
  widetilde: '\u02dc',
  dot: '\u02d9',
  ddot: '\u00a8',
  vec: '\u2192',
};

/** Text-mode commands: content is literal, the variant styles it as a whole. */
const TEXT_VARIANTS: Readonly<Record<string, string | undefined>> = {
  text: undefined,
  textrm: undefined,
  mathrm: 'normal',
  operatorname: 'normal',
  mathbf: 'bold',
  mathit: 'italic',
  mathsf: 'sans-serif',
  mathtt: 'monospace',
  mathbb: 'double-struck',
  mathcal: 'script',
  mathfrak: 'fraktur',
};

/** Spacing commands: `\,` and friends insert glue rather than dropping out. */
const SPACING_COMMANDS: Readonly<Record<string, string>> = {
  quad: '1em',
  qquad: '2em',
};

const SPACING_ESCAPES: Readonly<Record<string, string>> = {
  ',': '0.167em',
  ':': '0.222em',
  ';': '0.278em',
  '!': '-0.167em',
  ' ': '0.25em',
};

const DELIMITER_GLYPHS: Readonly<Record<string, string>> = {
  '(': '(',
  ')': ')',
  '[': '[',
  ']': ']',
  '|': '|',
  '/': '/',
  '.': '',
};

const DELIMITER_COMMANDS: Readonly<Record<string, string>> = {
  '{': '{',
  '}': '}',
  '|': '\u2016',
  langle: '\u27e8',
  rangle: '\u27e9',
  lceil: '\u2308',
  rceil: '\u2309',
  lfloor: '\u230a',
  rfloor: '\u230b',
  lvert: '|',
  rvert: '|',
  lVert: '\u2016',
  rVert: '\u2016',
  vert: '|',
  Vert: '\u2016',
  uparrow: '\u2191',
  downarrow: '\u2193',
};

function tokenize(source: string): MathToken[] {
  const tokens: MathToken[] = [];
  let index = 0;
  while (index < source.length) {
    const char = source[index] as string;
    if (WHITESPACE.test(char)) {
      let end = index;
      while (end < source.length && WHITESPACE.test(source[end] as string)) {
        end++;
      }
      tokens.push({kind: 'space'});
      index = end;
      continue;
    }
    if (char === '\\') {
      const next = source[index + 1];
      if (next === undefined) {
        throw new MathSyntaxError('dangling escape');
      }
      if (LETTER.test(next)) {
        let end = index + 1;
        while (end < source.length && LETTER.test(source[end] as string)) {
          end++;
        }
        tokens.push({kind: 'command', name: source.slice(index + 1, end)});
        index = end;
        continue;
      }
      const width = SPACING_ESCAPES[next];
      tokens.push(width === undefined ? {kind: 'char', value: next} :
                                         {kind: 'space', width});
      index += 2;
      continue;
    }
    if (char === '{') {
      tokens.push({kind: 'open'});
      index++;
      continue;
    }
    if (char === '}') {
      tokens.push({kind: 'close'});
      index++;
      continue;
    }
    if (char === '^') {
      tokens.push({kind: 'sup'});
      index++;
      continue;
    }
    if (char === '_') {
      tokens.push({kind: 'sub'});
      index++;
      continue;
    }
    if (char === '~') {
      tokens.push({kind: 'space', width: '0.25em'});
      index++;
      continue;
    }
    // Alignment, macro and comment syntax is outside the supported subset.
    if (char === '&' || char === '#' || char === '$' || char === '%') {
      throw new MathSyntaxError(`unsupported character ${char}`);
    }
    if (DIGIT.test(char) ||
        (char === '.' && DIGIT.test(source[index + 1] ?? ''))) {
      const match = /^[0-9]*\.?[0-9]*/.exec(source.slice(index));
      const value = match === null ? char : match[0];
      tokens.push({kind: 'number', value});
      index += value.length;
      continue;
    }
    tokens.push({kind: 'char', value: char});
    index++;
  }
  return tokens;
}

function classifyChar(char: string): MathNode | null {
  if (DIGIT.test(char)) {
    return {kind: 'mn', text: char};
  }
  if (LETTER.test(char) || UNICODE_LETTER.test(char)) {
    return {kind: 'mi', text: char};
  }
  const glyph = OPERATOR_GLYPHS[char] ?? (OPERATOR_CHARS.has(char) ? char : '');
  if (glyph !== '') {
    return {kind: 'mo', text: glyph};
  }
  if (UNICODE_MATH_SYMBOL.test(char)) {
    return {kind: 'mo', text: char};
  }
  return null;
}

function isLimitsBase(node: MathNode): boolean {
  return (node.kind === 'mi' || node.kind === 'mo') && node.limits === true;
}

class MathParser {
  private index = 0;
  private depth = 0;
  private nodes = 0;

  constructor(
      private readonly tokens: MathToken[], private readonly display: boolean,
      private readonly limits: MathLimits) {}

  parse(): MathNode {
    const children = this.parseRow({});
    if (this.peek() !== undefined) {
      throw new MathSyntaxError('trailing tokens');
    }
    return this.track({kind: 'mrow', children});
  }

  private peek(): MathToken | undefined {
    return this.tokens[this.index];
  }

  private take(): MathToken {
    const token = this.tokens[this.index];
    if (token === undefined) {
      throw new MathSyntaxError('unexpected end of expression');
    }
    this.index++;
    return token;
  }

  private track<T extends MathNode>(node: T): T {
    this.nodes++;
    if (this.nodes > this.limits.maxNodes) {
      throw new MathSyntaxError('expression is too large');
    }
    return node;
  }

  private enterGroup(): void {
    this.depth++;
    if (this.depth > this.limits.maxDepth) {
      throw new MathSyntaxError('expression nests too deeply');
    }
  }

  private parseRow(
      {inGroup = false, stopAt, stopAtRight = false}:
          {inGroup?: boolean; stopAt?: string; stopAtRight?: boolean}):
      MathNode[] {
    const children: MathNode[] = [];
    for (;;) {
      const token = this.peek();
      if (token === undefined) {
        if (inGroup || stopAtRight) {
          throw new MathSyntaxError('unclosed group');
        }
        return children;
      }
      if (token.kind === 'space') {
        this.index++;
        if (token.width !== undefined) {
          children.push(this.track({kind: 'mspace', width: token.width}));
        }
        continue;
      }
      if (token.kind === 'close') {
        if (!inGroup) {
          throw new MathSyntaxError('unmatched closing brace');
        }
        return children;
      }
      if (stopAt !== undefined && token.kind === 'char' &&
          token.value === stopAt) {
        return children;
      }
      if (token.kind === 'command' && token.name === 'right') {
        if (!stopAtRight) {
          throw new MathSyntaxError('\\right without \\left');
        }
        return children;
      }
      if (token.kind === 'sup' || token.kind === 'sub') {
        throw new MathSyntaxError('script without a base');
      }
      children.push(this.parseScripts(this.parseAtom()));
    }
  }

  private parseAtom(): MathNode {
    const token = this.take();
    switch (token.kind) {
      case 'number':
        return this.track({kind: 'mn', text: token.value});
      case 'open':
        this.index--;
        return this.parseGroup();
      case 'command':
        return this.parseCommand(token.name);
      case 'char': {
        const node = classifyChar(token.value);
        if (node === null) {
          throw new MathSyntaxError(`unsupported character ${token.value}`);
        }
        return this.track(node);
      }
      default:
        throw new MathSyntaxError('unexpected token');
    }
  }

  private parseGroup(): MathNode {
    this.take();
    this.enterGroup();
    const children = this.parseRow({inGroup: true});
    this.depth--;
    if (this.peek()?.kind !== 'close') {
      throw new MathSyntaxError('unclosed group');
    }
    this.index++;
    return this.track({kind: 'mrow', children});
  }

  /** One token, or a braced group: `x^2` is a script, `\frac{a}{b}` two groups. */
  private parseArgument(): MathNode {
    const token = this.peek();
    if (token === undefined) {
      throw new MathSyntaxError('missing argument');
    }
    if (token.kind === 'open') {
      return this.parseGroup();
    }
    return this.parseAtom();
  }

  private parseScripts(base: MathNode): MathNode {
    let sub: MathNode|undefined;
    let sup: MathNode|undefined;
    for (;;) {
      const token = this.peek();
      if (token?.kind !== 'sub' && token?.kind !== 'sup') {
        break;
      }
      this.index++;
      const argument = this.parseArgument();
      if (token.kind === 'sub') {
        if (sub !== undefined) {
          throw new MathSyntaxError('duplicate subscript');
        }
        sub = argument;
      } else {
        if (sup !== undefined) {
          throw new MathSyntaxError('duplicate superscript');
        }
        sup = argument;
      }
    }
    if (sub === undefined && sup === undefined) {
      return base;
    }
    return this.track({
      kind: 'script',
      base,
      sub,
      sup,
      limits: this.display && isLimitsBase(base),
    });
  }

  private parseCommand(name: string): MathNode {
    const spacing = SPACING_COMMANDS[name];
    if (spacing !== undefined) {
      return this.track({kind: 'mspace', width: spacing});
    }
    const accent = ACCENT_COMMANDS[name];
    if (accent !== undefined) {
      return this.track({kind: 'accent', base: this.parseArgument(), glyph: accent});
    }
    if (name in TEXT_VARIANTS) {
      return this.track({
        kind: 'mtext',
        text: this.parseTextArgument(),
        variant: TEXT_VARIANTS[name],
      });
    }
    const symbol = SYMBOL_COMMANDS[name];
    if (symbol !== undefined) {
      return this.track(symbol.element === 'mi' ?
          {kind: 'mi', text: symbol.glyph, variant: symbol.variant,
           limits: symbol.limits} :
          {kind: 'mo', text: symbol.glyph, limits: symbol.limits});
    }
    switch (name) {
      case 'frac':
      case 'dfrac':
      case 'tfrac': {
        const numerator = this.parseArgument();
        const denominator = this.parseArgument();
        return this.track({kind: 'mfrac', numerator, denominator});
      }
      case 'sqrt':
        return this.parseRoot();
      case 'left':
        return this.parseFenced();
      default:
        // \begin, \href, \includegraphics and every other unlisted command.
        throw new MathSyntaxError(`unsupported command \\${name}`);
    }
  }

  private parseRoot(): MathNode {
    let index: MathNode|undefined;
    const next = this.peek();
    if (next?.kind === 'char' && next.value === '[') {
      this.index++;
      this.enterGroup();
      const children = this.parseRow({inGroup: true, stopAt: ']'});
      this.depth--;
      const closing = this.peek();
      if (closing?.kind !== 'char' || closing.value !== ']') {
        throw new MathSyntaxError('unclosed root index');
      }
      this.index++;
      index = this.track({kind: 'mrow', children});
    }
    const body = this.parseArgument();
    return this.track(
        index === undefined ? {kind: 'msqrt', body} :
                              {kind: 'mroot', body, index});
  }

  private parseFenced(): MathNode {
    const open = this.parseDelimiter();
    this.enterGroup();
    const children = this.parseRow({inGroup: true, stopAtRight: true});
    this.depth--;
    const closing = this.peek();
    if (closing?.kind !== 'command' || closing.name !== 'right') {
      throw new MathSyntaxError('\\left without \\right');
    }
    this.index++;
    const close = this.parseDelimiter();
    const fenced: MathNode[] = [];
    if (open !== '') {
      fenced.push(this.track({kind: 'mo', text: open, stretchy: true}));
    }
    fenced.push(...children);
    if (close !== '') {
      fenced.push(this.track({kind: 'mo', text: close, stretchy: true}));
    }
    return this.track({kind: 'mrow', children: fenced});
  }

  private parseDelimiter(): string {
    const token = this.take();
    if (token.kind === 'char') {
      const glyph = DELIMITER_GLYPHS[token.value];
      if (glyph === undefined) {
        throw new MathSyntaxError(`unsupported delimiter ${token.value}`);
      }
      return glyph;
    }
    if (token.kind === 'command') {
      const glyph = DELIMITER_COMMANDS[token.name];
      if (glyph === undefined) {
        throw new MathSyntaxError(`unsupported delimiter \\${token.name}`);
      }
      return glyph;
    }
    throw new MathSyntaxError('unsupported delimiter');
  }

  /** Text mode: whitespace and characters stay literal until the `}`. */
  private parseTextArgument(): string {
    const open = this.peek();
    if (open?.kind !== 'open') {
      throw new MathSyntaxError('expected a braced group');
    }
    this.index++;
    this.enterGroup();
    let text = '';
    for (;;) {
      const token = this.peek();
      if (token === undefined) {
        throw new MathSyntaxError('unclosed group');
      }
      this.index++;
      if (token.kind === 'close') {
        break;
      }
      if (token.kind === 'space') {
        text += ' ';
        continue;
      }
      if (token.kind === 'number' || token.kind === 'char') {
        text += token.value;
        continue;
      }
      if (token.kind === 'command') {
        const symbol = SYMBOL_COMMANDS[token.name];
        if (symbol === undefined) {
          throw new MathSyntaxError(`unsupported command \\${token.name}`);
        }
        text += symbol.glyph;
        continue;
      }
      throw new MathSyntaxError('unsupported token in text group');
    }
    this.depth--;
    return text;
  }
}

const MATHML_ESCAPES: Readonly<Record<string, string>> = {
  '&': '&amp;',
  '<': '&lt;',
  '>': '&gt;',
  '"': '&quot;',
};

function escapeMathml(value: string): string {
  return value.replace(/[&<>"]/g, char => MATHML_ESCAPES[char] as string);
}

function renderNode(node: MathNode): string {
  switch (node.kind) {
    case 'mi': {
      const variant = node.variant === undefined ?
          '' :
          ` mathvariant="${node.variant}"`;
      return `<mi${variant}>${escapeMathml(node.text)}</mi>`;
    }
    case 'mn':
      return `<mn>${escapeMathml(node.text)}</mn>`;
    case 'mo': {
      const stretchy = node.stretchy === true ? ' stretchy="true"' : '';
      return `<mo${stretchy}>${escapeMathml(node.text)}</mo>`;
    }
    case 'mtext': {
      const variant = node.variant === undefined ?
          '' :
          ` mathvariant="${node.variant}"`;
      return `<mtext${variant}>${escapeMathml(node.text)}</mtext>`;
    }
    case 'mspace':
      return `<mspace width="${node.width}"/>`;
    case 'mrow':
      return `<mrow>${node.children.map(renderNode).join('')}</mrow>`;
    case 'mfrac':
      return `<mfrac>${renderNode(node.numerator)}` +
          `${renderNode(node.denominator)}</mfrac>`;
    case 'msqrt':
      return `<msqrt>${renderNode(node.body)}</msqrt>`;
    case 'mroot':
      return `<mroot>${renderNode(node.body)}${renderNode(node.index)}</mroot>`;
    case 'accent':
      return `<mover accent="true">${renderNode(node.base)}` +
          `<mo>${escapeMathml(node.glyph)}</mo></mover>`;
    case 'script':
      return renderScript(node);
  }
}

function renderScript(node: Extract<MathNode, {kind: 'script'}>): string {
  const base = renderNode(node.base);
  const sub = node.sub === undefined ? undefined : renderNode(node.sub);
  const sup = node.sup === undefined ? undefined : renderNode(node.sup);
  if (node.limits) {
    if (sub !== undefined && sup !== undefined) {
      return `<munderover>${base}${sub}${sup}</munderover>`;
    }
    if (sub !== undefined) {
      return `<munder>${base}${sub}</munder>`;
    }
    return `<mover>${base}${sup}</mover>`;
  }
  if (sub !== undefined && sup !== undefined) {
    return `<msubsup>${base}${sub}${sup}</msubsup>`;
  }
  if (sub !== undefined) {
    return `<msub>${base}${sub}</msub>`;
  }
  return `<msup>${base}${sup}</msup>`;
}

/**
 * Converts one equation body (delimiters already stripped) to MathML.
 *
 * `display` selects block layout and, with it, whether large operators take
 * their limits above and below.
 */
export function renderTexToMathML(
    source: string, display: boolean,
    limits: MathLimits = MATH_LIMITS): MathRenderResult {
  if (source.length > limits.maxSourceLength) {
    return {ok: false, reason: 'source exceeds the length limit'};
  }
  try {
    const tokens = tokenize(source);
    if (tokens.length > limits.maxTokens) {
      return {ok: false, reason: 'expression has too many tokens'};
    }
    const body = new MathParser(tokens, display, limits).parse();
    const layout = display ? 'block' : 'inline';
    return {
      ok: true,
      mathml: `<math class="maho-math maho-math-${layout}" ` +
          `display="${layout}">${renderNode(body)}</math>`,
    };
  } catch (error: unknown) {
    return {
      ok: false,
      reason: error instanceof MathSyntaxError ? error.message : 'invalid math',
    };
  }
}
