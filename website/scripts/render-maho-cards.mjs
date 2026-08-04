import { Resvg } from '@resvg/resvg-js';
import { html } from 'satori-html';
import satori from 'satori';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readdirSync, readFileSync, writeFileSync, mkdirSync } from 'node:fs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const OUT = '/tmp/maho-cards';
mkdirSync(OUT, { recursive: true });

function fontFile(pkg, matcher) {
  const dir = join(dirname(require.resolve(`${pkg}/package.json`)), 'files');
  const m = readdirSync(dir).find((f) => matcher.test(f));
  if (!m) throw new Error(`no font in ${dir} for ${matcher}`);
  return join(dir, m);
}
const fonts = [
  { name: 'Instrument Serif', data: readFileSync(fontFile('@fontsource/instrument-serif', /instrument-serif-latin-400-normal\.woff$/)), weight: 400, style: 'normal' },
  { name: 'Geist', data: readFileSync(fontFile('@fontsource/geist', /geist-latin-400-normal\.woff$/)), weight: 400, style: 'normal' },
  { name: 'Geist', data: readFileSync(fontFile('@fontsource/geist', /geist-latin-500-normal\.woff$/)), weight: 500, style: 'normal' },
];

const cards = [
  { n: 1, tag: 'MAHO', headline: 'What maho is', sub: 'A desktop browser built around Spaces, split view, and a command bar.' },
  { n: 2, tag: 'SPACES', headline: 'Work in separate Spaces', sub: 'Keep projects, contexts, and tab piles apart without switching browsers.' },
  { n: 3, tag: 'PROFILES', headline: 'Profiles when you need them', sub: 'Assign profiles to Spaces so logins, cookies, and sessions stay separate.' },
  { n: 4, tag: 'STILL YOURS', headline: 'Still yours', sub: 'Private by default. AI only when asked. Never forced, never mandatory.' },
];

const S = 1080;
const ACCENT = '#8b7ff5';

function markup(c) {
  return `
  <div style="display:flex;flex-direction:column;width:${S}px;height:${S}px;background:#0a0a0a;position:relative;padding:88px;box-sizing:border-box;">
    <div style="display:flex;position:absolute;top:0;left:0;width:520px;height:520px;background:radial-gradient(circle at top left, rgba(139,127,245,0.16), transparent 70%);"></div>
    <div style="display:flex;justify-content:space-between;align-items:center;">
      <span style="font-family:'Geist Mono','Geist';font-size:22px;letter-spacing:0.18em;text-transform:uppercase;color:${ACCENT};">${c.tag}</span>
      <span style="font-family:'Geist Mono','Geist';font-size:20px;letter-spacing:0.16em;color:rgba(250,250,250,0.45);">${c.n} / 4</span>
    </div>
    <div style="display:flex;flex-direction:column;flex:1;justify-content:center;">
      <div style="display:flex;font-family:'Instrument Serif';font-size:104px;line-height:1.02;letter-spacing:-0.02em;color:#fafafa;max-width:860px;">${c.headline}</div>
      <div style="display:flex;font-family:'Geist';font-weight:400;font-size:38px;line-height:1.4;color:#a1a1aa;margin-top:36px;max-width:820px;">${c.sub}</div>
    </div>
    <div style="display:flex;align-items:center;gap:14px;">
      <div style="display:flex;width:36px;height:2px;background:${ACCENT};"></div>
      <span style="font-family:'Geist Mono','Geist';font-size:20px;letter-spacing:0.18em;text-transform:uppercase;color:#fafafa;">MAHO</span>
    </div>
  </div>`;
}

for (const c of cards) {
  const svg = await satori(html(markup(c)), { width: S, height: S, fonts });
  const png = new Resvg(svg, { fitTo: { mode: 'width', value: S } }).render().asPng();
  const p = join(OUT, `card-${c.n}.png`);
  writeFileSync(p, png);
  console.log(`${p}  ${png.length} bytes`);
}
console.log('DONE');
